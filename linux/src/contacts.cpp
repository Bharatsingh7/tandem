#include "contacts.h"

#include "db.h"
#include "images.h"
#include "link.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QTimer>

static const QString kT9 = "22233344455566677778889999";

static QString t9Of(const QString &text) {
    QString out;
    for (QChar ch : text.toLower()) {
        if (ch >= 'a' && ch <= 'z') out += kT9[ch.unicode() - 'a'];
        else if (ch.isDigit()) out += ch;
        else out += ' ';
    }
    return out;
}

QString Contacts::digits(const QString &s) {
    QString d;
    for (QChar c : s) if (c.isDigit()) d += c;
    return d;
}

QString Contacts::key(const QString &number) {
    const QString d = digits(number);
    return d.size() >= 10 ? d.right(10) : d;
}

QString Contacts::pretty(const QString &number) {
    const QString d = digits(number);
    if (number.startsWith("+91") && d.size() == 12) return "+91 " + d.mid(2, 5) + " " + d.mid(7);
    if (d.size() == 10 && !number.startsWith('+')) return d.left(5) + " " + d.mid(5);
    return number;
}

Contacts::Contacts(Link *link, QObject *parent)
    : QObject(parent), m_link(link),
      m_model({"cid", "name", "phone", "phones", "emails", "starred", "hasPhoto", "org", "title", "section"}) {
    // Warm the image cache from SQLite once; photos are small.
    auto q = Db::exec("SELECT id, jpeg FROM contact_photos");
    while (q.next()) Images::get().put("contact/" + q.value(0).toString(), q.value(1).toByteArray());
    reindex();
    reload();
    connect(link, &Link::cameOnline, this, &Contacts::sync);
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &) { if (e == "contacts.changed") sync(); });
}

QVariantMap Contacts::rowToMap(const QSqlQuery &q) const {
    const auto phones = QJsonDocument::fromJson(q.value("phones").toByteArray()).array().toVariantList();
    const QString name = q.value("name").toString();
    const bool starred = q.value("starred").toInt();
    QString section = starred ? "★" : (name.isEmpty() || !name[0].isLetter() ? "#" : name.left(1).toUpper());
    return {{"cid", q.value("id")}, {"name", name},
            {"phone", phones.isEmpty() ? QString() : pretty(phones[0].toMap().value("n").toString())},
            {"phones", phones}, {"emails", QJsonDocument::fromJson(q.value("emails").toByteArray()).array().toVariantList()},
            {"starred", starred}, {"hasPhoto", q.value("has_photo").toInt() && Images::get().has("contact/" + q.value("id").toString())},
            {"org", q.value("org")}, {"title", q.value("title")}, {"section", section}};
}

void Contacts::reload() {
    QVector<QVariantMap> items;
    const QString s = m_search.trimmed().toLower();
    QSqlQuery q;
    if (s.isEmpty()) {
        q = Db::exec("SELECT * FROM contacts ORDER BY starred DESC, name COLLATE NOCASE");
    } else {
        const QString d = digits(s);
        q = Db::exec("SELECT * FROM contacts WHERE search LIKE ? OR (? != '' AND digits LIKE ?) "
                     "ORDER BY starred DESC, name COLLATE NOCASE",
                     {"%" + s + "%", d, "%" + d + "%"});
    }
    while (q.next()) items.append(rowToMap(q));
    m_model.setItems(std::move(items));
    m_total = Db::scalar("SELECT COUNT(*) FROM contacts").toInt();
    Q_EMIT changed();
}

void Contacts::setSearch(const QString &s) {
    if (s == m_search) return;
    m_search = s;
    Q_EMIT searchChanged();
    reload();
}

int Contacts::pending() const { return Db::scalar("SELECT COUNT(*) FROM pending").toInt(); }

void Contacts::reindex() {
    m_byKey.clear();
    auto q = Db::exec("SELECT id, name, phones FROM contacts");
    while (q.next()) {
        for (const auto &p : QJsonDocument::fromJson(q.value(2).toByteArray()).array()) {
            const QString k = key(p.toObject().value("n").toString());
            if (!k.isEmpty() && !m_byKey.contains(k)) m_byKey.insert(k, {q.value(0).toLongLong(), q.value(1).toString()});
        }
    }
}

QVariantMap Contacts::lookup(const QString &number) const {
    const auto it = m_byKey.constFind(key(number));
    if (it == m_byKey.constEnd() || number.isEmpty()) return {};
    return {{"cid", it->first}, {"name", it->second}, {"hasPhoto", Images::get().has("contact/" + QString::number(it->first))}};
}

QVariantMap Contacts::get(qint64 id) const {
    auto q = Db::exec("SELECT * FROM contacts WHERE id=?", {id});
    return q.next() ? rowToMap(q) : QVariantMap{};
}

