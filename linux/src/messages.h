#pragma once
#include "listmodel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class Link;
class Contacts;
class Notifs;

// SMS threads (live from the phone) + chats rebuilt from messaging notifications (kept in SQLite).
class Messages : public QObject {
    Q_OBJECT
    Q_PROPERTY(ListModel *conversations READ conversations CONSTANT)
    Q_PROPERTY(ListModel *thread READ thread CONSTANT)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(int unread READ unread NOTIFY unreadChanged)
public:
    Messages(Link *link, Contacts *contacts, Notifs *notifs, QObject *parent = nullptr);
    ListModel *conversations() { return &m_convs; }
    ListModel *thread() { return &m_thread; }
    QVariantMap current() const { return m_current; }
    QString filter() const { return m_filter; }
    QString search() const { return m_search; }
    void setFilter(const QString &f);
    void setSearch(const QString &s);
    int unread() const { return m_unread; }

    Q_INVOKABLE void open(const QString &kind, const QString &ref);
    Q_INVOKABLE void openNumber(const QString &number);
    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void close();
    Q_INVOKABLE void deleteChat(const QString &key);
    void openForNotification(const QJsonObject &n);

Q_SIGNALS:
    void currentChanged();
    void filterChanged();
    void unreadChanged();
    void toast(const QString &text);

private:
    void ingest(const QJsonObject &n);
    void refreshSms();
    void rebuild();
    void loadThread();
    void setThread(QVector<QVariantMap> msgs);
    static QString chatKey(const QJsonObject &n);

    Link *m_link;
    Contacts *m_contacts;
    Notifs *m_notifs;
    ListModel m_convs, m_thread;
    QJsonArray m_threads;
    QVariantMap m_current;
    QString m_filter = "all", m_search;
    int m_unread = 0;
    QSet<QString> m_whatsapp{"com.whatsapp", "com.whatsapp.w4b"};
};
