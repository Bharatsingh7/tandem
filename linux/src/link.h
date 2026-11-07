#pragma once
#include <QJSValue>
#include <QJsonObject>
#include <QJsonValue>
#include <QElapsedTimer>
#include <QObject>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <functional>

class QQmlEngine;
class Bluetooth;

// TLS link to the phone. Same wire format as the Android side (link/Frames.kt):
// 4-byte big-endian length + UTF-8 JSON. Connects over USB (adb forward) first, then Wi-Fi.
class Link : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool online READ online NOTIFY stateChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY stateChanged)
    Q_PROPERTY(QString phoneName READ phoneName NOTIFY stateChanged)
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY stateChanged)
    Q_PROPERTY(QString serial READ serial NOTIFY stateChanged)
    Q_PROPERTY(QString host READ host NOTIFY stateChanged)
    Q_PROPERTY(bool helper READ helper NOTIFY stateChanged)
    Q_PROPERTY(bool sleeping READ sleeping NOTIFY stateChanged)
    Q_PROPERTY(QString phoneFingerprint READ phoneFingerprint NOTIFY stateChanged)
public:
    using Callback = std::function<void(bool ok, const QJsonValue &result)>;
    explicit Link(QObject *parent = nullptr);
    void setEngine(QQmlEngine *e) { m_engine = e; }

    QString state() const { return m_state; }
    bool online() const { return m_state == "online"; }
    QString transport() const { return m_transport; }
    QString phoneName() const { return m_phoneName; }
    QString pairCode() const { return m_pairCode; }
    QString serial() const { return m_serial; }
    QString host() const { return m_host; }
    bool helper() const { return m_helper; }
    QString phoneFingerprint() const;

    void request(const QString &method, const QJsonObject &args = {}, Callback cb = {});
    Q_INVOKABLE void call(const QString &method, const QVariantMap &args = {}, const QJSValue &cb = {});
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void forget();
    Q_INVOKABLE void ensureHelper();
    bool sleeping() const { return m_sleeping; }
    // Sleep: drop the phone, stop discovery and reconnects until woken. Remembered across restarts.
    Q_INVOKABLE void setSleeping(bool on);
    static QString defaultGateway();
    static QString gatewayForInterface(const QString &ifname);
    void setBluetooth(Bluetooth *bt) { m_bt = bt; }
    Bluetooth *bluetooth() const { return m_bt; }

Q_SIGNALS:
    void stateChanged();
    void event(const QString &name, const QJsonValue &data);
    void cameOnline();

private:
    void setState(const QString &s);
    void startDiscovery();
    void tryNext();
    void connectTo(const QString &host, quint16 port, const QString &transport);
    void onEncrypted();
    void onReadyRead();
    void onDisconnected();
    void handle(const QJsonObject &f);
    bool send(const QJsonObject &obj);
    void goOnline();
    void failAll();

    QQmlEngine *m_engine = nullptr;
    QSslSocket *m_sock = nullptr;
    QByteArray m_buf;
    QHash<qint64, Callback> m_pending;
    qint64 m_nextId = 1;
    QTimer m_retry, m_ping, m_connectTimeout, m_usbPoll;
    bool m_sleeping = false;
    void pollUsb();
    QList<std::tuple<QString, quint16, QString>> m_targets;
    bool m_discovering = false;
    QElapsedTimer m_lastSweep;
    void sweep(std::function<void(QStringList)> done);
    bool m_gotHello = false;
    QString m_state = "offline", m_transport, m_phoneName, m_pairCode, m_serial, m_host, m_peerFp, m_myFp;
    QString m_tryTransport, m_tryHost;
    bool m_helper = false;
    bool m_rePair = false;
    Bluetooth *m_bt = nullptr;
};
