#include "notifier.h"

#include <QDBusReply>

#include "config.h"

#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>

Notifier &Notifier::get() {
    static Notifier n;
    return n;
}

Notifier::Notifier() {
    auto bus = QDBusConnection::sessionBus();
    bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                "ActionInvoked", this, SLOT(onAction(uint, QString)));
    bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                "NotificationClosed", this, SLOT(onClosed(uint, uint)));
}

QString Notifier::cacheImage(const QByteArray &bytes, const QString &ext) {
    const QString dir = Config::dataDir() + "/icons";
    QDir().mkpath(dir);
    const QString path = dir + "/" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16) + "." + ext;
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(bytes);
    }
    return path;
}

void Notifier::notify(const QString &summary, const QString &body, const Opts &o, Handler h, std::function<void(uint)> done) {
    QStringList actions;
    for (auto &[k, label] : o.actions) actions << k << label;
    QVariantMap hints{{"urgency", QVariant::fromValue<uchar>(o.urgency)}, {"desktop-entry", "com.cyberwolf.Tandem"}};
    if (!o.image.isEmpty()) hints["image-path"] = o.image;
    if (!o.category.isEmpty()) hints["category"] = o.category;
    if (o.resident) hints["resident"] = true;
    auto msg = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "Notify");
    // Phone content is untrusted: never let it inject links or images into the desktop popup.
    static const bool markup = [] {
        auto c = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                                "org.freedesktop.Notifications", "GetCapabilities");
        const QDBusReply<QStringList> r = QDBusConnection::sessionBus().call(c, QDBus::Block, 1000);
        return !r.isValid() || r.value().contains("body-markup");
    }();
    msg << QString("Tandem") << o.replaces << QString("phone") << summary << (markup ? body.toHtmlEscaped() : body) << actions << hints << o.timeout;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, h, done] {
        QDBusPendingReply<uint> r = *w;
        w->deleteLater();
        if (r.isError()) return;
        if (h) m_handlers.insert(r.value(), h);
        if (done) done(r.value());
    });
}

void Notifier::close(uint id) {
    if (!id) return;
    m_handlers.remove(id);
    auto msg = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                              "org.freedesktop.Notifications", "CloseNotification");
    msg << id;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void Notifier::onAction(uint id, const QString &key) {
    if (auto it = m_handlers.constFind(id); it != m_handlers.constEnd()) (*it)(key);
}

void Notifier::onClosed(uint id, uint) { m_handlers.remove(id); }
