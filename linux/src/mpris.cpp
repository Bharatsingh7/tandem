#include "mpris.h"

#include "images.h"
#include "notifier.h"
#include "phone.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>

static const char *kName = "org.mpris.MediaPlayer2.tandem";
static const char *kPath = "/org/mpris/MediaPlayer2";

Mpris::Mpris(Phone *phone, QObject *parent) : QObject(parent), m_phone(phone) {
    new MprisRoot(this);
    new MprisPlayer(this);
    connect(phone, &Phone::mediaChanged, this, &Mpris::sync);
}

qint64 Mpris::positionMs() const {
    const auto m = m_phone->media();
    qint64 pos = m.value("position").toLongLong();
    if (m.value("state") == "playing") pos += QDateTime::currentMSecsSinceEpoch() - m_phone->mediaAt();
    const qint64 dur = m.value("duration").toLongLong();
    return dur > 0 ? qMin(pos, dur) : pos;
}

void Mpris::sync() {
    const auto m = m_phone->media();
    auto bus = QDBusConnection::sessionBus();
    const bool active = m.value("active").toBool();
    if (active && !m_registered) {
        bus.registerObject(kPath, this, QDBusConnection::ExportAdaptors);
        bus.registerService(kName);
        m_registered = true;
    } else if (!active && m_registered) {
        bus.unregisterService(kName);
        bus.unregisterObject(kPath);
        m_registered = false;
        return;
    }
    if (!m_registered) return;
    if (m_phone->artRev() != m_artRev) {
        m_artRev = m_phone->artRev();
        const QByteArray art = Images::get().raw("art");
        m_artPath = art.isEmpty() ? QString() : Notifier::cacheImage(art, "jpg");
    }
    auto *p = findChild<MprisPlayer *>();
    QVariantMap changed{{"PlaybackStatus", p->status()}, {"Metadata", p->metadata()}, {"CanGoNext", p->canNext()},
                        {"CanGoPrevious", p->canPrev()}, {"CanSeek", p->canSeek()}};
    auto sig = QDBusMessage::createSignal(kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    sig << QString("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    bus.send(sig);
}

QString MprisRoot::identity() const {
    const QString app = m_m->phone()->media().value("app").toString();
    return (app.isEmpty() ? QString("Phone") : app) + " (phone)";
}

QString MprisPlayer::status() const {
    const QString s = m_m->phone()->media().value("state").toString();
    return s == "playing" ? "Playing" : s == "paused" ? "Paused" : "Stopped";
}

QVariantMap MprisPlayer::metadata() const {
    const auto m = m_m->phone()->media();
    QVariantMap md{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(QString("/com/cyberwolf/tandem/track/%1").arg(qAbs(m.value("artKey").toLongLong()))))},
                   {"xesam:title", m.value("title").toString()},
                   {"xesam:artist", QStringList{m.value("artist").toString()}},
                   {"xesam:album", m.value("album").toString()},
                   {"mpris:length", m.value("duration").toLongLong() * 1000}};
    if (!m_m->artPath().isEmpty()) md["mpris:artUrl"] = "file://" + m_m->artPath();
    return md;
}

qlonglong MprisPlayer::position() const { return m_m->positionMs() * 1000; }
bool MprisPlayer::canNext() const { return m_m->phone()->media().value("canNext", true).toBool(); }
bool MprisPlayer::canPrev() const { return m_m->phone()->media().value("canPrev", true).toBool(); }
bool MprisPlayer::canSeek() const { return m_m->phone()->media().value("canSeek").toBool(); }
void MprisPlayer::Next() { m_m->phone()->mediaCmd("next"); }
void MprisPlayer::Previous() { m_m->phone()->mediaCmd("prev"); }
void MprisPlayer::Pause() { m_m->phone()->mediaCmd("pause"); }
void MprisPlayer::PlayPause() { m_m->phone()->mediaCmd("toggle"); }
void MprisPlayer::Stop() { m_m->phone()->mediaCmd("pause"); }
void MprisPlayer::Play() { m_m->phone()->mediaCmd("play"); }
void MprisPlayer::Seek(qlonglong offset) { m_m->phone()->mediaCmd("seek", qMax<qint64>(0, m_m->positionMs() + offset / 1000)); }
void MprisPlayer::SetPosition(const QDBusObjectPath &, qlonglong pos) { m_m->phone()->mediaCmd("seek", pos / 1000); }
