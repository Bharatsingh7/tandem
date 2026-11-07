#include "calls.h"

#include "bluetooth.h"
#include "config.h"
#include "contacts.h"
#include "db.h"
#include "images.h"
#include "link.h"
#include "notifier.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <csignal>
#include <sys/prctl.h>

static const char *kMprisPath = "/org/mpris/MediaPlayer2";

using ManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
using CallsMap = QMap<QDBusObjectPath, QVariantMap>;
Q_DECLARE_METATYPE(ManagedObjects)
Q_DECLARE_METATYPE(CallsMap)

Calls::Calls(Link *link, Contacts *contacts, Bluetooth *bt, QObject *parent)
    : QObject(parent), m_link(link), m_contacts(contacts), m_bt(bt),
      m_log({"id", "number", "name", "type", "date", "duration", "place", "cid", "hasPhoto", "day"}) {
    qDBusRegisterMetaType<ManagedObjects>();
    qDBusRegisterMetaType<CallsMap>();
    reloadLog();
    connect(link, &Link::cameOnline, this, [this] {
        m_link->request("call.list", {}, [this](bool ok, const QJsonValue &r) { if (ok) update(r.toObject()); });
        m_link->request("call.accounts", {}, [this](bool ok, const QJsonValue &r) {
            if (!ok) return;
            m_accounts = r.toArray().toVariantList();
            Q_EMIT accountsChanged();
        });
        refreshLog();
        pushPrefs();
    });
    connect(link, &Link::stateChanged, this, [this] {
        if (!m_link->online()) {
            if (m_bt && m_bt->isConnected()) {
                syncBtCalls();
            } else if (!m_calls.isEmpty()) {
                update({});
            }
        }
    });
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &d) {
        if (e == "call.state") update(d.toObject());
        else if (e == "call.logChanged") refreshLog();
    });
    connect(contacts, &Contacts::changed, this, &Calls::reloadLog);
    if (m_bt) {
        connect(m_bt, &Bluetooth::changed, this, [this] {
            if (m_bt->isConnected()) {
                if (m_telephonyAgPath.isEmpty()) discoverTelephonyAg();
                else checkBtCalls();
            }
        });
    }
    initTelephony();
}

Calls::~Calls() {
    stopRingtone();
}

void Calls::startRingtone() {
    if (m_ringer) return;
    m_ringer = new QProcess(this);
    m_ringer->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_ringer->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });

    QString soundFile = QDir::homePath() + "/.local/share/sounds/scifi-calm/stereo/phone-incoming-call.oga";
    if (!QFile::exists(soundFile))
        soundFile = "/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga";

    if (!QFile::exists(soundFile)) return;

    connect(m_ringer, &QProcess::finished, this, [this] {
        if (m_ringer) {
            m_ringer->deleteLater();
            m_ringer.clear();
            bool stillRinging = false;
            for (const auto &c : m_calls)
                if (c.toMap()["state"] == "ringing") stillRinging = true;
            if (stillRinging) startRingtone();
        }
    });

    if (QFile::exists("/usr/bin/pw-play")) {
        m_ringer->start("pw-play", {soundFile});
    } else if (QFile::exists("/usr/bin/canberra-gtk-play")) {
        m_ringer->start("canberra-gtk-play", {"-f", soundFile});
    }
}

void Calls::stopRingtone() {
    if (m_ringer) {
        m_ringer->terminate();
        m_ringer->deleteLater();
        m_ringer.clear();
    }
}

void Calls::pushPrefs() {
    const QString addr = m_bt->address();
    if (!addr.isEmpty() && m_link->online()) m_link->request("call.prefs", {{"pcBt", addr}});
}

QVariantMap Calls::enrich(const QJsonObject &c) const {
    QVariantMap m = c.toVariantMap();
    const QString number = c["number"].toString();
    const auto hit = m_contacts->lookup(number);
    const QString name = hit.value("name").toString().isEmpty() ? c["name"].toString() : hit["name"].toString();
    m["title"] = name.isEmpty() ? (number.isEmpty() ? QString("Unknown number") : Contacts::pretty(number)) : name;
    m["subtitle"] = name.isEmpty() ? QString() : Contacts::pretty(number);
    m["cid"] = hit.value("cid", 0);
    m["hasPhoto"] = hit.value("hasPhoto", false);
    return m;
}

