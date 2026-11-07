#include "messages.h"

#include <algorithm>

#include "contacts.h"
#include "db.h"
#include "images.h"
#include "link.h"
#include "notifs.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QTimer>

static QString imgKey(const QString &chat) { return "chat/" + QCryptographicHash::hash(chat.toUtf8(), QCryptographicHash::Md5).toHex().left(12); }

Messages::Messages(Link *link, Contacts *contacts, Notifs *notifs, QObject *parent)
    : QObject(parent), m_link(link), m_contacts(contacts), m_notifs(notifs),
      m_convs({"kind", "ref", "title", "snippet", "time", "unread", "app", "pkg", "avatar", "address"}),
      m_thread({"text", "time", "self", "sender", "status", "day"}) {
    auto q = Db::exec("SELECT key, avatar FROM chats WHERE avatar IS NOT NULL");
    while (q.next()) Images::get().put(imgKey(q.value(0).toString()), q.value(1).toByteArray());
    rebuild();
    connect(link, &Link::cameOnline, this, [this] {
        Db::exec("UPDATE chats SET notif_key=NULL");  // stale while we were away
        refreshSms();
    });
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &d) {
        if (e == "sms.changed") refreshSms();
        else if (e == "sms.received") QTimer::singleShot(700, this, &Messages::refreshSms);
        else if (e == "sms.sent" && !d.toObject()["ok"].toBool()) Q_EMIT toast("SMS failed to send");
    });
    connect(notifs, &Notifs::posted, this, [this](const QJsonObject &n) {
        if (!n["conversation"].toObject().isEmpty() && n["pkg"].toString() != m_notifs->smsPkg) ingest(n);
    });
    connect(notifs, &Notifs::removed, this, [this](const QString &key) {
        if (Db::scalar("SELECT COUNT(*) FROM chats WHERE notif_key=?", {key}).toInt()) {
            Db::exec("UPDATE chats SET notif_key=NULL, unread=0 WHERE notif_key=?", {key});
            rebuild();
            if (m_current["kind"] == "chat") { m_current["canReply"] = false; Q_EMIT currentChanged(); }
        }
    });
    connect(contacts, &Contacts::changed, this, &Messages::rebuild);
}

QString Messages::chatKey(const QJsonObject &n) {
    const QString shortcut = n["shortcut"].toString();
    return n["pkg"].toString() + "|" + (shortcut.isEmpty() ? n["conversation"].toObject()["title"].toString() : shortcut);
}

void Messages::ingest(const QJsonObject &n) {
    const auto conv = n["conversation"].toObject();
    const QString key = chatKey(n);
    int fresh = 0;
    Db::begin();
    for (const auto &v : conv["messages"].toArray()) {
        const auto m = v.toObject();
        QString text = m["text"].toString();
        if (text.isEmpty() && !m["media"].toString().isEmpty()) text = "[" + m["media"].toString().section('/', 0, 0) + "]";
        if (text.isEmpty()) continue;
        const bool self = m["self"].toBool();
        if (self && Db::scalar("SELECT 1 FROM chat_messages WHERE chat_key=? AND self=1 AND text=? AND ABS(time-?)<180000",
                               {key, text, m["time"].toInteger()}).isValid())
            continue;
        auto q = Db::exec("INSERT OR IGNORE INTO chat_messages VALUES(?,?,?,?,?)", {key, m["time"].toInteger(), m["sender"].toString(), text, int(self)});
        if (!self) fresh += q.numRowsAffected() > 0;
    }
    const auto msgs = conv["messages"].toArray();
    const auto last = msgs.isEmpty() ? QJsonObject{} : msgs.last().toObject();
    const bool group = conv["group"].toBool();
    QByteArray avatar;
    if (!group && n.contains("avatar")) {
        avatar = QByteArray::fromBase64(n["avatar"].toString().toLatin1());
        Images::get().put(imgKey(key), avatar);
    }
    const bool viewing = m_current["kind"] == "chat" && m_current["ref"] == key;
    Db::exec("INSERT INTO chats(key, pkg, app, title, is_group, last_time, last_text, unread, notif_key, avatar) VALUES(?,?,?,?,?,?,?,?,?,?) "
             "ON CONFLICT(key) DO UPDATE SET title=excluded.title, is_group=excluded.is_group, last_time=MAX(last_time, excluded.last_time), "
             "last_text=excluded.last_text, unread=unread+?, notif_key=excluded.notif_key, avatar=COALESCE(excluded.avatar, avatar), app=excluded.app",
             {key, n["pkg"].toString(), n["app"].toString(), conv["title"].toString(), int(group),
              last.isEmpty() ? n["time"].toInteger() : last["time"].toInteger(), last["text"].toString(), viewing ? 0 : fresh,
              n["key"].toString(), avatar.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(avatar), viewing ? 0 : fresh});
    Db::commit();
    rebuild();
    if (viewing) {
        m_current["canReply"] = true;
        Q_EMIT currentChanged();
        loadThread();
    }
}

