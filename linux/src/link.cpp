#include "link.h"
#include "bluetooth.h"
#include <memory>
#include <QHostAddress>
#include <QTcpSocket>
#include <QNetworkInterface>

#include "config.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlEngine>
#include <QSslConfiguration>
#include <QSslKey>
#include <QtEndian>

static const quint16 kPhonePort = 47900;
static const quint16 kLocalPort = 47901;
static const char *kPkg = "com.cyberwolf.tandem";

static QString sha256Hex(const QByteArray &b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); }

static QString pairCodeFor(const QString &phoneFp, const QString &pcFp) {
    const QByteArray d = QCryptographicHash::hash((phoneFp + pcFp).toUtf8(), QCryptographicHash::Sha256);
    const quint32 n = qFromBigEndian<quint32>(d.constData());
    return QString("%1").arg(n % 1000000, 6, 10, QChar('0'));
}

Link::Link(QObject *parent) : QObject(parent) {
    Config::get().ensureIdentity();
    QFile f(Config::certPath());
    if (f.open(QIODevice::ReadOnly)) m_myFp = sha256Hex(QSslCertificate(f.readAll(), QSsl::Pem).toDer());
    m_phoneName = Config::get().value("phone_name").toString();

    m_retry.setSingleShot(true);
    m_retry.setInterval(3000);
    connect(&m_retry, &QTimer::timeout, this, &Link::startDiscovery);
    m_ping.setInterval(20000);
    connect(&m_ping, &QTimer::timeout, this, [this] { send({{"t", "req"}, {"id", 0}, {"m", "ping"}, {"a", QJsonObject{}}}); });
    m_connectTimeout.setSingleShot(true);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] {
        if (m_sock) {
            m_sock->abort();
            onDisconnected();
        }
    });
    // While linked without the helper (e.g. over Wi-Fi after the phone restarted), watch for USB and start it.
    m_usbPoll.setInterval(5000);
    connect(&m_usbPoll, &QTimer::timeout, this, &Link::pollUsb);
    m_usbPoll.start();
    m_sleeping = Config::get().value("sleeping", false).toBool();
    if (m_sleeping) m_state = "sleeping";
    else QTimer::singleShot(0, this, &Link::startDiscovery);
}

void Link::pollUsb() {
    if (m_sleeping || !online() || m_helper) return;
    auto *adb = new QProcess(this);
    connect(adb, &QProcess::finished, this, [this, adb] {
        adb->deleteLater();
        const auto lines = QString::fromUtf8(adb->readAllStandardOutput()).split('\n');
        QString serial;
        for (int i = 1; i < lines.size(); ++i) {
            const auto cols = lines[i].simplified().split(' ');
            if (cols.size() >= 2 && cols[1] == "device") { serial = cols[0]; break; }
        }
        if (serial.isEmpty() || !online() || m_helper) return;
        if (serial != m_serial) { m_serial = serial; Q_EMIT stateChanged(); }
        ensureHelper();
    });
    adb->start(adbBin(), {"devices"});
}

void Link::setSleeping(bool on) {
    if (on == m_sleeping) return;
    m_sleeping = on;
    Config::get().setValue("sleeping", on);
    if (on) {
        m_retry.stop();
        m_ping.stop();
        m_targets.clear();
        m_discovering = false;
        if (m_sock) { m_sock->disconnect(this); m_sock->abort(); m_sock->deleteLater(); m_sock = nullptr; }
        failAll();
        m_helper = false;
        m_transport.clear();
        setState("sleeping");
    } else {
        setState("offline");
        startDiscovery();
    }
}

QString Link::phoneFingerprint() const { return Config::get().value("phone_fp").toString(); }

void Link::setState(const QString &s) {
    m_state = s;
    Q_EMIT stateChanged();
}

// ---- discovery ----

QString Link::defaultGateway() {
    return gatewayForInterface("");
}

QString Link::gatewayForInterface(const QString &ifname) {
    QFile f("/proc/net/route");
    if (!f.open(QIODevice::ReadOnly)) return {};
    quint32 subnet = 0;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const auto cols = line.simplified().split(' ');
        if (cols.size() > 2) {
            const bool matchIf = ifname.isEmpty() || cols[0] == ifname.toUtf8();
            if (matchIf && cols[1] == "00000000") {
                const quint32 gw = cols[2].toUInt(nullptr, 16);
                if (gw != 0)
                    return QString("%1.%2.%3.%4").arg(gw & 0xff).arg((gw >> 8) & 0xff).arg((gw >> 16) & 0xff).arg(gw >> 24);
            } else if (matchIf && subnet == 0) {
                subnet = cols[1].toUInt(nullptr, 16);
            }
        }
    }
    if (!ifname.isEmpty() && subnet != 0) {
        return QString("%1.%2.%3.1").arg(subnet & 0xff).arg((subnet >> 8) & 0xff).arg((subnet >> 16) & 0xff);
    }
    return {};
}

