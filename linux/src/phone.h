#pragma once
#include <QObject>
#include <QVariantMap>

class Link;

// Phone status, now-playing and quick actions (toggles, volume, ring, lock, open link, USB setup).
class Phone : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap media READ media NOTIFY mediaChanged)
    Q_PROPERTY(qint64 mediaAt READ mediaAt NOTIFY mediaChanged)
    Q_PROPERTY(int artRev READ artRev NOTIFY mediaChanged)
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool ringing READ ringing NOTIFY ringingChanged)
public:
    explicit Phone(Link *link, QObject *parent = nullptr);
    QVariantMap status() const { return m_status; }
    QVariantMap media() const { return m_media; }
    qint64 mediaAt() const { return m_mediaAt; }
    int artRev() const { return m_artRev; }
    QString busy() const { return m_busy; }
    bool ringing() const { return m_ringing; }

    int lastBatteryLevel() const { return m_lastBatteryLevel; }
    bool lastBatteryCharging() const { return m_lastBatteryCharging; }

    Q_INVOKABLE void setToggle(const QString &name, bool on);
    Q_INVOKABLE void setRinger(const QString &mode);
    Q_INVOKABLE void setVolume(const QString &stream, int v);
    Q_INVOKABLE void setBrightness(int v, bool automatic = false);
    Q_INVOKABLE void ring(bool on);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void openUrl(QString url);
    Q_INVOKABLE void mediaCmd(const QString &cmd, qint64 pos = -1);
    Q_INVOKABLE void setupOverUsb();
    Q_INVOKABLE bool pcUsesHotspot() const;

Q_SIGNALS:
    void statusChanged();
    void mediaChanged();
    void busyChanged();
    void ringingChanged();
    void toast(const QString &text);

private:
    void setStatus(const QJsonObject &s);
    void setMedia(const QJsonObject &m);
    Link *m_link;
    QVariantMap m_status, m_media;
    qint64 m_mediaAt = 0;
    int m_artRev = 0;
    int m_lastBatteryLevel = -1;
    bool m_lastBatteryCharging = false;
    QString m_busy, m_lastArtKey;
    bool m_ringing = false;
};
