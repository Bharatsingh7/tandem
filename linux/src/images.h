#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Thread-safe image store served to QML as image://t/<key>[?rev]. Keys: contact/<id>, app/<pkg>,
// chat/<hash>, art, thumb/<id>, notif/<hash>. Encoded bytes are kept and decoded on first use.
class Images : public QQuickImageProvider {
public:
    static Images &get();
    Images() : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading) {}
    void put(const QString &key, const QByteArray &encoded);
    void remove(const QString &key);
    bool has(const QString &key);
    QByteArray raw(const QString &key);
    QImage image(const QString &key);
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

private:
    QMutex m_lock;
    QHash<QString, QByteArray> m_raw;
    QHash<QString, QImage> m_decoded;
};
