#include "db.h"

#include "config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

namespace Db {

void open() {
    auto db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(Config::dataDir() + "/tandem.db");
    db.open();
    const char *schema[] = {
        "CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT)",
        "CREATE TABLE IF NOT EXISTS contacts(id INTEGER PRIMARY KEY, name TEXT, given TEXT, family TEXT, org TEXT, title TEXT,"
        " starred INTEGER, updated INTEGER, has_photo INTEGER, lookup TEXT, phones TEXT, emails TEXT, search TEXT, t9 TEXT, digits TEXT)",
        "CREATE TABLE IF NOT EXISTS contact_photos(id INTEGER PRIMARY KEY, jpeg BLOB, updated INTEGER)",
        "CREATE TABLE IF NOT EXISTS pending(id INTEGER PRIMARY KEY AUTOINCREMENT, op TEXT, payload TEXT, ts INTEGER)",
        "CREATE TABLE IF NOT EXISTS calllog(id INTEGER PRIMARY KEY, number TEXT, name TEXT, type TEXT, date INTEGER,"
        " duration INTEGER, account TEXT, place TEXT)",
        "CREATE TABLE IF NOT EXISTS chats(key TEXT PRIMARY KEY, pkg TEXT, app TEXT, title TEXT, is_group INTEGER,"
        " last_time INTEGER, last_text TEXT, unread INTEGER DEFAULT 0, notif_key TEXT, avatar BLOB)",
        "CREATE TABLE IF NOT EXISTS chat_messages(chat_key TEXT, time INTEGER, sender TEXT, text TEXT, self INTEGER,"
        " UNIQUE(chat_key, time, sender, text))",
        "CREATE INDEX IF NOT EXISTS chat_messages_by_chat ON chat_messages(chat_key, time)",
        "CREATE INDEX IF NOT EXISTS calllog_by_date ON calllog(date DESC)",
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
    };
    for (auto s : schema) exec(s);
}

QSqlQuery exec(const QString &sql, const QVariantList &args) {
    QSqlQuery q;
    q.prepare(sql);
    for (const auto &a : args) q.addBindValue(a);
    if (!q.exec()) qWarning() << "sql:" << q.lastError().text() << sql;
    return q;
}

QVariant scalar(const QString &sql, const QVariantList &args) {
    auto q = exec(sql, args);
    return q.next() ? q.value(0) : QVariant{};
}

QVariant meta(const QString &key, const QVariant &def) {
    const auto v = scalar("SELECT v FROM meta WHERE k=?", {key});
    if (!v.isValid()) return def;
    return QJsonDocument::fromJson("[" + v.toString().toUtf8() + "]").array().first().toVariant();
}

void setMeta(const QString &key, const QVariant &value) {
    const QByteArray j = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
    exec("INSERT OR REPLACE INTO meta VALUES(?,?)", {key, QString::fromUtf8(j.mid(1, j.size() - 2))});
}

void begin() { QSqlDatabase::database().transaction(); }
void commit() { QSqlDatabase::database().commit(); }
}
