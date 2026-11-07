#include "images.h"

#include <QPainter>

Images &Images::get() {
    static auto *i = new Images;  // handed to the QML engine, lives for the whole run
    return *i;
}

void Images::put(const QString &key, const QByteArray &encoded) {
    QMutexLocker l(&m_lock);
    m_raw.insert(key, encoded);
    m_decoded.remove(key);
}

void Images::remove(const QString &key) {
    QMutexLocker l(&m_lock);
    m_raw.remove(key);
    m_decoded.remove(key);
}

bool Images::has(const QString &key) {
    QMutexLocker l(&m_lock);
    return m_raw.contains(key);
}

QByteArray Images::raw(const QString &key) {
    QMutexLocker l(&m_lock);
    return m_raw.value(key);
}

QImage Images::image(const QString &key) {
    QMutexLocker l(&m_lock);
    if (auto it = m_decoded.constFind(key); it != m_decoded.constEnd()) return *it;
    const QByteArray bytes = m_raw.value(key);
    if (bytes.isEmpty()) return {};
    QImage img;
    img.loadFromData(bytes);
    m_decoded.insert(key, img);
    return img;
}

QImage Images::requestImage(const QString &id, QSize *size, const QSize &requested) {
    QImage img = image(id.section('?', 0, 0));
    const bool round = id.section('?', 1).split('&').contains("round");
    if (img.isNull()) {
        img = QImage(1, 1, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
    } else if (round) {
        // Avatars: centre-crop, scale, and mask to a circle here so QML needs no per-delegate effect.
        const int side = qMin(img.width(), img.height());
        const int out = requested.isValid() && requested.width() > 0 ? qMin(requested.width() * 2, side) : side;
        QImage sq = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side)
                        .scaled(out, out, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage dst(out, out, QImage::Format_ARGB32_Premultiplied);
        dst.fill(Qt::transparent);
        QPainter p(&dst);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QBrush(sq));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(0, 0, out, out));
        p.end();
        img = dst;
    } else if (requested.isValid() && requested.width() > 0 &&
               (img.width() > requested.width() * 2 || img.height() > requested.height() * 2)) {
        img = img.scaled(requested * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (size) *size = img.size();
    return img;
}