void Messages::refreshSms() {
    m_link->request("sms.threads", {{"limit", 80}}, [this](bool ok, const QJsonValue &r) {
        if (!ok) return;
        m_threads = r.toArray();
        rebuild();
        if (m_current["kind"] == "sms" || m_current["kind"] == "new") {
            if (m_current["kind"] == "new") {
                for (const auto &t : m_threads)
                    if (Contacts::key(t.toObject()["address"].toString()) == Contacts::key(m_current["number"].toString())) {
                        m_current["kind"] = "sms";
                        m_current["ref"] = QString::number(t.toObject()["thread"].toInteger());
                        Q_EMIT currentChanged();
                    }
            }
            loadThread();
        }
    });
}

void Messages::rebuild() {
    QVector<QVariantMap> rows;
    int unread = 0;
    const QString s = m_search.trimmed().toLower();
    if (m_filter == "all" || m_filter == "sms") {
        for (const auto &v : m_threads) {
            const auto t = v.toObject();
            const QString addr = t["address"].toString();
            const auto hit = m_contacts->lookup(addr);
            QString title = hit.value("name").toString();
            if (title.isEmpty()) title = t["name"].toString().isEmpty() ? Contacts::pretty(addr) : t["name"].toString();
            rows << QVariantMap{{"kind", "sms"}, {"ref", QString::number(t["thread"].toInteger())}, {"title", title},
                                {"snippet", t["snippet"].toString().simplified()}, {"time", t["date"]}, {"unread", t["unread"]},
                                {"app", "SMS"}, {"pkg", "sms"}, {"address", addr},
                                {"avatar", hit.value("hasPhoto").toBool() ? "contact/" + hit["cid"].toString() : QString()}};
            unread += t["unread"].toInt();
        }
    }
    auto q = Db::exec("SELECT key, pkg, app, title, last_time, last_text, unread, avatar IS NOT NULL FROM chats ORDER BY last_time DESC");
    while (q.next()) {
        const QString pkg = q.value(1).toString();
        const bool wa = m_whatsapp.contains(pkg);
        const int u = q.value(6).toInt();
        if (m_filter == "all") unread += u;
        if (m_filter == "sms" || (m_filter == "wa" && !wa) || (m_filter == "other" && wa)) continue;
        if (m_filter != "all") unread += u;
        rows << QVariantMap{{"kind", "chat"}, {"ref", q.value(0)}, {"title", q.value(3)}, {"snippet", q.value(5).toString().simplified()},
                            {"time", q.value(4)}, {"unread", u}, {"app", q.value(2)}, {"pkg", pkg}, {"address", QString()},
                            {"avatar", q.value(7).toBool() ? imgKey(q.value(0).toString()) : QString()}};
    }
    if (!s.isEmpty())
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const QVariantMap &r) {
                       return !r["title"].toString().toLower().contains(s) && !r["snippet"].toString().toLower().contains(s);
                   }), rows.end());
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) { return a["time"].toLongLong() > b["time"].toLongLong(); });
    m_convs.setItems(std::move(rows));
    if (unread != m_unread) { m_unread = unread; Q_EMIT unreadChanged(); }
}

void Messages::setFilter(const QString &f) { if (f != m_filter) { m_filter = f; Q_EMIT filterChanged(); rebuild(); } }
void Messages::setSearch(const QString &s) { if (s != m_search) { m_search = s; Q_EMIT filterChanged(); rebuild(); } }

void Messages::open(const QString &kind, const QString &ref) {
    m_current = {{"kind", kind}, {"ref", ref}};
    if (kind == "sms") {
        for (const auto &v : m_threads) {
            const auto t = v.toObject();
            if (QString::number(t["thread"].toInteger()) != ref) continue;
            const QString addr = t["address"].toString();
            const auto hit = m_contacts->lookup(addr);
            const QString name = hit.value("name").toString().isEmpty() ? t["name"].toString() : hit["name"].toString();
            m_current["title"] = name.isEmpty() ? Contacts::pretty(addr) : name;
            m_current["subtitle"] = Contacts::pretty(addr) + " · SMS";
            m_current["number"] = addr;
            m_current["avatar"] = hit.value("hasPhoto").toBool() ? "contact/" + hit["cid"].toString() : QString();
            m_current["canReply"] = true;
        }
    } else {
        auto q = Db::exec("SELECT title, app, is_group, notif_key, avatar IS NOT NULL FROM chats WHERE key=?", {ref});
        if (!q.next()) return;
        m_current["title"] = q.value(0);
        m_current["subtitle"] = q.value(1).toString() + (q.value(2).toInt() ? " · group" : "");
        m_current["group"] = q.value(2).toBool();
        m_current["canReply"] = !q.value(3).toString().isEmpty();
        m_current["avatar"] = q.value(4).toBool() ? imgKey(ref) : QString();
        Db::exec("UPDATE chats SET unread=0 WHERE key=?", {ref});
        rebuild();
    }
    Q_EMIT currentChanged();
    loadThread();
}

