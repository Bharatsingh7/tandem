#pragma once
#include <QHash>
#include <QObject>
#include <QStringList>
#include <functional>

// freedesktop notifications with action buttons, fully async (never blocks the UI).
class Notifier : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<void(const QString &action)>;
    struct Opts {
        QList<QPair<QString, QString>> actions;
        QString image;
        uint replaces = 0;
        int urgency = 1;
        QString category;
        bool resident = false;
        int timeout = -1;
    };
    static Notifier &get();
    // done(id) receives the server id, needed to replace or close the notification later.
    void notify(const QString &summary, const QString &body, const Opts &o, Handler h = {},
                std::function<void(uint)> done = {});
    void close(uint id);
    static QString cacheImage(const QByteArray &bytes, const QString &ext = "png");

private Q_SLOTS:
    void onAction(uint id, const QString &key);
    void onClosed(uint id, uint reason);

private:
    Notifier();
    QHash<uint, Handler> m_handlers;
};
