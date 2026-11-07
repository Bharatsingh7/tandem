#pragma once
#include "listmodel.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

class Link;

// Phone notifications: a model for the Notifications page and desktop popups with actions.
class Notifs : public QObject {
    Q_OBJECT
    Q_PROPERTY(ListModel *model READ model CONSTANT)
    Q_PROPERTY(int unread READ unread NOTIFY unreadChanged)
public:
    explicit Notifs(Link *link, QObject *parent = nullptr);
    ListModel *model() { return &m_model; }
    int unread() const;
    QJsonObject find(const QString &key) const { return m_items.value(key); }

    Q_INVOKABLE void dismiss(const QString &key = {});
    Q_INVOKABLE void reply(const QString &key, const QString &text);
    void reply(const QString &key, const QString &text, std::function<void(bool, QString)> done);
    Q_INVOKABLE void action(const QString &key, int index);
    Q_INVOKABLE void open(const QString &key);
    Q_INVOKABLE void muteApp(const QString &pkg, bool mute);
    Q_INVOKABLE bool isMuted(const QString &pkg) const;
    QString smsPkg;
    std::function<bool()> messagesVisible;

Q_SIGNALS:
    void unreadChanged();
    void posted(const QJsonObject &n);
    void removed(const QString &key);
    void openChat(const QJsonObject &n);
    void openPage(const QString &page);
    void toast(const QString &text);

private:
    void rebuild();
    void popup(const QJsonObject &n, bool fresh);
    void wantIcon(const QString &pkg);
    QVariantMap toRow(const QJsonObject &n) const;

    Link *m_link;
    ListModel m_model;
    QHash<QString, QJsonObject> m_items;
    QHash<QString, uint> m_popups;
    QSet<QString> m_icons;
};
