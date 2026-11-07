#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QVariantMap>

class Phone;

// Phone media as org.mpris.MediaPlayer2.tandem, so desktop media widgets and keys control it.
class Mpris : public QObject {
    Q_OBJECT
public:
    explicit Mpris(Phone *phone, QObject *parent = nullptr);
    Phone *phone() const { return m_phone; }
    qint64 positionMs() const;
    QString artPath() const { return m_artPath; }
Q_SIGNALS:
    void raiseRequested();
private:
    void sync();
    Phone *m_phone;
    bool m_registered = false;
    QString m_artPath;
    int m_artRev = -1;
};

class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ no)
    Q_PROPERTY(bool CanRaise READ yes)
    Q_PROPERTY(bool HasTrackList READ no)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ empty)
    Q_PROPERTY(QStringList SupportedMimeTypes READ empty)
public:
    explicit MprisRoot(Mpris *m) : QDBusAbstractAdaptor(m), m_m(m) {}
    bool no() const { return false; }
    bool yes() const { return true; }
    QString identity() const;
    QString desktopEntry() const { return "com.cyberwolf.Tandem"; }
    QStringList empty() const { return {}; }
public Q_SLOTS:
    void Raise() { Q_EMIT m_m->raiseRequested(); }
    void Quit() {}
private:
    Mpris *m_m;
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(double Rate READ one)
    Q_PROPERTY(double MinimumRate READ one)
    Q_PROPERTY(double MaximumRate READ one)
    Q_PROPERTY(double Volume READ one)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canNext)
    Q_PROPERTY(bool CanGoPrevious READ canPrev)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ yes)
public:
    explicit MprisPlayer(Mpris *m) : QDBusAbstractAdaptor(m), m_m(m) {}
    QString status() const;
    double one() const { return 1.0; }
    bool yes() const { return true; }
    QVariantMap metadata() const;
    qlonglong position() const;
    bool canNext() const;
    bool canPrev() const;
    bool canSeek() const;
public Q_SLOTS:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &, qlonglong pos);
    void OpenUri(const QString &) {}
Q_SIGNALS:
    void Seeked(qlonglong pos);
private:
    Mpris *m_m;
};