QVariantList Contacts::favourites() const {
    QVariantList out;
    auto q = Db::exec("SELECT * FROM contacts WHERE starred=1 ORDER BY name COLLATE NOCASE");
    while (q.next()) out << rowToMap(q);
    return out;
}

QVariantList Contacts::t9(const QString &query, int limit) const {
    const QString q = digits(query);
    if (q.isEmpty()) return {};
    struct Hit { int score; bool starred; QString name; QVariantMap row; };
    QList<Hit> hits;
    auto rows = Db::exec("SELECT * FROM contacts");
    while (rows.next()) {
        const QString t = rows.value("t9").toString();
        int score;
        bool prefix = false;
        for (const auto &w : t.split(' ', Qt::SkipEmptyParts)) prefix |= w.startsWith(q);
        if (prefix) score = 0;
        else if (QString(t).remove(' ').contains(q)) score = 1;
        else if (rows.value("digits").toString().contains(q)) score = 2;
        else continue;
        hits.append({score, rows.value("starred").toBool(), rows.value("name").toString().toLower(), rowToMap(rows)});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return std::tie(a.score, b.starred, a.name) < std::tie(b.score, a.starred, b.name);
    });
    QVariantList out;
    QSet<QString> seen;
    for (const auto &h : hits) {
        for (const auto &p : h.row["phones"].toList()) {
            const auto pm = p.toMap();
            const QString d = digits(pm["n"].toString());
            if (seen.contains(d)) continue;
            seen.insert(d);
            out << QVariantMap{{"cid", h.row["cid"]}, {"name", h.row["name"]}, {"number", pm["n"]},
                               {"type", pm["type"]}, {"hasPhoto", h.row["hasPhoto"]}};
            if (out.size() >= limit) return out;
        }
    }
    // Numbers from the call log that aren't contacts still deserve a match.
    if (q.size() >= 3) {
        auto log = Db::exec("SELECT number, MAX(name) FROM calllog WHERE number LIKE ? GROUP BY number LIMIT 5", {"%" + q + "%"});
        while (log.next() && out.size() < limit) {
            const QString d = digits(log.value(0).toString());
            if (seen.contains(d)) continue;
            seen.insert(d);
            out << QVariantMap{{"cid", 0}, {"name", log.value(1).toString().isEmpty() ? pretty(log.value(0).toString()) : log.value(1)},
                               {"number", log.value(0)}, {"type", "recent"}, {"hasPhoto", false}};
        }
    }
    return out;
}

QVariantList Contacts::find(const QString &text, int limit) const {
    QVariantList out;
    const QString s = text.trimmed().toLower();
    if (s.isEmpty()) return out;
    const QString d = digits(s);
    auto q = Db::exec("SELECT * FROM contacts WHERE search LIKE ? OR (? != '' AND digits LIKE ?) "
                      "ORDER BY starred DESC, name COLLATE NOCASE LIMIT ?", {"%" + s + "%", d, "%" + d + "%", limit});
    while (q.next()) out << rowToMap(q);
    return out;
}

// ---- sync ----

void Contacts::sync() {
    if (m_syncing || !m_link->online()) return;
    m_syncing = true;
    flushPending();
    const qint64 since = Db::meta("contacts_since", 0).toLongLong();
    m_link->request("contacts.sync", {{"since", qMax<qint64>(0, since - 120000)}}, [this](bool ok, const QJsonValue &r) {
        m_syncing = false;
        if (!ok) return;
        const auto o = r.toObject();
        QSet<qint64> live;
        for (const auto &v : o.value("ids").toArray()) live.insert(v.toInteger());
        Db::begin();
        auto known = Db::exec("SELECT id FROM contacts");
        QList<qint64> gone;
        while (known.next()) if (!live.contains(known.value(0).toLongLong())) gone << known.value(0).toLongLong();
        for (qint64 id : gone) {
            Db::exec("DELETE FROM contacts WHERE id=?", {id});
            Db::exec("DELETE FROM contact_photos WHERE id=?", {id});
            Images::get().remove("contact/" + QString::number(id));
        }
        for (const auto &c : o.value("changed").toArray()) upsert(c.toObject());
        Db::commit();
        Db::setMeta("contacts_since", o.value("now").toInteger());
        reindex();
        reload();
        fetchPhotos();
    });
}

