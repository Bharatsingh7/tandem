#include "phone.h"

#include "config.h"
#include "images.h"
#include "link.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

static const char *kPkg = "com.cyberwolf.tandem";

Phone::Phone(Link *link, QObject *parent) : QObject(parent), m_link(link) {
    m_lastBatteryLevel = Config::get().value("last_battery_level", -1).toInt();
    m_lastBatteryCharging = Config::get().value("last_battery_charging", false).toBool();
    connect(link, &Link::cameOnline, this, [this] {
        m_link->request("status.get", {}, [this](bool ok, const QJsonValue &r) { if (ok) setStatus(r.toObject()); });
        m_link->request("media.get", {}, [this](bool ok, const QJsonValue &r) { if (ok) setMedia(r.toObject()); });
    });
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &d) {
        if (e == "status") setStatus(d.toObject());
        else if (e == "media") setMedia(d.toObject());
    });
    connect(link, &Link::stateChanged, this, [this] {
        if (!m_link->online() && m_media.value("active").toBool()) setMedia({{"active", false}});
    });
}

void Phone::setStatus(const QJsonObject &s) {
    m_status = s.toVariantMap();
    if (s.contains("battery")) {
        const auto bat = s["battery"].toObject();
        if (bat.contains("level")) {
            m_lastBatteryLevel = bat["level"].toInt();
            m_lastBatteryCharging = bat["charging"].toBool();
            Config::get().setValue("last_battery_level", m_lastBatteryLevel);
            Config::get().setValue("last_battery_charging", m_lastBatteryCharging);
        }
    }
    Q_EMIT statusChanged();
}

void Phone::setMedia(const QJsonObject &m) {
    m_media = m.toVariantMap();
    m_mediaAt = QDateTime::currentMSecsSinceEpoch();
    const QString art = m.value("art").toString();
    const QString key = QString::number(m.value("artKey").toInteger());
    if (!art.isEmpty() && key != m_lastArtKey) {
        Images::get().put("art", QByteArray::fromBase64(art.toLatin1()));
        m_lastArtKey = key;
        ++m_artRev;
    } else if (art.isEmpty() && !m.value("active").toBool()) {
        Images::get().remove("art");
        m_lastArtKey.clear();
        ++m_artRev;
    }
    Q_EMIT mediaChanged();
}

void Phone::setToggle(const QString &name, bool on) {
    m_busy = name;
    Q_EMIT busyChanged();
    m_link->request("toggle.set", {{"name", name}, {"on", on}}, [this, name](bool ok, const QJsonValue &r) {
        m_busy.clear();
        Q_EMIT busyChanged();
        if (ok) setStatus(r.toObject());
        else Q_EMIT toast(QString("Couldn't switch %1: %2").arg(name, r.toString()));
    });
}

void Phone::setRinger(const QString &mode) {
    m_link->request("ringer.set", {{"mode", mode}}, [this](bool ok, const QJsonValue &r) { if (!ok) Q_EMIT toast(r.toString()); });
}

void Phone::setVolume(const QString &stream, int v) { m_link->request("volume.set", {{"stream", stream}, {"v", v}}); }

void Phone::setBrightness(int v, bool automatic) {
    m_link->request("brightness.set", automatic ? QJsonObject{{"auto", true}} : QJsonObject{{"v", v}},
                    [this](bool ok, const QJsonValue &r) { if (!ok) Q_EMIT toast(r.toString()); });
}

void Phone::ring(bool on) {
    m_ringing = on;
    Q_EMIT ringingChanged();
    m_link->request("phone.ring", {{"on", on}}, [this](bool ok, const QJsonValue &r) { if (!ok) Q_EMIT toast(r.toString()); });
    if (on) QTimer::singleShot(45000, this, [this] { if (m_ringing) { m_ringing = false; Q_EMIT ringingChanged(); } });
}

void Phone::lock() {
    m_link->request("phone.lock", {}, [this](bool ok, const QJsonValue &r) { Q_EMIT toast(ok ? "Phone locked" : r.toString()); });
}

void Phone::openUrl(QString url) {
    url = url.trimmed();
    if (url.isEmpty()) return;
    if (!url.contains("://")) url.prepend("https://");
    m_link->request("phone.openUrl", {{"url", url}}, [this](bool ok, const QJsonValue &r) { Q_EMIT toast(ok ? "Opened on phone" : r.toString()); });
}

void Phone::mediaCmd(const QString &cmd, qint64 pos) {
    QJsonObject a{{"cmd", cmd}};
    if (pos >= 0) a["pos"] = pos;
    m_link->request("media.cmd", a);
}

bool Phone::pcUsesHotspot() const {
    return m_link->transport() == "wifi" && m_link->host() == Link::defaultGateway();
}

void Phone::setupOverUsb() {
    const QString serial = m_link->serial();
    if (serial.isEmpty()) {
        Q_EMIT toast("Connect the phone over USB with debugging on");
        return;
    }
    const QString pkg = kPkg;
    QStringList cmds;
    for (const char *p : {"POST_NOTIFICATIONS", "READ_PHONE_STATE", "READ_PHONE_NUMBERS", "CALL_PHONE", "ANSWER_PHONE_CALLS",
                          "READ_CALL_LOG", "WRITE_CALL_LOG", "READ_CONTACTS", "WRITE_CONTACTS", "READ_SMS", "SEND_SMS",
                          "RECEIVE_SMS", "RECEIVE_MMS", "READ_MEDIA_IMAGES", "READ_MEDIA_VIDEO", "BLUETOOTH_CONNECT"})
        cmds << QString("pm grant %1 android.permission.%2").arg(pkg, p);
    cmds << QString("cmd notification allow_listener %1/%1.notify.NotificationMirror").arg(pkg)
         << QString("cmd notification allow_dnd %1").arg(pkg)
         << QString("appops set %1 ACCESS_RESTRICTED_SETTINGS allow").arg(pkg)
         << QString("appops set %1 RUN_ANY_IN_BACKGROUND allow").arg(pkg)
         << QString("appops set %1 MANAGE_ONGOING_CALLS allow").arg(pkg)
         << QString("dumpsys deviceidle whitelist +%1").arg(pkg)
         << QString("cmd telecom add-or-remove-call-companion-app %1 0; cmd telecom add-or-remove-call-companion-app %1 1").arg(pkg)
         << QString("am start -n %1/.MainActivity").arg(pkg);
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        p->deleteLater();
        Q_EMIT toast(code == 0 ? "Phone set up" : "Setup finished with errors");
        m_link->ensureHelper();
    });
    p->start(adbBin(), {"-s", serial, "shell", cmds.join(" ; ")});
}