void Link::startDiscovery() {
    if (m_discovering || m_sock || m_sleeping) return;
    m_discovering = true;
    m_targets.clear();

    // mDNS runs alongside; its hosts are appended if we're still looking when it answers.
    auto *avahi = new QProcess(this);
    connect(avahi, &QProcess::finished, this, [this, avahi] {
        for (const QByteArray &line : avahi->readAllStandardOutput().split('\n')) {
            const auto f = line.split(';');
            if (f.size() > 8 && f[0] == "=" && f[2] == "IPv4") {
                const QString h = QString::fromUtf8(f[7]);
                bool known = false;
                for (auto &t : m_targets) known |= std::get<0>(t) == h;
                if (!known && m_discovering) m_targets.append({h, kPhonePort, "wifi"});
            }
        }
        avahi->deleteLater();
    });
    avahi->start("avahi-browse", {"-rpt", "_tandem._tcp"});
    QTimer::singleShot(4000, avahi, [avahi] { avahi->kill(); });

    auto *adb = new QProcess(this);
    connect(adb, &QProcess::finished, this, [this, adb] {
        QString serial;
        const auto lines = QString::fromUtf8(adb->readAllStandardOutput()).split('\n');
        for (int i = 1; i < lines.size(); ++i) {
            const auto cols = lines[i].simplified().split(' ');
            if (cols.size() >= 2 && cols[1] == "device") { serial = cols[0]; break; }
        }
        adb->deleteLater();
        if (serial != m_serial) { m_serial = serial; Q_EMIT stateChanged(); }
        auto wifiTargets = [this] {
            const QString last = Config::get().value("last_host").toString();
            const QString gw = defaultGateway();
            if (!last.isEmpty()) m_targets.append({last, kPhonePort, "wifi"});
            if (!gw.isEmpty() && gw != last) m_targets.append({gw, kPhonePort, "wifi"});

            const QString bnepGw = gatewayForInterface("bnep0");
            if (!bnepGw.isEmpty()) m_targets.append({bnepGw, kPhonePort, "bluetooth"});

            tryNext();
        };
        if (serial.isEmpty()) return wifiTargets();
        auto *fwd = new QProcess(this);
        connect(fwd, &QProcess::finished, this, [this, fwd, wifiTargets](int code) {
            fwd->deleteLater();
            if (code == 0) m_targets.prepend({"127.0.0.1", kLocalPort, "usb"});
            if (code != 0 || (!Config::get().value("phone_fp").toString().isEmpty() && !m_rePair && m_state != "locked")) return wifiTargets();
            // Not paired yet: open the phone's pairing window over adb (only the shell user may), then connect.
            auto *win = new QProcess(this);
            connect(win, &QProcess::finished, this, [win, wifiTargets] { win->deleteLater(); wifiTargets(); });
            win->start(adbBin(), {"-s", m_serial, "shell", "am", "broadcast", "-n", "com.cyberwolf.tandem/.link.PairWindowReceiver"});
        });
        fwd->start(adbBin(), {"-s", serial, "forward", QString("tcp:%1").arg(kLocalPort), QString("tcp:%1").arg(kPhonePort)});
    });
    connect(adb, &QProcess::errorOccurred, this, [this, adb](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        adb->deleteLater();
        const QString last = Config::get().value("last_host").toString();
        if (!last.isEmpty()) m_targets.append({last, kPhonePort, "wifi"});
        tryNext();
    });
    adb->start(adbBin(), {"devices"});
}

// mDNS is unreliable on phones (and absent on their hotspot), so when nothing else answers we probe the local
// /24 for the Tandem port. Whatever answers still has to present the pinned certificate, so this can't be spoofed.
void Link::sweep(std::function<void(QStringList)> done) {
    QStringList hosts;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        const auto f = iface.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &e : iface.addressEntries()) {
            const QHostAddress ip = e.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || e.prefixLength() < 16) continue;
            const quint32 me = ip.toIPv4Address(), base = me & 0xffffff00u;
            for (quint32 h = 1; h < 255; ++h)
                if (base + h != me) hosts << QHostAddress(base + h).toString();
        }
    }
    if (hosts.isEmpty()) return done({});
    auto found = std::make_shared<QStringList>();
    auto left = std::make_shared<int>(hosts.size());
    for (const QString &h : hosts) {
        auto *sock = new QTcpSocket(this);
        auto finish = [sock, found, left, done, h](bool ok) {
            if (sock->property("done").toBool()) return;
            sock->setProperty("done", true);
            if (ok) *found << h;
            sock->abort();
            sock->deleteLater();
            if (--*left == 0) done(*found);
        };
        connect(sock, &QTcpSocket::connected, sock, [finish] { finish(true); });
        connect(sock, &QTcpSocket::errorOccurred, sock, [finish] { finish(false); });
        QTimer::singleShot(600, sock, [finish] { finish(false); });
        sock->connectToHost(h, kPhonePort);
    }
}

