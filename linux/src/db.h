#pragma once
#include <QSqlQuery>
#include <QVariantList>

// SQLite mirror (~/.local/share/tandem/tandem.db): contacts, photos, call log, chats, pending edits.
namespace Db {
void open();
QSqlQuery exec(const QString &sql, const QVariantList &args = {});
QVariant scalar(const QString &sql, const QVariantList &args = {});
QVariant meta(const QString &key, const QVariant &def = {});
void setMeta(const QString &key, const QVariant &value);
void begin();
void commit();
}