void Calls::update(const QJsonObject &d) {
    const bool wasInCall = !m_calls.isEmpty();
    QVariantList calls;
    for (const auto &c : d["calls"].toArray()) calls << enrich(c.toObject());
    m_calls = calls;
    m_audio = d["audio"].toObject().toVariantMap();
    const auto ended = d["ended"].toObject();
    if (!ended.isEmpty() && ended["incoming"].toBool() && ended["missed"].toBool()) missed(ended);

    QVariantMap ringingCall;
    for (const auto &c : m_calls)
        if (c.toMap()["state"] == "ringing") ringingCall = c.toMap();
    if (!ringingCall.isEmpty()) {
        ringNotify(ringingCall);
        startRingtone();
        Q_EMIT ringing();
    } else {
        stopRingtone();
        if (m_ringId) {
            Notifier::get().close(m_ringId);
            m_ringId = 0;
        }
    }

    if (!m_calls.isEmpty() && !wasInCall) {
        m_justEnded = false;
        if (Config::get().value("pause_media_on_call", true).toBool()) pauseDesktop();
    } else if (m_calls.isEmpty() && wasInCall) {
        resumeDesktop();
        m_justEnded = true;
        QTimer::singleShot(2000, this, [this] { m_justEnded = false; Q_EMIT callsChanged(); });
    }
    bridgeAudio(!m_calls.isEmpty() && m_audio.value("onPc").toBool());
    Q_EMIT callsChanged();
}

// ---- call audio bridge ----
// With the PC as the phone's hands-free unit, PipeWire exposes the call as a Bluetooth source (caller's voice)
// and sink (our mic). Nothing routes those by itself, so while a call is on the PC we loop them to the defaults.

void Calls::bridgeAudio(bool on) {
    if (on == m_bridgeWanted) return;
    m_bridgeWanted = on;
    if (on) {
        findPhoneNodes(0);
        return;
    }
    for (QProcess *p : {m_voice.data(), m_mic.data()})
        if (p) { p->terminate(); p->deleteLater(); }
    m_voice.clear();
    m_mic.clear();
}

void Calls::findPhoneNodes(int attempt) {
    if (!m_bridgeWanted || (m_voice && m_mic)) return;
    const QString addr = Config::get().value("phone_bt").toString();
    auto *dump = new QProcess(this);
    connect(dump, &QProcess::finished, this, [this, dump, addr, attempt] {
        dump->deleteLater();
        if (!m_bridgeWanted) return;
        QString source, sink;
        for (const auto &v : QJsonDocument::fromJson(dump->readAllStandardOutput()).array()) {
            const auto o = v.toObject();
            if (o["type"].toString() != "PipeWire:Interface:Node") continue;
            const auto props = o["info"].toObject()["props"].toObject();
            if (props["api.bluez5.address"].toString().compare(addr, Qt::CaseInsensitive) != 0) continue;
            const QString cls = props["media.class"].toString(), name = props["node.name"].toString();
            const QString profile = props["api.bluez5.profile"].toString();
            if (!profile.isEmpty() && !profile.contains("headset") && !profile.contains("hfp") && !profile.contains("hsp") && !profile.contains("gateway")) continue;
            if (cls == "Audio/Source") source = name;
            else if (cls == "Audio/Sink") sink = name;
        }
        auto loop = [this](const QStringList &args) {
            auto *p = new QProcess(this);
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
            p->start("pw-loopback", args);
            return p;
        };
        if (!source.isEmpty() && !m_voice)
            m_voice = loop({"-n", "tandem-call-voice", "-c", "1", "-m", "[ MONO ]", "-C", source,
                            "-o", "media.role=Communication node.description=\"Phone call\""});
        if (!sink.isEmpty() && !m_mic)
            m_mic = loop({"-n", "tandem-call-mic", "-c", "1", "-m", "[ MONO ]", "-P", sink,
                          "-i", "media.role=Communication node.description=\"Phone call microphone\""});
        // SCO nodes appear a moment after the phone switches audio over; keep looking for a few seconds.
        if ((!m_voice || !m_mic) && attempt < 15) QTimer::singleShot(600, this, [this, attempt] { findPhoneNodes(attempt + 1); });
    });
    dump->start("pw-dump", {});
}

