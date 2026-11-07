#include "stateexport.h"

#include "bluetooth.h"
#include "calls.h"
#include "config.h"
#include "images.h"
#include "link.h"
#include "messages.h"
#include "notifs.h"
#include "phone.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString StateExport::dir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (d.isEmpty()) d = QDir::homePath() + "/.local/share/tandem";
    return d + "/tandem";
}

static void writePrivate(const QString &path, const QByteArray &data) {
    // Written in place (one write) so file watchers keep their inotify watch.
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
}

StateExport::StateExport(Link *link, Phone *phone, Calls *calls, Messages *messages, Notifs *notifs, Bluetooth *bt, QObject *parent)
    : QObject(parent), m_link(link), m_phone(phone), m_calls(calls), m_messages(messages), m_notifs(notifs), m_bt(bt) {
    QDir().mkpath(dir());
    QFile::setPermissions(dir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &StateExport::write);
    auto poke = [this] { if (!m_debounce.isActive()) m_debounce.start(); };
    connect(link, &Link::stateChanged, this, poke);
    connect(phone, &Phone::statusChanged, this, poke);
    connect(phone, &Phone::mediaChanged, this, poke);
    connect(phone, &Phone::busyChanged, this, poke);
    connect(calls, &Calls::callsChanged, this, poke);
    connect(messages, &Messages::unreadChanged, this, poke);
    connect(notifs, &Notifs::unreadChanged, this, poke);
    connect(notifs->model(), &ListModel::countChanged, this, poke);
    if (bt) {
        connect(bt, &Bluetooth::changed, this, poke);
        connect(bt, &Bluetooth::batteryChanged, this, poke);
    }
    // Heartbeat: readers treat a snapshot older than ~75 s as "Tandem not running".
    m_heartbeat.setInterval(30000);
    connect(&m_heartbeat, &QTimer::timeout, this, &StateExport::write);
    m_heartbeat.start();
    write();
}

StateExport::~StateExport() {
    QFile::remove(dir() + "/state.json");
    QFile::remove(dir() + "/art.jpg");
}

void StateExport::write() {
    const auto st = m_phone->status();
    const auto media = m_phone->media();
    if (m_phone->artRev() != m_artRev) {
        m_artRev = m_phone->artRev();
        const QByteArray art = Images::get().raw("art");
        if (art.isEmpty()) QFile::remove(dir() + "/art.jpg");
        else writePrivate(dir() + "/art.jpg", art);
    }
    QJsonArray calls;
    for (const auto &v : m_calls->calls()) {
        const auto c = v.toMap();
        calls.append(QJsonObject{{"id", c["id"].toInt()}, {"state", c["state"].toString()}, {"title", c["title"].toString()},
                                 {"subtitle", c["subtitle"].toString()}, {"since", c["since"].toLongLong()},
                                 {"incoming", c["incoming"].toBool()}});
    }
    QJsonArray notifs;
    auto *model = m_notifs->model();
    for (int i = 0; i < qMin(4, model->count()); ++i) {
        const auto n = model->get(i);
        notifs.append(QJsonObject{{"app", n["app"].toString()}, {"title", n["title"].toString()},
                                  {"text", n["text"].toString().left(160)}, {"time", n["time"].toLongLong()}});
    }
    const bool isBt = !m_link->online() && m_bt && m_bt->isConnected();
    const QString effectiveState = isBt ? "bluetooth" : m_link->state();
    const QString effectiveTransport = isBt ? "bluetooth" : m_link->transport();

    QJsonObject batteryObj = QJsonObject::fromVariantMap(st.value("battery").toMap());
    if (batteryObj.isEmpty() && isBt) {
        const int pct = m_bt->batteryPercent();
        if (pct >= 0) {
            batteryObj = QJsonObject{{"level", pct}, {"charging", false}, {"plugged", false}};
        } else if (m_phone->lastBatteryLevel() > 0) {
            batteryObj = QJsonObject{{"level", m_phone->lastBatteryLevel()},
                                     {"charging", m_phone->lastBatteryCharging()},
                                     {"plugged", false}};
        }
    }

    QJsonObject audioObj;
    if (m_link->online()) {
        const auto audio = m_calls->audio();
        audioObj = QJsonObject{{"onPc", audio.value("onPc").toBool()}, {"muted", audio.value("muted").toBool()},
                               {"route", audio.value("route").toString()}};
    } else if (isBt) {
        audioObj = QJsonObject{{"onPc", true}, {"muted", false}, {"route", "bluetooth"}};
    }

    QString phoneName = m_link->phoneName();
    if (phoneName.isEmpty()) phoneName = st.value("model").toString();
    if (phoneName.isEmpty()) phoneName = Config::get().value("phone_name").toString();
    if (phoneName.isEmpty()) phoneName = Config::get().value("phone_bt_name").toString();
    if (phoneName.isEmpty()) phoneName = "Motorola Edge 60 Pro";

    QJsonObject o{
        {"ts", QDateTime::currentMSecsSinceEpoch()},
        {"online", m_link->online() || isBt},
        {"state", effectiveState},
        {"transport", effectiveTransport},
        {"helper", m_link->helper()},
        {"phone", phoneName},
        {"battery", batteryObj},
        {"network", QJsonObject::fromVariantMap(st.value("network").toMap())},
        {"toggles", QJsonObject::fromVariantMap(st.value("toggles").toMap())},
        {"ringer", st.value("ringer").toString()},
        {"busy", m_phone->busy()},
        {"media", QJsonObject{{"active", media.value("active").toBool()}, {"title", media.value("title").toString()},
                              {"artist", media.value("artist").toString()}, {"app", media.value("app").toString()},
                              {"state", media.value("state").toString()}, {"art", m_artRev > 0 && QFile::exists(dir() + "/art.jpg") ? m_artRev : 0}}},
        {"calls", calls},
        {"audio", audioObj},
        {"unread", m_messages->unread()},
        {"notifCount", model->count()},
        {"notifs", notifs},
    };
    writePrivate(dir() + "/state.json", QJsonDocument(o).toJson(QJsonDocument::Compact));
}