void Link::tryNext() {
    if (m_sock || m_sleeping) return;
    if (m_targets.isEmpty() && (!m_lastSweep.isValid() || m_lastSweep.elapsed() > 20000)) {
        m_lastSweep.start();
        sweep([this](const QStringList &hosts) {
            if (m_sock) return;
            for (const QString &h : hosts) m_targets.append({h, kPhonePort, "wifi"});
            tryNext();
        });
        return;
    }
    if (m_targets.isEmpty()) {
        m_discovering = false;
        if (m_bt && m_bt->isConnected()) {
            if (m_state != "bluetooth") setState("bluetooth");
            if (!m_bt->panConnected()) m_bt->connectPan();
        } else if (m_state != "mismatch" && m_state != "offline" && m_state != "locked") {
            setState("offline");
        }
        m_retry.start();
        return;
    }
    auto [host, port, transport] = m_targets.takeFirst();
    connectTo(host, port, transport);
}

void Link::connectTo(const QString &host, quint16 port, const QString &transport) {
    m_tryHost = host;
    m_tryTransport = transport;
    m_gotHello = false;
    m_buf.clear();
    m_sock = new QSslSocket(this);
    QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
    QFile cf(Config::certPath()), kf(Config::keyPath());
    if (cf.open(QIODevice::ReadOnly)) conf.setLocalCertificate(QSslCertificate(cf.readAll(), QSsl::Pem));
    if (kf.open(QIODevice::ReadOnly)) conf.setPrivateKey(QSslKey(kf.readAll(), QSsl::Ec, QSsl::Pem));
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);  // authenticity comes from fingerprint pinning
    conf.setProtocol(QSsl::TlsV1_3OrLater);
    m_sock->setSslConfiguration(conf);
    connect(m_sock, &QSslSocket::sslErrors, m_sock, [this](const QList<QSslError> &) { m_sock->ignoreSslErrors(); });
    connect(m_sock, &QSslSocket::encrypted, this, &Link::onEncrypted);
    connect(m_sock, &QSslSocket::readyRead, this, &Link::onReadyRead);
    connect(m_sock, &QSslSocket::disconnected, this, &Link::onDisconnected);
    connect(m_sock, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_sock && m_sock->state() != QAbstractSocket::ConnectedState) onDisconnected();
    });
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_sock->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    m_connectTimeout.start(transport == "usb" ? 5000 : 2500);
    m_sock->connectToHostEncrypted(host, port, host);
}

void Link::onEncrypted() {
    m_connectTimeout.stop();
    m_peerFp = sha256Hex(m_sock->peerCertificate().toDer());
    const QString pinned = Config::get().value("phone_fp").toString();
    if (!pinned.isEmpty() && pinned != m_peerFp) {
        m_discovering = false;
        m_targets.clear();
        setState("mismatch");
        m_sock->abort();
        return;
    }
    m_discovering = false;
    m_targets.clear();
    const bool needPair = pinned.isEmpty() || m_rePair || m_state == "locked" || m_state == "pairing";
    send({{"t", "hello"}, {"v", 1}, {"name", QHostInfo::localHostName()}, {"pair", needPair}});
}

void Link::onReadyRead() {
    m_buf += m_sock->readAll();
    while (m_buf.size() >= 4) {
        const quint32 n = qFromBigEndian<quint32>(m_buf.constData());
        if (n > (24u << 20)) { m_sock->abort(); return; }
        if (quint32(m_buf.size()) < 4 + n) break;
        const QJsonObject f = QJsonDocument::fromJson(m_buf.mid(4, n)).object();
        m_buf.remove(0, 4 + n);
        handle(f);
        if (!m_sock) return;
    }
}