void Calls::ringNotify(const QVariantMap &c) {
    if (m_ringId) return;  // one popup per ringing call
    Notifier::Opts o;
    o.actions = {{"answer", "Answer"}, {"decline", "Decline"}};
    o.urgency = 2;
    o.category = "call.incoming";
    o.resident = true;
    o.timeout = 0;
    if (c["hasPhoto"].toBool()) o.image = Notifier::cacheImage(Images::get().raw("contact/" + c["cid"].toString()), "jpg");
    const int id = c["id"].toInt();
    Notifier::get().notify("Incoming call · " + c["title"].toString(), c["subtitle"].toString(), o,
                           [this, id](const QString &a) {
                               if (a == "answer") answer(id);
                               else if (a == "decline") reject(id);
                           },
                           [this](uint nid) { m_ringId = nid; });
}

void Calls::missed(const QJsonObject &c) {
    const auto m = enrich(c);
    const QString number = c["number"].toString();
    Notifier::Opts o;
    if (!number.isEmpty()) o.actions = {{"call", "Call back"}, {"sms", "Message"}};
    o.category = "call.unanswered";
    if (m["hasPhoto"].toBool()) o.image = Notifier::cacheImage(Images::get().raw("contact/" + m["cid"].toString()), "jpg");
    Notifier::get().notify("Missed call · " + m["title"].toString(),
                           m["subtitle"].toString().isEmpty() ? QDateTime::currentDateTime().toString("HH:mm") : m["subtitle"].toString(),
                           o, [this, number](const QString &a) {
                               if (a == "call") dial(number);
                               else if (a == "sms") Q_EMIT showSms(number);
                           });
}

// ---- call log ----

void Calls::refreshLog() {
    const qint64 since = Db::meta("calllog_since", 0).toLongLong();
    m_link->request("call.log", {{"limit", 500}, {"since", since}}, [this](bool ok, const QJsonValue &r) {
        if (!ok) return;
        qint64 maxDate = 0;
        Db::begin();
        for (const auto &v : r.toArray()) {
            const auto o = v.toObject();
            Db::exec("INSERT OR REPLACE INTO calllog VALUES(?,?,?,?,?,?,?,?)",
                     {o["id"].toInteger(), o["number"].toString(), o["name"].toString(), o["type"].toString(),
                      o["date"].toInteger(), o["duration"].toInteger(), o["account"].toString(), o["place"].toString()});
            maxDate = qMax(maxDate, o["date"].toInteger());
        }
        Db::commit();
        if (maxDate) Db::setMeta("calllog_since", maxDate - 1);
        reloadLog();
    });
}

static QString dayLabel(qint64 ms) {
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date(), today = QDate::currentDate();
    if (d == today) return "Today";
    if (d == today.addDays(-1)) return "Yesterday";
    if (d.daysTo(today) < 7) return d.toString("dddd");
    return d.year() == today.year() ? d.toString("d MMMM") : d.toString("d MMMM yyyy");
}

void Calls::reloadLog() {
    QString where;
    if (m_filter == "missed") where = "WHERE type IN ('missed','rejected')";
    else if (m_filter == "in" || m_filter == "out") where = QString("WHERE type='%1'").arg(m_filter);
    auto q = Db::exec("SELECT * FROM calllog " + where + " ORDER BY date DESC LIMIT 500");
    QVector<QVariantMap> items;
    while (q.next()) {
        const QString number = q.value("number").toString();
        const auto hit = m_contacts->lookup(number);
        QString name = hit.value("name").toString();
        if (name.isEmpty()) name = q.value("name").toString();
        items.append({{"id", q.value("id")}, {"number", number},
                      {"name", name.isEmpty() ? (number.isEmpty() ? QString("Private number") : Contacts::pretty(number)) : name},
                      {"type", q.value("type")}, {"date", q.value("date")}, {"duration", q.value("duration")},
                      {"place", q.value("place")}, {"cid", hit.value("cid", 0)}, {"hasPhoto", hit.value("hasPhoto", false)},
                      {"day", dayLabel(q.value("date").toLongLong())}});
    }
    m_log.setItems(std::move(items));
}

void Calls::setLogFilter(const QString &f) {
    if (f == m_filter) return;
    m_filter = f;
    Q_EMIT logFilterChanged();
    reloadLog();
}

void Calls::deleteLog(qint64 id) {
    Db::exec("DELETE FROM calllog WHERE id=?", {id});
    m_link->request("call.logDelete", {{"id", id}});
    reloadLog();
}