void Contacts::upsert(const QJsonObject &c) {
    QStringList words{c["name"].toString(), c["org"].toString()};
    for (const auto &e : c["emails"].toArray()) words << e.toObject()["a"].toString();
    QStringList ds;
    for (const auto &p : c["phones"].toArray()) ds << digits(p.toObject()["n"].toString());
    const qint64 id = c["id"].toInteger();
    Db::exec("INSERT OR REPLACE INTO contacts VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
             {id, c["name"].toString(), c["given"].toString(), c["family"].toString(), c["org"].toString(),
              c["title"].toString(), int(c["starred"].toBool()), c["updated"].toInteger(), int(c["hasPhoto"].toBool()),
              c["lookup"].toString(), QJsonDocument(c["phones"].toArray()).toJson(QJsonDocument::Compact),
              QJsonDocument(c["emails"].toArray()).toJson(QJsonDocument::Compact), words.join(' ').toLower(),
              t9Of(c["name"].toString()), ds.join(' ')});
    if (!c["hasPhoto"].toBool()) {
        Db::exec("DELETE FROM contact_photos WHERE id=?", {id});
        Images::get().remove("contact/" + QString::number(id));
    } else {
        Db::exec("UPDATE contact_photos SET updated=0 WHERE id=? AND updated<?", {id, c["updated"].toInteger()});
    }
}

void Contacts::fetchPhotos() {
    auto q = Db::exec("SELECT c.id FROM contacts c LEFT JOIN contact_photos p ON p.id=c.id "
                      "WHERE c.has_photo=1 AND (p.id IS NULL OR p.updated=0)");
    while (q.next()) if (!m_photoQueue.contains(q.value(0).toLongLong())) m_photoQueue << q.value(0).toLongLong();
    while (m_photoInFlight < 4 && !m_photoQueue.isEmpty()) {
        const qint64 id = m_photoQueue.takeFirst();
        ++m_photoInFlight;
        m_link->request("contacts.photo", {{"id", id}}, [this, id](bool ok, const QJsonValue &r) {
            --m_photoInFlight;
            const QString b64 = r.toObject().value("jpeg").toString();
            if (ok && !b64.isEmpty()) {
                const QByteArray jpeg = QByteArray::fromBase64(b64.toLatin1());
                Db::exec("INSERT OR REPLACE INTO contact_photos VALUES(?,?,?)", {id, jpeg, QDateTime::currentMSecsSinceEpoch()});
                Images::get().put("contact/" + QString::number(id), jpeg);
            }
            if (m_photoQueue.isEmpty() && m_photoInFlight == 0) {
                ++m_photoRev;
                reload();
                Q_EMIT photosChanged();
            } else if (m_link->online()) {
                fetchPhotos();
            }
        });
    }
}

// ---- edits ----

void Contacts::edit(const QString &op, const QJsonObject &payload) {
    if (!m_link->online()) {
        Db::exec("INSERT INTO pending(op, payload, ts) VALUES(?,?,?)",
                 {op, QJsonDocument(payload).toJson(QJsonDocument::Compact), QDateTime::currentMSecsSinceEpoch()});
        Q_EMIT toast("Saved here. It syncs to the phone when it connects");
        Q_EMIT changed();
        return;
    }
    m_link->request(op, payload, [this, op](bool ok, const QJsonValue &r) {
        if (!ok) {
            Q_EMIT toast("Couldn't save: " + r.toString());
            return;
        }
        if (op == "contacts.save" && r.isObject()) {
            upsert(r.toObject());
            reindex();
            reload();
            Q_EMIT toast("Saved to phone");
        }
        if (op == "contacts.setPhoto") {
            ++m_photoRev;
            Q_EMIT photosChanged();
        }
    });
}

void Contacts::flushPending() {
    auto q = Db::exec("SELECT id, op, payload FROM pending ORDER BY id");
    QList<std::tuple<qint64, QString, QByteArray>> rows;
    while (q.next()) rows.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toByteArray()});
    for (auto &[id, op, payload] : rows) {
        Db::exec("DELETE FROM pending WHERE id=?", {id});
        m_link->request(op, QJsonDocument::fromJson(payload).object());
    }
}

void Contacts::save(const QVariantMap &data) { edit("contacts.save", QJsonObject::fromVariantMap(data)); }

void Contacts::remove(qint64 id) {
    Db::exec("DELETE FROM contacts WHERE id=?", {id});
    Images::get().remove("contact/" + QString::number(id));
    reindex();
    reload();
    edit("contacts.delete", {{"id", id}});
}

void Contacts::star(qint64 id, bool on) {
    Db::exec("UPDATE contacts SET starred=? WHERE id=?", {int(on), id});
    reload();
    edit("contacts.star", {{"id", id}, {"on", on}});
}

