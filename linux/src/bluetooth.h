#pragma once
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QObject>
#include <QTimer>

// BlueZ for call audio: the PC acts as the phone's hands-free headset (PipeWire provides HFP HF).
// Pairing registers a one-shot agent and asks the phone to bond; both screens show the passkey.
class Bluetooth : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString phoneState READ phoneState NOTIFY changed)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY changed)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(bool panConnected READ panConnected NOTIFY panChanged)
    Q_PROPERTY(QString passkey READ passkey NOTIFY passkeyChanged)
    Q_PROPERTY(bool pairing READ pairing NOTIFY passkeyChanged)
public:
    explicit Bluetooth(QObject *parent = nullptr);
    bool available() const { return !m_adapter.isEmpty(); }
    Q_INVOKABLE QString address() const;
    QString phoneState() const;  // "unpaired" | "paired" | "connected"
    bool isConnected() const { return phoneState() == "connected"; }
    int batteryPercent() const;
    bool panConnected() const;
    QString passkey() const { return m_passkey; }
    bool pairing() const { return m_agentRegistered; }

    void connectPhone(const QString &addr);
    Q_INVOKABLE void setMediaAudio(bool on);
    Q_INVOKABLE void connectPan();
    Q_INVOKABLE void disconnectPan();
    Q_INVOKABLE bool beginPairing(int seconds = 120);
    Q_INVOKABLE void endPairing();
    void agentConfirm(const QDBusObjectPath &dev, uint passkey);

Q_SIGNALS:
    void changed();
    void passkeyChanged();
    void paired(const QString &addr);
    void batteryChanged(int percent);
    void panChanged(bool connected);

private Q_SLOTS:
    void onInterfacesAdded(const QDBusMessage &m);
    void enforceMedia();
    void onPropertiesChanged();

private:
    QString devicePath(const QString &addr) const;
    QVariantMap deviceProps(const QString &addr) const;
    void setAdapter(const QString &prop, const QVariant &v);
    QString m_adapter;
    QString m_passkey;
    bool m_agentRegistered = false;
    QTimer m_pairTimeout;
    QObject *m_agent = nullptr;
};