void Messages::openNumber(const QString &number) {
    for (const auto &v : m_threads)
        if (Contacts::key(v.toObject()["address"].toString()) == Contacts::key(number))
            return open("sms", QString::number(v.toObject()["thread"].toInteger()));
    const auto hit = m_contacts->lookup(number);
    m_current = {{"kind", "new"}, {"ref", ""}, {"number", number}, {"canReply", true},
                 {"title", hit.value("name").toString().isEmpty() ? Contacts::pretty(number) : hit["name"].toString()},
                 {"subtitle", "New SMS"}, {"avatar", hit.value("hasPhoto").toBool() ? "contact/" + hit["cid"].toString() : QString()}};
    m_thread.setItems({});
    Q_EMIT currentChanged();
}

void Messages::openForNotification(const QJsonObject &n) {
    if (n["conversation"].toObject().isEmpty()) return;
    ingest(n);
    open("chat", chatKey(n));
}

void Messages::close() {
    m_current.clear();
    m_thread.setItems({});
    Q_EMIT currentChanged();
}

static QString dayOf(qint64 ms) {
    if (!ms) return {};
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date(), today = QDate::currentDate();
    if (d == today) return "Today";
    if (d == today.addDays(-1)) return "Yesterday";
    return d.toString(d.year() == today.year() ? "dddd, d MMMM" : "d MMMM yyyy");
}

void Messages::setThread(QVector<QVariantMap> msgs) {
    std::stable_sort(msgs.begin(), msgs.end(), [](const QVariantMap &a, const QVariantMap &b) { return a["time"].toLongLong() < b["time"].toLongLong(); });
    QString lastDay;
    for (auto &m : msgs) {
        const QString d = dayOf(m["time"].toLongLong());
        m["day"] = d != lastDay ? d : QString();
        lastDay = d;
    }
    m_thread.setItems(std::move(msgs));
}

void Messages::loadThread() {
    const QString kind = m_current["kind"].toString(), ref = m_current["ref"].toString();
    if (kind == "sms") {
        m_link->request("sms.messages", {{"thread", ref.toLongLong()}, {"limit", 300}}, [this, ref](bool ok, const QJsonValue &r) {
            if (!ok || m_current["ref"] != ref) return;
            QVector<QVariantMap> msgs;
            for (const auto &v : r.toArray()) {
                const auto m = v.toObject();
                const int type = m["type"].toInt();
                msgs << QVariantMap{{"text", m["body"]}, {"time", m["date"]}, {"self", type != 1}, {"sender", QString()},
                                    {"status", type == 5 ? "failed" : (type == 4 || type == 6) ? "sending" : ""}};
            }
            setThread(std::move(msgs));
        });
    } else if (kind == "chat") {
        auto q = Db::exec("SELECT * FROM (SELECT time, sender, text, self FROM chat_messages WHERE chat_key=? ORDER BY time DESC LIMIT 400) ORDER BY time", {ref});
        QVector<QVariantMap> msgs;
        const bool group = m_current["group"].toBool();
        while (q.next())
            msgs << QVariantMap{{"text", q.value(2)}, {"time", q.value(0)}, {"self", q.value(3).toBool()},
                                {"sender", group && !q.value(3).toBool() ? q.value(1).toString() : QString()}, {"status", ""}};
        setThread(std::move(msgs));
    }
}

void Messages::send(const QString &textIn) {
    const QString text = textIn.trimmed();
    if (text.isEmpty() || m_current.isEmpty()) return;
    const QString kind = m_current["kind"].toString();
    if (kind == "sms" || kind == "new") {
        m_thread.append({{"text", text}, {"time", QDateTime::currentMSecsSinceEpoch()}, {"self", true}, {"sender", ""}, {"status", "sending"}, {"day", ""}});
        m_link->request("sms.send", {{"address", m_current["number"].toString()}, {"body", text}},
                        [this](bool ok, const QJsonValue &r) { if (!ok) Q_EMIT toast("SMS failed: " + r.toString()); });
        return;
    }
    const QString key = m_current["ref"].toString();
    const QString notifKey = Db::scalar("SELECT notif_key FROM chats WHERE key=?", {key}).toString();
    if (notifKey.isEmpty()) {
        Q_EMIT toast("Replies work while the chat has a notification on the phone");
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_thread.append({{"text", text}, {"time", now}, {"self", true}, {"sender", ""}, {"status", "sending"}, {"day", ""}});
    m_notifs->reply(notifKey, text, [this, key, text, now](bool ok, const QString &err) {
        if (!ok) {
            Q_EMIT toast(err);
            return;
        }
        Db::exec("INSERT OR IGNORE INTO chat_messages VALUES(?,?,?,?,1)", {key, now, "You", text});
        Db::exec("UPDATE chats SET last_time=?, last_text=? WHERE key=?", {now, text, key});
        rebuild();
        if (m_current["ref"] == key) loadThread();
    });
}

void Messages::deleteChat(const QString &key) {
    Db::exec("DELETE FROM chat_messages WHERE chat_key=?", {key});
    Db::exec("DELETE FROM chats WHERE key=?", {key});
    if (m_current["ref"] == key) close();
    rebuild();
}
