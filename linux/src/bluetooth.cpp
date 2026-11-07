#include "bluetooth.h"

#include <QDBusContext>

#include "config.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QProcess>

static const char *kBluez = "org.bluez";
static const char *kHfpAg = "0000111f-0000-1000-8000-00805f9b34fb";
static const char *kA2dpSource = "0000110a-0000-1000-8000-00805f9b34fb";
static const char *kAgentPath = "/com/cyberwolf/tandem/agent";

using ManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
Q_DECLARE_METATYPE(ManagedObjects)

static ManagedObjects managedObjects() {
    auto msg = QDBusMessage::createMethodCall(kBluez, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    QDBusReply<ManagedObjects> r = QDBusConnection::systemBus().call(msg, QDBus::Block, 2000);
    return r.isValid() ? r.value() : ManagedObjects{};
}

class Agent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    explicit Agent(Bluetooth *bt) : QObject(bt), m_bt(bt) {}
public Q_SLOTS:
    void Release() {}
    QString RequestPinCode(const QDBusObjectPath &) { return "0000"; }
    void DisplayPinCode(const QDBusObjectPath &, const QString &) {}
    uint RequestPasskey(const QDBusObjectPath &) { return 0; }
    void DisplayPasskey(const QDBusObjectPath &d, uint passkey, ushort) { if (check(d)) m_bt->agentConfirm(d, passkey); }
    void RequestConfirmation(const QDBusObjectPath &d, uint passkey) { if (check(d)) m_bt->agentConfirm(d, passkey); }
    void RequestAuthorization(const QDBusObjectPath &d) { check(d); }
    void AuthorizeService(const QDBusObjectPath &d, const QString &) { check(d); }
    void Cancel() {}
private:
    // Anyone nearby could try to pair while we're discoverable: reject devices that aren't the phone.
    bool check(const QDBusObjectPath &d) {
        const QString want = Config::get().value("phone_bt_name").toString();
        QDBusInterface dev("org.bluez", d.path(), "org.bluez.Device1", QDBusConnection::systemBus());
        const QString name = dev.property("Name").toString(), addr = dev.property("Address").toString();
        const QString known = Config::get().value("phone_bt").toString();
        if ((!known.isEmpty() && addr.compare(known, Qt::CaseInsensitive) == 0) || (!want.isEmpty() && name == want)) return true;
        sendErrorReply("org.bluez.Error.Rejected", "not the paired phone");
        return false;
    }
    Bluetooth *m_bt;
};

Bluetooth::Bluetooth(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<ManagedObjects>();
    const auto objs = managedObjects();
    for (auto it = objs.begin(); it != objs.end(); ++it)
        if (it.value().contains("org.bluez.Adapter1")) { m_adapter = it.key().path(); break; }
    QDBusConnection::systemBus().connect(kBluez, {}, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                         this, SLOT(onPropertiesChanged()));
    QDBusConnection::systemBus().connect(kBluez, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                                         this, SLOT(onInterfacesAdded(QDBusMessage)));
    QTimer::singleShot(1500, this, &Bluetooth::enforceMedia);
    m_pairTimeout.setSingleShot(true);
    connect(&m_pairTimeout, &QTimer::timeout, this, &Bluetooth::endPairing);
}

void Bluetooth::onPropertiesChanged() {
    Q_EMIT changed();
    Q_EMIT batteryChanged(batteryPercent());
    Q_EMIT panChanged(panConnected());
}

QString Bluetooth::address() const {
    if (m_adapter.isEmpty()) return {};
    QDBusInterface a(kBluez, m_adapter, "org.bluez.Adapter1", QDBusConnection::systemBus());
    return a.property("Address").toString();
}

QString Bluetooth::devicePath(const QString &addr) const {
    return m_adapter + "/dev_" + addr.toUpper().replace(':', '_');
}

QVariantMap Bluetooth::deviceProps(const QString &addr) const {
    if (addr.isEmpty() || m_adapter.isEmpty()) return {};
    const auto objs = managedObjects();
    return objs.value(QDBusObjectPath(devicePath(addr))).value("org.bluez.Device1");
}

QString Bluetooth::phoneState() const {
    const auto p = deviceProps(Config::get().value("phone_bt").toString());
    if (p.isEmpty() || !p.value("Paired").toBool()) return "unpaired";
    return p.value("Connected").toBool() ? "connected" : "paired";
}

int Bluetooth::batteryPercent() const {
    const QString addr = Config::get().value("phone_bt").toString();
    if (addr.isEmpty() || m_adapter.isEmpty()) return -1;
    const auto objs = managedObjects();
    const auto bat = objs.value(QDBusObjectPath(devicePath(addr))).value("org.bluez.Battery1");
    if (bat.contains("Percentage")) return bat.value("Percentage").toInt();
    return -1;
}

bool Bluetooth::panConnected() const {
    const QString addr = Config::get().value("phone_bt").toString();
    if (addr.isEmpty() || m_adapter.isEmpty()) return false;
    const auto objs = managedObjects();
    const auto net = objs.value(QDBusObjectPath(devicePath(addr))).value("org.bluez.Network1");
    return net.value("Connected").toBool();
}

void Bluetooth::connectPan() {
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, p, &QProcess::deleteLater);
    p->start("nmcli", {"con", "up", "id", "motorola edge 60 pro Network"});
}

void Bluetooth::disconnectPan() {
    const QString addr = Config::get().value("phone_bt").toString();
    if (addr.isEmpty() || m_adapter.isEmpty()) return;
    auto msg = QDBusMessage::createMethodCall(kBluez, devicePath(addr), "org.bluez.Network1", "Disconnect");
    QDBusConnection::systemBus().asyncCall(msg, 5000);
}

