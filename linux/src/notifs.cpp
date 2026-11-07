#include "notifs.h"

#include "config.h"
#include "images.h"
#include "link.h"
#include "notifier.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QTimer>

static const QSet<QString> kSkip = {"call", "transport", "progress", "service", "sys", "navigation", "stopwatch"};

static QString hashKey(const QString &k) { return QCryptographicHash::hash(k.toUtf8(), QCryptographicHash::Md5).toHex().left(12); }

Notifs::Notifs(Link *link, QObject *parent)
    : QObject(parent), m_link(link),
      m_model({"key", "pkg", "app", "title", "text", "time", "clearable", "actions", "canReply", "lines", "avatar", "icon",
               "progress"}) {
    connect(link, &Link::cameOnline, this, [this] {
        m_link->request("notif.list", {}, [this](bool ok, const QJsonValue &r) {
            if (!ok) return;
            m_items.clear();
            for (const auto &v : r.toArray()) {
                const auto n = v.toObject();
                m_items.insert(n["key"].toString(), n);
                wantIcon(n["pkg"].toString());
            }
            rebuild();
        });
    });
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &d) {
        const auto n = d.toObject();
        if (e == "notif.posted") {
            const QString key = n["key"].toString();
            const auto old = m_items.value(key);
            const bool fresh = old.isEmpty() || old["title"] != n["title"] || old["text"] != n["text"] ||
                               old["conversation"].toObject()["messages"].toArray().size() !=
                                   n["conversation"].toObject()["messages"].toArray().size();
            m_items.insert(key, n);
            wantIcon(n["pkg"].toString());
            Q_EMIT posted(n);
            rebuild();
            if (fresh) popup(n, old.isEmpty());
        } else if (e == "notif.removed") {
            const QString key = n["key"].toString();
            m_items.remove(key);
            Notifier::get().close(m_popups.take(key));
            Q_EMIT removed(key);
            rebuild();
        }
    });
}

int Notifs::unread() const {
    int n = 0;
    for (const auto &i : m_items) n += i["clearable"].toBool() && !i["media"].toBool();
    return n;
}

void Notifs::wantIcon(const QString &pkg) {
    if (m_icons.contains(pkg)) return;
    m_icons.insert(pkg);
    m_link->request("notif.icon", {{"pkg", pkg}}, [this, pkg](bool ok, const QJsonValue &r) {
        if (!ok) return;
        Images::get().put("app/" + pkg, QByteArray::fromBase64(r.toObject()["png"].toString().toLatin1()));
        rebuild();
    });
}

QVariantMap Notifs::toRow(const QJsonObject &n) const {
    QVariantList actions;
    bool canReply = false;
    for (const auto &a : n["actions"].toArray()) {
        if (a.toObject()["reply"].toBool()) canReply = true;
        else actions << a.toObject().toVariantMap();
    }
    QStringList lines;
    const auto conv = n["conversation"].toObject();
    const auto msgs = conv["messages"].toArray();
    for (int i = qMax(0, int(msgs.size()) - 4); i < msgs.size(); ++i) {
        const auto m = msgs[i].toObject();
        const bool showSender = conv["group"].toBool() || m["self"].toBool();
        lines << (showSender ? (m["self"].toBool() ? QString("You") : m["sender"].toString()) + ": " : QString()) + m["text"].toString();
    }
    QString avatar;
    if (n.contains("avatar")) {
        avatar = "notif/" + hashKey(n["key"].toString());
        Images::get().put(avatar, QByteArray::fromBase64(n["avatar"].toString().toLatin1()));
    }
    const QString text = n["big"].toString().isEmpty() ? n["text"].toString() : n["big"].toString();
    return {{"key", n["key"]}, {"pkg", n["pkg"]}, {"app", n["app"]}, {"title", n["title"]},
            {"text", lines.isEmpty() ? text : lines.join('\n')}, {"time", n["time"]}, {"clearable", n["clearable"]},
            {"actions", actions}, {"canReply", canReply}, {"lines", lines.size()}, {"avatar", avatar},
            {"icon", Images::get().has("app/" + n["pkg"].toString()) ? "app/" + n["pkg"].toString() : QString()},
            {"progress", n["progress"].isObject() ? n["progress"].toObject()["v"].toDouble() / qMax(1.0, n["progress"].toObject()["max"].toDouble()) : -1.0}};
}