void Link::handle(const QJsonObject &f) {
    const QString t = f.value("t").toString();
    if (!m_gotHello) {
        if (t != "hello") return;
        m_gotHello = true;
        m_phoneName = f.value("name").toString();
        m_transport = m_tryTransport;
        m_host = m_tryHost;
        m_ping.start();
        if (f.value("trusted").toBool() && !Config::get().value("phone_fp").toString().isEmpty()) {
            m_rePair = false;
            goOnline();
        } else {
            m_rePair = true;
            m_pairCode = pairCodeFor(m_peerFp, m_myFp);
            setState("pairing");
            if (!m_serial.isEmpty()) {
                auto *win = new QProcess(this);
                connect(win, &QProcess::finished, win, &QProcess::deleteLater);
                win->start(adbBin(), {"-s", m_serial, "shell", "am", "broadcast", "-n", "com.cyberwolf.tandem/.link.PairWindowReceiver"});
            }
        }
        return;
    }
    if (t == "res") {
        const qint64 id = f.value("id").toInteger();
        if (auto cb = m_pending.take(id)) {
            if (f.value("ok").toBool()) cb(true, f.value("r"));
            else cb(false, f.value("err"));
        }
    } else if (t == "ev") {
        const QString e = f.value("e").toString();
        if (e == "pair.accepted") {
            Config::get().setValue("phone_fp", m_peerFp);
            m_rePair = false;
            goOnline();
        } else if (e == "pair.closed") {
            setState("locked");
        } else if (e == "pair.rejected") {
            m_pairCode.clear();
            m_rePair = false;
            setState("offline");
        } else {
            Q_EMIT event(e, f.value("d"));
        }
    }
}

void Link::goOnline() {
    m_pairCode.clear();
    m_rePair = false;
    Config::get().setValue("phone_name", m_phoneName);
    if (m_transport == "wifi") Config::get().setValue("last_host", m_host);
    m_helper = false;
    setState("online");
    Q_EMIT cameOnline();
    ensureHelper();
}

void Link::onDisconnected() {
    if (!m_sock) return;
    m_connectTimeout.stop();
    m_sock->disconnect(this);
    m_sock->deleteLater();
    m_sock = nullptr;
    m_ping.stop();
    failAll();
    if (m_discovering) {
        tryNext();
        return;
    }
    m_helper = false;
    m_transport.clear();
    if (m_sleeping) return;
    if (m_state != "mismatch" && m_state != "locked") {
        if (m_bt && m_bt->isConnected()) {
            setState("bluetooth");
            if (!m_bt->panConnected()) m_bt->connectPan();
        } else {
            setState("offline");
        }
    }
    m_retry.start();
}

void Link::failAll() {
    auto pending = std::exchange(m_pending, {});
    for (auto &cb : pending)
        if (cb) cb(false, QJsonValue("disconnected"));
}

bool Link::send(const QJsonObject &obj) {
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return false;
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray frame(4, 0);
    qToBigEndian<quint32>(body.size(), frame.data());
    m_sock->write(frame + body);
    return true;
}

void Link::request(const QString &method, const QJsonObject &args, Callback cb) {
    if (!online()) {
        if (cb) QTimer::singleShot(0, this, [cb] { cb(false, QJsonValue("Phone not connected")); });
        return;
    }
    const qint64 id = m_nextId++;
    if (cb) m_pending.insert(id, std::move(cb));
    send({{"t", "req"}, {"id", id}, {"m", method}, {"a", args}});
}

void Link::call(const QString &method, const QVariantMap &args, const QJSValue &cb) {
    QJSValue callback = cb;
    request(method, QJsonObject::fromVariantMap(args), [this, callback](bool ok, const QJsonValue &r) mutable {
        if (callback.isCallable() && m_engine)
            callback.call({QJSValue(ok), m_engine->toScriptValue(r.toVariant())});
    });
}

void Link::reconnect() {
    m_discovering = false;
    m_targets.clear();
    if (m_sock) {
        m_sock->abort();
        onDisconnected();
    } else {
        startDiscovery();
    }
}

void Link::forget() {
    Config::get().setValue("phone_fp", QVariant());
    Config::get().setValue("phone_name", QVariant());
    m_rePair = true;
    m_pairCode.clear();
    m_state = "offline";
    reconnect();
}

void Link::ensureHelper() {
    request("shell.info", {}, [this](bool ok, const QJsonValue &r) {
        if (!ok) return;
        const auto info = r.toObject();
        const QString version = info.value("version").toString();
        if (info.value("running").toString() == version) {
            m_helper = true;
            Q_EMIT stateChanged();
            return;
        }
        if (m_serial.isEmpty()) return;
        const QString inner = QString("CLASSPATH='%1' setsid app_process /system/bin --nice-name=tandem-shell "
                                      "com.cyberwolf.tandem.shell.Main %2 %3 </dev/null >/dev/null 2>&1 &")
                                  .arg(info.value("apk").toString(), info.value("token").toString(), version);
        auto *p = new QProcess(this);
        connect(p, &QProcess::finished, this, [this, p] {
            p->deleteLater();
            QTimer::singleShot(1500, this, [this] {
                request("shell.info", {}, [this](bool ok, const QJsonValue &r) {
                    m_helper = ok && r.toObject().value("running").toString() == r.toObject().value("version").toString();
                    Q_EMIT stateChanged();
                });
            });
        });
        p->start(adbBin(), {"-s", m_serial, "shell", "(" + inner + ")"});
    });
    Q_UNUSED(kPkg);
}
