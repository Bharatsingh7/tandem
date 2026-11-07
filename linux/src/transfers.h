#pragma once
#include "listmodel.h"

#include <QFile>
#include <QHash>
#include <QObject>
#include <QUrl>

class Link;

// Phone photos (lazy thumbnails) and file transfer both ways, chunked over the link.
class Transfers : public QObject {
    Q_OBJECT
    Q_PROPERTY(ListModel *photos READ photos CONSTANT)
    Q_PROPERTY(QString progressName READ progressName NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    explicit Transfers(Link *link, QObject *parent = nullptr);
    ListModel *photos() { return &m_photos; }
    QString progressName() const { return m_progressName; }
    double progress() const { return m_progress; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void sendFiles(const QList<QUrl> &files);
    Q_INVOKABLE void sendPaths(const QStringList &paths);
    Q_INVOKABLE void download(int row);

Q_SIGNALS:
    void progressChanged();
    void loadingChanged();
    void toast(const QString &text);

private:
    struct Incoming { QFile *file; qint64 size; qint64 got; QString name; };
    void onEvent(const QString &e, const QJsonValue &d);
    void pumpThumbs();
    void sendNext();
    void setProgress(const QString &name, double p);

    Link *m_link;
    ListModel m_photos;
    QHash<int, Incoming> m_incoming;
    QStringList m_outQueue;
    bool m_sending = false, m_loading = false;
    QList<int> m_thumbQueue;
    int m_thumbsInFlight = 0;
    QString m_progressName;
    double m_progress = 1.0;
};