QString Calls::lastDialed() const { return Db::scalar("SELECT number FROM calllog WHERE type='out' ORDER BY date DESC LIMIT 1").toString(); }

// ---- actions ----

void Calls::err(bool ok, const QJsonValue &r) {
    if (!ok) Q_EMIT toast(r.toString());
}

bool Calls::ensurePcAudio() {
    const QString phoneBt = Config::get().value("phone_bt").toString();
    if (!Config::get().value("call_audio_on_pc", true).toBool() || phoneBt.isEmpty() || !m_bt->available()) return false;
    m_bt->connectPhone(phoneBt);
    return true;
}

void Calls::answer(int id) {
    stopRingtone();
    const bool pc = ensurePcAudio();
    if (!m_link->online()) {
        QString btPath;
        for (const auto &v : m_calls) {
            const auto m = v.toMap();
            if (m.contains("btPath")) { btPath = m["btPath"].toString(); break; }
        }
        if (!btPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", btPath, "org.pipewire.Telephony.Call1", "Answer");
            QDBusConnection::sessionBus().asyncCall(msg);
            bridgeAudio(true);
        }
        return;
    }
    QJsonObject a{{"pc", pc}};
    if (id) a["id"] = id;
    m_link->request("call.answer", a, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

void Calls::reject(int id, const QString &text) {
    stopRingtone();
    if (!m_link->online()) {
        QString btPath;
        for (const auto &v : m_calls) {
            const auto m = v.toMap();
            if (m.contains("btPath")) { btPath = m["btPath"].toString(); break; }
        }
        if (!btPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", btPath, "org.pipewire.Telephony.Call1", "Hangup");
            QDBusConnection::sessionBus().asyncCall(msg);
        } else if (!m_telephonyAgPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", m_telephonyAgPath, "org.ofono.VoiceCallManager", "HangupAll");
            QDBusConnection::sessionBus().asyncCall(msg);
        }
        return;
    }
    QJsonObject a;
    if (id) a["id"] = id;
    if (!text.isEmpty()) a["text"] = text;
    m_link->request("call.reject", a, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

void Calls::end(int id) {
    stopRingtone();
    if (!m_link->online()) {
        QString btPath;
        for (const auto &v : m_calls) {
            const auto m = v.toMap();
            if (m.contains("btPath")) { btPath = m["btPath"].toString(); break; }
        }
        if (!btPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", btPath, "org.pipewire.Telephony.Call1", "Hangup");
            QDBusConnection::sessionBus().asyncCall(msg);
        } else if (!m_telephonyAgPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", m_telephonyAgPath, "org.ofono.VoiceCallManager", "HangupAll");
            QDBusConnection::sessionBus().asyncCall(msg);
        }
        bridgeAudio(false);
        return;
    }
    m_link->request("call.end", id ? QJsonObject{{"id", id}} : QJsonObject{}, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

void Calls::hold(int id, bool on) { m_link->request("call.hold", {{"id", id}, {"on", on}}, [this](bool ok, const QJsonValue &r) { err(ok, r); }); }
void Calls::swap() { m_link->request("call.swap", {}, [this](bool ok, const QJsonValue &r) { err(ok, r); }); }
void Calls::merge(int id) { m_link->request("call.merge", {{"id", id}}, [this](bool ok, const QJsonValue &r) { err(ok, r); }); }
void Calls::mute(bool on) {
    if (!m_link->online()) {
        m_audio["muted"] = on;
        Q_EMIT callsChanged();
        return;
    }
    m_link->request("call.mute", {{"on", on}}, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

void Calls::dtmf(const QString &digits, int id) {
    QJsonObject a{{"digits", digits}};
    if (id) a["id"] = id;
    m_link->request("call.dtmf", a);
}

void Calls::route(const QString &to, int id) {
    if (to == "pc") ensurePcAudio();
    QJsonObject a{{"to", to == "phone" ? QString("earpiece") : to}};
    if (id) a["id"] = id;
    m_link->request("call.route", a, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

void Calls::dial(const QString &number, const QString &account) {
    if (number.trimmed().isEmpty()) return;
    if (!m_link->online()) {
        if (!m_telephonyAgPath.isEmpty()) {
            auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", m_telephonyAgPath, "org.ofono.VoiceCallManager", "Dial");
            msg << number.trimmed();
            QDBusConnection::sessionBus().asyncCall(msg);
            ensurePcAudio();
            bridgeAudio(true);
            return;
        }
        Q_EMIT toast("Phone not connected");
        return;
    }
    QJsonObject a{{"number", number.trimmed()}, {"pc", ensurePcAudio()}};
    if (!account.isEmpty()) a["account"] = account;
    m_link->request("call.dial", a, [this](bool ok, const QJsonValue &r) { err(ok, r); });
}

// ---- PipeWire Telephony (Bluetooth HFP fallback) ----

void Calls::initTelephony() {
    auto bus = QDBusConnection::sessionBus();
    bus.connect("org.pipewire.Telephony", "", "org.ofono.VoiceCallManager", "CallAdded",
                this, SLOT(onBtCallAdded(QDBusObjectPath, QVariantMap)));
    bus.connect("org.pipewire.Telephony", "", "org.ofono.VoiceCallManager", "CallRemoved",
                this, SLOT(onBtCallRemoved(QDBusObjectPath)));
    bus.connect("org.pipewire.Telephony", "", "org.freedesktop.DBus.Properties", "PropertiesChanged",
                this, SLOT(onBtPropertiesChanged(QString, QVariantMap, QStringList)));
    bus.connect("org.pipewire.Telephony", "/org/pipewire/Telephony", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                this, SLOT(onTelephonyInterfacesAdded(QDBusMessage)));
    bus.connect("org.pipewire.Telephony", "/org/pipewire/Telephony", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
                this, SLOT(onTelephonyInterfacesRemoved(QDBusMessage)));
    discoverTelephonyAg();
}

void Calls::discoverTelephonyAg() {
    const QString wantAddr = Config::get().value("phone_bt").toString();
    auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", "/org/pipewire/Telephony",
                                              "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, wantAddr] {
        QDBusPendingReply<ManagedObjects> r = *w;
        w->deleteLater();
        if (r.isError()) return;
        const auto objs = r.value();
        for (auto it = objs.begin(); it != objs.end(); ++it) {
            const auto &ifaces = it.value();
            if (ifaces.contains("org.pipewire.Telephony.AudioGateway1")) {
                const QString addr = ifaces.value("org.pipewire.Telephony.AudioGateway1").value("Address").toString();
                if (wantAddr.isEmpty() || addr.compare(wantAddr, Qt::CaseInsensitive) == 0) {
                    m_telephonyAgPath = it.key().path();
                    break;
                }
            }
        }
        if (!m_telephonyAgPath.isEmpty()) checkBtCalls();
    });
}

void Calls::checkBtCalls() {
    if (m_telephonyAgPath.isEmpty()) return;
    auto msg = QDBusMessage::createMethodCall("org.pipewire.Telephony", m_telephonyAgPath,
                                              "org.ofono.VoiceCallManager", "GetCalls");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        QDBusPendingReply<CallsMap> r = *w;
        w->deleteLater();
        if (r.isError()) return;
        m_btCalls.clear();
        const auto calls = r.value();
        for (auto it = calls.begin(); it != calls.end(); ++it) {
            addBtCall(it.key().path(), it.value());
        }
        syncBtCalls();
    });
}

void Calls::addBtCall(const QString &path, const QVariantMap &props) {
    const QString number = props.value("LineIdentification").toString();
    QString state = props.value("State").toString();
    if (state == "incoming") state = "ringing";
    else if (state == "alerting" || state == "dialing") state = "dialing";
    else if (state == "active") state = "active";
    else if (state == "held") state = "holding";
    else if (state.isEmpty()) state = "active";

    const auto hit = m_contacts->lookup(number);
    QString name = hit.value("name").toString();
    if (name.isEmpty()) name = props.value("Name").toString();

    QVariantMap c;
    c["id"] = 1;
    c["number"] = number;
    c["name"] = name;
    c["title"] = name.isEmpty() ? (number.isEmpty() ? QString("Unknown caller") : Contacts::pretty(number)) : name;
    c["subtitle"] = name.isEmpty() ? QString() : Contacts::pretty(number);
    c["state"] = state;
    c["incoming"] = (state == "ringing" || props.value("State").toString() == "incoming");
    c["since"] = QDateTime::currentMSecsSinceEpoch();
    c["btPath"] = path;
    c["cid"] = hit.value("cid", 0);
    c["hasPhoto"] = hit.value("hasPhoto", false);
    m_btCalls[path] = c;
}

void Calls::syncBtCalls() {
    if (m_link->online()) return;
    m_calls.clear();
    for (const auto &v : m_btCalls) m_calls.append(v);
    m_audio = QVariantMap{{"onPc", true}, {"muted", false}, {"route", "bluetooth"}};

    QVariantMap ringingCall;
    for (const auto &c : m_calls) {
        if (c.toMap()["state"] == "ringing") ringingCall = c.toMap();
    }
    if (!ringingCall.isEmpty()) {
        ringNotify(ringingCall);
        startRingtone();
        Q_EMIT ringing();
    } else {
        stopRingtone();
        if (m_ringId) {
            Notifier::get().close(m_ringId);
            m_ringId = 0;
        }
    }
    bridgeAudio(!m_calls.isEmpty());
    Q_EMIT callsChanged();
}

void Calls::onBtCallAdded(const QDBusObjectPath &path, const QVariantMap &props) {
    addBtCall(path.path(), props);
    syncBtCalls();
}

void Calls::onBtCallRemoved(const QDBusObjectPath &path) {
    m_btCalls.remove(path.path());
    syncBtCalls();
    if (m_btCalls.isEmpty()) {
        stopRingtone();
        bridgeAudio(false);
        m_justEnded = true;
        QTimer::singleShot(2000, this, [this] { m_justEnded = false; Q_EMIT callsChanged(); });
    }
}

void Calls::onBtPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &) {
    if (iface != "org.pipewire.Telephony.Call1" && iface != "org.ofono.VoiceCall") return;
    bool changedAny = false;
    for (auto it = m_btCalls.begin(); it != m_btCalls.end(); ++it) {
        if (changed.contains("State")) {
            QString state = changed.value("State").toString();
            if (state == "incoming") state = "ringing";
            else if (state == "alerting" || state == "dialing") state = "dialing";
            else if (state == "active") state = "active";
            else if (state == "held") state = "holding";
            it.value()["state"] = state;
            changedAny = true;
        }
        if (changed.contains("LineIdentification")) {
            const QString number = changed.value("LineIdentification").toString();
            it.value()["number"] = number;
            const auto hit = m_contacts->lookup(number);
            QString name = hit.value("name").toString();
            it.value()["title"] = name.isEmpty() ? Contacts::pretty(number) : name;
            it.value()["subtitle"] = name.isEmpty() ? QString() : Contacts::pretty(number);
            changedAny = true;
        }
    }
    if (changedAny) syncBtCalls();
}

void Calls::onTelephonyInterfacesAdded(const QDBusMessage &m) {
    const QString path = m.arguments().value(0).value<QDBusObjectPath>().path();
    if (path.contains("/ag")) discoverTelephonyAg();
}

void Calls::onTelephonyInterfacesRemoved(const QDBusMessage &m) {
    const QString path = m.arguments().value(0).value<QDBusObjectPath>().path();
    if (path == m_telephonyAgPath) {
        m_telephonyAgPath.clear();
        m_btCalls.clear();
        syncBtCalls();
    }
}

// ---- desktop media ----

void Calls::pauseDesktop() {
    m_paused.clear();
    auto bus = QDBusConnection::sessionBus();
    const QStringList names = bus.interface()->registeredServiceNames().value();
    for (const QString &n : names) {
        if (!n.startsWith("org.mpris.MediaPlayer2.") || n == "org.mpris.MediaPlayer2.tandem") continue;
        auto msg = QDBusMessage::createMethodCall(n, kMprisPath, "org.freedesktop.DBus.Properties", "Get");
        msg << QString("org.mpris.MediaPlayer2.Player") << QString("PlaybackStatus");
        auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 800), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, n] {
            QDBusPendingReply<QDBusVariant> r = *w;
            w->deleteLater();
            if (r.isError() || r.value().variant().toString() != "Playing") return;
            QDBusConnection::sessionBus().asyncCall(
                QDBusMessage::createMethodCall(n, kMprisPath, "org.mpris.MediaPlayer2.Player", "Pause"));
            m_paused << n;
        });
    }
}

void Calls::resumeDesktop() {
    for (const QString &n : std::exchange(m_paused, {}))
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(n, kMprisPath, "org.mpris.MediaPlayer2.Player", "Play"));
}