void Contacts::setPhotoFromFile(qint64 id, const QString &path) {
    QImage img(path.startsWith("file://") ? QUrl(path).toLocalFile() : path);
    if (img.isNull()) return;
    const int side = qMin(img.width(), img.height());
    img = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side)
              .scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", 88);
    Db::exec("INSERT OR REPLACE INTO contact_photos VALUES(?,?,?)", {id, jpeg, QDateTime::currentMSecsSinceEpoch()});
    Db::exec("UPDATE contacts SET has_photo=1 WHERE id=?", {id});
    Images::get().put("contact/" + QString::number(id), jpeg);
    ++m_photoRev;
    Q_EMIT photosChanged();
    reload();
    edit("contacts.setPhoto", {{"id", id}, {"jpeg", QString::fromLatin1(jpeg.toBase64())}});
}

// ---- vCard ----

static QString vesc(QString s) {
    return s.replace("\\", "\\\\").replace(",", "\\,").replace(";", "\;").replace("\n", "\\n");
}
static QString vunesc(QString s) {
    return s.replace("\\n", "\n").replace("\\,", ",").replace("\;", ";").replace("\\\\", "\\").trimmed();
}

int Contacts::exportVcf(const QString &path) const {
    QFile f(path.startsWith("file://") ? QUrl(path).toLocalFile() : path);
    if (!f.open(QIODevice::WriteOnly)) return 0;
    int n = 0;
    auto q = Db::exec("SELECT * FROM contacts ORDER BY name COLLATE NOCASE");
    while (q.next()) {
        QStringList out{"BEGIN:VCARD", "VERSION:3.0", "FN:" + vesc(q.value("name").toString()),
                        "N:" + vesc(q.value("family").toString()) + ";" + vesc(q.value("given").toString()) + ";;;"};
        if (!q.value("org").toString().isEmpty()) out << "ORG:" + vesc(q.value("org").toString());
        if (!q.value("title").toString().isEmpty()) out << "TITLE:" + vesc(q.value("title").toString());
        for (const auto &p : QJsonDocument::fromJson(q.value("phones").toByteArray()).array()) {
            const QString t = p.toObject()["type"].toString();
            out << QString("TEL;TYPE=%1:%2").arg(t == "home" ? "HOME" : t == "work" ? "WORK" : "CELL", p.toObject()["n"].toString());
        }
        for (const auto &e : QJsonDocument::fromJson(q.value("emails").toByteArray()).array())
            out << QString("EMAIL;TYPE=%1:%2").arg(e.toObject()["type"].toString() == "work" ? "WORK" : "HOME", e.toObject()["a"].toString());
        const QByteArray photo = Images::get().raw("contact/" + q.value("id").toString());
        if (!photo.isEmpty()) out << "PHOTO;ENCODING=b;TYPE=JPEG:" + QString::fromLatin1(photo.toBase64());
        out << "END:VCARD";
        f.write((out.join("\r\n") + "\r\n").toUtf8());
        ++n;
    }
    return n;
}

int Contacts::importVcf(const QString &path) {
    QFile f(path.startsWith("file://") ? QUrl(path).toLocalFile() : path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QString text = QString::fromUtf8(f.readAll());
    text.replace(QRegularExpression("\r?\n[ \t]"), "");
    int n = 0;
    auto it = QRegularExpression("BEGIN:VCARD(.*?)END:VCARD", QRegularExpression::DotMatchesEverythingOption |
                                 QRegularExpression::CaseInsensitiveOption).globalMatch(text);
    while (it.hasNext()) {
        QVariantMap d{{"name", ""}, {"given", ""}, {"family", ""}, {"org", ""}, {"title", ""}};
        QVariantList phones, emails;
        for (const QString &line : it.next().captured(1).split(QRegularExpression("\r?\n"))) {
            const int colon = line.indexOf(':');
            if (colon < 0) continue;
            const QString head = line.left(colon).toUpper(), val = line.mid(colon + 1);
            const QString prop = head.section(';', 0, 0);
            if (prop == "FN") d["name"] = vunesc(val);
            else if (prop == "N") { d["family"] = vunesc(val.section(';', 0, 0)); d["given"] = vunesc(val.section(';', 1, 1)); }
            else if (prop == "ORG") d["org"] = vunesc(val.section(';', 0, 0));
            else if (prop == "TITLE") d["title"] = vunesc(val);
            else if (prop == "TEL") phones << QVariantMap{{"n", val.trimmed()}, {"type", head.contains("WORK") ? "work" : head.contains("HOME") ? "home" : "mobile"}};
            else if (prop == "EMAIL") emails << QVariantMap{{"a", val.trimmed()}, {"type", head.contains("WORK") ? "work" : "home"}};
        }
        d["phones"] = phones;
        d["emails"] = emails;
        if (!d["name"].toString().isEmpty() || !d["given"].toString().isEmpty()) {
            save(d);
            ++n;
        }
    }
    return n;
}