void Notifs::rebuild() {
    QList<QJsonObject> list;
    for (const auto &n : m_items) if (!n["media"].toBool()) list << n;
    std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) { return a["time"].toInteger() > b["time"].toInteger(); });
    QVector<QVariantMap> rows;
    for (const auto &n : list) rows << toRow(n);
    m_model.setItems(std::move(rows));
    Q_EMIT unreadChanged();
}

bool Notifs::isMuted(const QString &pkg) const { return Config::get().value("muted_apps").toStringList().contains(pkg); }

void Notifs::muteApp(const QString &pkg, bool mute) {
    auto list = Config::get().value("muted_apps").toStringList();
    list.removeAll(pkg);
    if (mute) list << pkg;
    Config::get().setValue("muted_apps", list);
}

void Notifs::popup(const QJsonObject &n, bool) {
    if (!Config::get().value("notify_mirror", true).toBool()) return;
    if (n["ongoing"].toBool() || n["media"].toBool() || n["progress"].isObject()) return;
    if (isMuted(n["pkg"].toString()) || kSkip.contains(n["category"].toString())) return;
    const auto conv = n["conversation"].toObject();
    if (!conv.isEmpty() && messagesVisible && messagesVisible()) return;
    QString title = n["title"].toString().isEmpty() ? n["app"].toString() : n["title"].toString();
    QString body = n["text"].toString().isEmpty() ? n["big"].toString() : n["text"].toString();
    const auto msgs = conv["messages"].toArray();
    if (!msgs.isEmpty()) {
        const auto last = msgs.last().toObject();
        title = conv["title"].toString();
        body = conv["group"].toBool() ? last["sender"].toString() + ": " + last["text"].toString() : last["text"].toString();
    }
    Notifier::Opts o;
    o.actions.append(qMakePair(QString("default"), QString("Open")));
    bool canReply = false;
    for (const auto &a : n["actions"].toArray()) canReply |= a.toObject()["reply"].toBool();
    if (canReply) o.actions.append(qMakePair(QString("reply"), QString("Reply")));
    for (const auto &a : n["actions"].toArray()) {
        const auto ao = a.toObject();
        if (!ao["reply"].toBool() && o.actions.size() < 4) o.actions.append(qMakePair(QString("a%1").arg(ao["i"].toInt()), ao["title"].toString()));
    }
    if (n.contains("avatar")) o.image = Notifier::cacheImage(QByteArray::fromBase64(n["avatar"].toString().toLatin1()));
    else if (Images::get().has("app/" + n["pkg"].toString())) o.image = Notifier::cacheImage(Images::get().raw("app/" + n["pkg"].toString()));
    o.replaces = m_popups.value(n["key"].toString());
    if (!conv.isEmpty()) o.category = "im.received";
    const QString key = n["key"].toString();
    Notifier::get().notify(title, body.left(300), o,
                           [this, n, key](const QString &a) {
                               if (a == "default" || a == "reply") {
                                   if (!n["conversation"].toObject().isEmpty()) Q_EMIT openChat(n);
                                   else Q_EMIT openPage("notifications");
                               } else if (a.startsWith('a')) {
                                   action(key, a.mid(1).toInt());
                               }
                           },
                           [this, key](uint id) { m_popups.insert(key, id); });
}

void Notifs::dismiss(const QString &key) {
    m_link->request("notif.dismiss", key.isEmpty() ? QJsonObject{} : QJsonObject{{"key", key}});
    if (key.isEmpty()) {
        for (auto it = m_items.begin(); it != m_items.end();) it = it->value("clearable").toBool() ? m_items.erase(it) : std::next(it);
    } else {
        m_items.remove(key);
    }
    rebuild();
}

void Notifs::reply(const QString &key, const QString &text) {
    reply(key, text, [this](bool ok, const QString &err) { Q_EMIT toast(ok ? "Sent" : err); });
}

void Notifs::reply(const QString &key, const QString &text, std::function<void(bool, QString)> done) {
    m_link->request("notif.reply", {{"key", key}, {"text", text}},
                    [done](bool ok, const QJsonValue &r) { if (done) done(ok, r.toString()); });
}

void Notifs::action(const QString &key, int index) { m_link->request("notif.action", {{"key", key}, {"index", index}}); }
void Notifs::open(const QString &key) { m_link->request("notif.open", {{"key", key}}); }
