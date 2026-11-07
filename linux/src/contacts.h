#pragma once
#include "listmodel.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

class Link;

// Two-way synced contacts with an offline SQLite mirror, T9 search and number lookup.
class Contacts : public QObject {
    Q_OBJECT
    Q_PROPERTY(ListModel *model READ model CONSTANT)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int pending READ pending NOTIFY changed)
    Q_PROPERTY(int photoRev READ photoRev NOTIFY photosChanged)
    Q_PROPERTY(QVariantList favourites READ favourites NOTIFY changed)
public:
    explicit Contacts(Link *link, QObject *parent = nullptr);
    ListModel *model() { return &m_model; }
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    int total() const { return m_total; }
    int pending() const;
    int photoRev() const { return m_photoRev; }
    QVariantList favourites() const;

    Q_INVOKABLE QVariantMap get(qint64 id) const;
    Q_INVOKABLE QVariantMap lookup(const QString &number) const;   // {cid, name, hasPhoto}
    Q_INVOKABLE QVariantList t9(const QString &query, int limit = 12) const;
    Q_INVOKABLE QVariantList find(const QString &text, int limit = 8) const;
    Q_INVOKABLE void save(const QVariantMap &data);
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE void star(qint64 id, bool on);
    Q_INVOKABLE void setPhotoFromFile(qint64 id, const QString &path);
    Q_INVOKABLE int exportVcf(const QString &path) const;
    Q_INVOKABLE int importVcf(const QString &path);
    Q_INVOKABLE void sync();
    Q_INVOKABLE static QString pretty(const QString &number);
    static QString key(const QString &number);
    static QString digits(const QString &s);

Q_SIGNALS:
    void changed();
    void searchChanged();
    void photosChanged();
    void toast(const QString &text);

private:
    void upsert(const QJsonObject &c);
    void reload();
    void reindex();
    void fetchPhotos();
    void edit(const QString &op, const QJsonObject &payload);
    void flushPending();
    QVariantMap rowToMap(const class QSqlQuery &q) const;

    Link *m_link;
    ListModel m_model;
    QString m_search;
    int m_total = 0, m_photoRev = 0;
    bool m_syncing = false;
    QHash<QString, QPair<qint64, QString>> m_byKey;
    QList<qint64> m_photoQueue;
    int m_photoInFlight = 0;
};