void Bluetooth::connectPhone(const QString &addr) {
    if (deviceProps(addr).isEmpty()) return;
    auto msg = QDBusMessage::createMethodCall(kBluez, devicePath(addr), "org.bluez.Device1", "ConnectProfile");
    msg << QString(kHfpAg);
    QDBusConnection::systemBus().asyncCall(msg, 20000);
}

void Bluetooth::setMediaAudio(bool on) {
    Config::get().setValue("phone_media_on_pc", on);
    const QString addr = Config::get().value("phone_bt").toString();
    if (deviceProps(addr).isEmpty()) return;
    auto msg = QDBusMessage::createMethodCall(kBluez, devicePath(addr), "org.bluez.Device1", on ? "ConnectProfile" : "DisconnectProfile");
    msg << QString(kA2dpSource);
    QDBusConnection::systemBus().asyncCall(msg, 15000);
}

// The phone is paired as a call headset, but Android also opens media audio (A2DP) to it and then plays
// everything here. Unless the user asked for phone media on the PC, drop A2DP whenever it appears; HFP stays.
void Bluetooth::onInterfacesAdded(const QDBusMessage &m) {
    const QString addr = Config::get().value("phone_bt").toString();
    if (addr.isEmpty()) return;
    const QString path = m.arguments().value(0).value<QDBusObjectPath>().path();
    if (path.startsWith(devicePath(addr) + "/")) QTimer::singleShot(300, this, &Bluetooth::enforceMedia);
}

void Bluetooth::enforceMedia() {
    const QString addr = Config::get().value("phone_bt").toString();
    if (addr.isEmpty() || Config::get().value("phone_media_on_pc").toBool()) return;
    const QString dev = devicePath(addr) + "/";
    const auto objs = managedObjects();
    for (auto it = objs.begin(); it != objs.end(); ++it) {
        if (!it.key().path().startsWith(dev)) continue;
        const QString uuid = it.value().value("org.bluez.MediaTransport1").value("UUID").toString().toLower();
        if (uuid.startsWith("0000110a") || uuid.startsWith("0000110b")) {
            auto msg = QDBusMessage::createMethodCall(kBluez, devicePath(addr), "org.bluez.Device1", "DisconnectProfile");
            msg << QString(kA2dpSource);
            QDBusConnection::systemBus().asyncCall(msg, 15000);
            return;
        }
    }
}

void Bluetooth::setAdapter(const QString &prop, const QVariant &v) {
    auto msg = QDBusMessage::createMethodCall(kBluez, m_adapter, "org.freedesktop.DBus.Properties", "Set");
    msg << QString("org.bluez.Adapter1") << prop << QVariant::fromValue(QDBusVariant(v));
    QDBusConnection::systemBus().call(msg, QDBus::Block, 2000);
}

bool Bluetooth::beginPairing(int seconds) {
    if (m_adapter.isEmpty()) return false;
    setAdapter("Powered", true);
    setAdapter("Pairable", true);
    setAdapter("DiscoverableTimeout", uint(seconds));
    setAdapter("Discoverable", true);
    if (!m_agentRegistered) {
        if (!m_agent) m_agent = new Agent(this);
        auto bus = QDBusConnection::systemBus();
        bus.registerObject(kAgentPath, m_agent, QDBusConnection::ExportAllSlots);
        QDBusInterface mgr(kBluez, "/org/bluez", "org.bluez.AgentManager1", bus);
        mgr.call("RegisterAgent", QVariant::fromValue(QDBusObjectPath(kAgentPath)), QString("DisplayYesNo"));
        mgr.call("RequestDefaultAgent", QVariant::fromValue(QDBusObjectPath(kAgentPath)));
        m_agentRegistered = true;
    }
    m_passkey.clear();
    Q_EMIT passkeyChanged();
    m_pairTimeout.start(seconds * 1000);
    return true;
}

void Bluetooth::endPairing() {
    m_pairTimeout.stop();
    if (m_agentRegistered) {
        auto bus = QDBusConnection::systemBus();
        QDBusInterface mgr(kBluez, "/org/bluez", "org.bluez.AgentManager1", bus);
        mgr.call("UnregisterAgent", QVariant::fromValue(QDBusObjectPath(kAgentPath)));
        bus.unregisterObject(kAgentPath);
        m_agentRegistered = false;
    }
    setAdapter("Discoverable", false);
    m_passkey.clear();
    Q_EMIT passkeyChanged();
}

void Bluetooth::agentConfirm(const QDBusObjectPath &dev, uint passkey) {
    m_passkey = QString("%1").arg(passkey, 6, 10, QChar('0'));
    Q_EMIT passkeyChanged();
    const QString path = dev.path();
    // Poll until bonding completes, then trust the phone and finish.
    auto *t = new QTimer(this);
    t->setInterval(1000);
    int *tries = new int(0);
    connect(t, &QTimer::timeout, this, [this, t, path, tries] {
        QDBusInterface d(kBluez, path, "org.bluez.Device1", QDBusConnection::systemBus());
        if (d.property("Paired").toBool()) {
            d.setProperty("Trusted", true);
            const QString addr = d.property("Address").toString();
            Config::get().setValue("phone_bt", addr);
            t->stop();
            t->deleteLater();
            delete tries;
            endPairing();
            Q_EMIT paired(addr);
            Q_EMIT changed();
        } else if (++*tries > 60) {
            t->stop();
            t->deleteLater();
            delete tries;
        }
    });
    t->start();
}

#include "bluetooth.moc"
