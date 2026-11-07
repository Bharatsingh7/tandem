#pragma once
#include "listmodel.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>

class Link;
class Contacts;
class Bluetooth;

class Calls : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList calls READ calls NOTIFY callsChanged)
    Q_PROPERTY(QVariantMap audio READ audio NOTIFY callsChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(bool inCall READ inCall NOTIFY callsChanged)
    Q_PROPERTY(bool justEnded READ justEnded NOTIFY callsChanged)
    Q_PROPERTY(ListModel *log READ log CONSTANT)
    Q_PROPERTY(QString logFilter READ logFilter WRITE setLogFilter NOTIFY logFilterChanged)
public:
    Calls(Link *link, Contacts *contacts, Bluetooth *bt, QObject *parent = nullptr);
    ~Calls() override;
    QVariantList calls() const { return m_calls; }
    QVariantMap audio() const { return m_audio; }
    QVariantList accounts() const { return m_accounts; }
    bool inCall() const { return !m_calls.isEmpty(); }
    bool justEnded() const { return m_justEnded; }
    ListModel *log() { return &m_log; }
    QString logFilter() const { return m_filter; }
    void setLogFilter(const QString &f);

    Q_INVOKABLE void answer(int id = 0);
    Q_INVOKABLE void reject(int id = 0, const QString &text = {});
    Q_INVOKABLE void end(int id = 0);
    Q_INVOKABLE void hold(int id, bool on);
    Q_INVOKABLE void swap();
    Q_INVOKABLE void merge(int id);
    Q_INVOKABLE void mute(bool on);
    Q_INVOKABLE void dtmf(const QString &digits, int id = 0);
    Q_INVOKABLE void route(const QString &to, int id = 0);
    Q_INVOKABLE void dial(const QString &number, const QString &account = {});
    Q_INVOKABLE void deleteLog(qint64 id);
    Q_INVOKABLE QString lastDialed() const;
    void pushPrefs();

Q_SIGNALS:
    void callsChanged();
    void accountsChanged();
    void logFilterChanged();
    void ringing();
    void toast(const QString &text);
    void showDialer(const QString &number);
    void showSms(const QString &number);

private Q_SLOTS:
    void onBtCallAdded(const QDBusObjectPath &path, const QVariantMap &props);
    void onBtCallRemoved(const QDBusObjectPath &path);
    void onBtPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
    void onTelephonyInterfacesAdded(const QDBusMessage &m);
    void onTelephonyInterfacesRemoved(const QDBusMessage &m);

private:
    void initTelephony();
    void discoverTelephonyAg();
    void checkBtCalls();
    void addBtCall(const QString &path, const QVariantMap &props);
    void syncBtCalls();
    void bridgeAudio(bool on);
    void findPhoneNodes(int attempt);
    void update(const QJsonObject &d);
    void refreshLog();
    void reloadLog();
    void ringNotify(const QVariantMap &c);
    void missed(const QJsonObject &c);
    void startRingtone();
    void stopRingtone();
    bool ensurePcAudio();
    QVariantMap enrich(const QJsonObject &c) const;
    void pauseDesktop();
    void resumeDesktop();
    void err(bool ok, const QJsonValue &r);

    Link *m_link;
    Contacts *m_contacts;
    Bluetooth *m_bt;
    QVariantList m_calls, m_accounts;
    QVariantMap m_audio;
    ListModel m_log;
    QString m_filter = "all";
    bool m_justEnded = false;
    uint m_ringId = 0;
    QStringList m_paused;
    QString m_telephonyAgPath;
    QMap<QString, QVariantMap> m_btCalls;
    // pw-loopback pair linking the phone's Bluetooth call nodes to this PC's speakers and mic.
    QPointer<QProcess> m_voice, m_mic, m_ringer;
    bool m_bridgeWanted = false;
};
