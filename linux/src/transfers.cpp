#include "transfers.h"

#include "config.h"
#include "images.h"
#include "link.h"
#include "notifier.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>

static constexpr qint64 kChunk = 192 * 1024;

static QString uniquePath(const QString &dir, const QString &name) {
    QDir().mkpath(dir);
    QFileInfo fi(QDir(dir).filePath(QFileInfo(name).fileName().isEmpty() ? QString("file") : QFileInfo(name).fileName()));
    QString path = fi.filePath();
    for (int n = 1; QFile::exists(path); ++n)
        path = QDir(dir).filePath(QString("%1 (%2)%3").arg(fi.completeBaseName()).arg(n)
                                      .arg(fi.suffix().isEmpty() ? QString() : "." + fi.suffix()));
    return path;
}

Transfers::Transfers(Link *link, QObject *parent)
    : QObject(parent), m_link(link), m_photos({"pid", "name", "date", "video", "size", "thumb"}) {
    connect(link, &Link::event, this, &Transfers::onEvent);
    connect(link, &Link::cameOnline, this, [this] { if (m_photos.count() == 0) reload(); });
}

void Transfers::setProgress(const QString &name, double p) {
    m_progressName = name;
    m_progress = p;
    Q_EMIT progressChanged();
}

void Transfers::onEvent(const QString &e, const QJsonValue &dv) {
    const auto d = dv.toObject();
    const int tid = d["tid"].toInt();
    if (e == "file.offer") {
        const QString path = uniquePath(Config::get().value("downloads").toString(), d["name"].toString());
        auto *f = new QFile(path, this);
        f->open(QIODevice::WriteOnly);
        m_incoming.insert(tid, {f, d["size"].toInteger(), 0, d["name"].toString()});
    } else if (e == "file.chunk") {
        auto it = m_incoming.find(tid);
        if (it == m_incoming.end()) return;
        const QByteArray b = QByteArray::fromBase64(d["data"].toString().toLatin1());
        it->file->write(b);
        it->got += b.size();
        if (it->size > 0) setProgress(it->name, double(it->got) / it->size);
    } else if (e == "file.done") {
        auto in = m_incoming.take(tid);
        if (!in.file) return;
        const QString path = in.file->fileName();
        in.file->close();
        in.file->deleteLater();
        setProgress(in.name, 1.0);
        Notifier::Opts o;
        o.actions = {{"default", "Open"}, {"folder", "Show in folder"}};
        o.category = "transfer.complete";
        if (QMimeDatabase().mimeTypeForFile(path).name().startsWith("image/")) o.image = path;
        Notifier::get().notify("Received " + QFileInfo(path).fileName(), "From your phone", o, [path](const QString &a) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(a == "folder" ? QFileInfo(path).absolutePath() : path));
        });
    }
}

// ---- photos ----

void Transfers::reload() {
    m_photos.setItems({});
    m_thumbQueue.clear();
    loadMore();
}

void Transfers::loadMore() {
    if (m_loading || !m_link->online()) return;
    m_loading = true;
    Q_EMIT loadingChanged();
    m_link->request("photos.list", {{"limit", 90}, {"offset", m_photos.count()}}, [this](bool ok, const QJsonValue &r) {
        m_loading = false;
        Q_EMIT loadingChanged();
        if (!ok) return;
        for (const auto &v : r.toArray()) {
            const auto p = v.toObject();
            const QString key = "thumb/" + QString::number(p["id"].toInteger());
            m_photos.append({{"pid", p["id"].toInteger()}, {"name", p["name"]}, {"date", p["date"]}, {"video", p["video"]},
                             {"size", p["size"]}, {"thumb", Images::get().has(key) ? key : QString()}});
            if (!Images::get().has(key)) m_thumbQueue << m_photos.count() - 1;
        }
        pumpThumbs();
    });
}

void Transfers::pumpThumbs() {
    while (m_thumbsInFlight < 6 && !m_thumbQueue.isEmpty()) {
        const int row = m_thumbQueue.takeFirst();
        const auto item = m_photos.get(row);
        ++m_thumbsInFlight;
        m_link->request("photos.thumb", {{"id", item["pid"].toLongLong()}, {"video", item["video"].toBool()}, {"size", 320}},
                        [this, row, item](bool ok, const QJsonValue &r) {
                            --m_thumbsInFlight;
                            if (ok && row < m_photos.count() && m_photos.get(row)["pid"] == item["pid"]) {
                                const QString key = "thumb/" + item["pid"].toString();
                                Images::get().put(key, QByteArray::fromBase64(r.toObject()["jpeg"].toString().toLatin1()));
                                m_photos.setField(row, "thumb", key);
                            }
                            pumpThumbs();
                        });
    }
}

void Transfers::download(int row) {
    const auto item = m_photos.get(row);
    if (item.isEmpty()) return;
    const QString path = uniquePath(QDir::homePath() + "/Pictures/Tandem", item["name"].toString());
    auto *f = new QFile(path, this);
    if (!f->open(QIODevice::WriteOnly)) return;
    const qint64 size = item["size"].toLongLong();
    auto step = std::make_shared<std::function<void(qint64)>>();
    *step = [this, f, item, size, path, step](qint64 off) {
        m_link->request("photos.read", {{"id", item["pid"].toLongLong()}, {"video", item["video"].toBool()}, {"offset", off}},
                        [this, f, item, size, path, step, off](bool ok, const QJsonValue &r) {
                            if (!ok) {
                                f->close(); f->deleteLater();
                                Q_EMIT toast("Copy failed: " + r.toString());
                                return;
                            }
                            const QByteArray b = QByteArray::fromBase64(r.toObject()["data"].toString().toLatin1());
                            f->write(b);
                            const qint64 next = off + b.size();
                            setProgress(item["name"].toString(), size > 0 ? double(next) / size : 0.5);
                            if (r.toObject()["eof"].toBool()) {
                                f->close(); f->deleteLater();
                                setProgress(item["name"].toString(), 1.0);
                                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                            } else {
                                (*step)(next);
                            }
                        });
    };
    (*step)(0);
}

// ---- PC -> phone ----

void Transfers::sendFiles(const QList<QUrl> &files) {
    QStringList paths;
    for (const auto &u : files) paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    sendPaths(paths);
}

void Transfers::sendPaths(const QStringList &paths) {
    m_outQueue << paths;
    if (!m_sending) sendNext();
}

void Transfers::sendNext() {
    if (m_outQueue.isEmpty()) {
        m_sending = false;
        return;
    }
    m_sending = true;
    const QString path = m_outQueue.takeFirst();
    auto *f = new QFile(path, this);
    if (!f->open(QIODevice::ReadOnly)) {
        Q_EMIT toast("Can't read " + path);
        f->deleteLater();
        return sendNext();
    }
    const QString name = QFileInfo(path).fileName();
    m_link->request("file.begin", {{"name", name}, {"size", f->size()}, {"mime", QMimeDatabase().mimeTypeForFile(path).name()}},
                    [this, f, name](bool ok, const QJsonValue &r) {
        if (!ok) {
            Q_EMIT toast("Send failed: " + r.toString());
            f->deleteLater();
            return sendNext();
        }
        const int tid = r.toObject()["tid"].toInt();
        auto pump = std::make_shared<std::function<void()>>();
        *pump = [this, f, name, tid, pump] {
            const QByteArray chunk = f->read(kChunk);
            if (chunk.isEmpty()) {
                m_link->request("file.end", {{"tid", tid}}, [this, f, name](bool ok, const QJsonValue &) {
                    f->deleteLater();
                    setProgress(name, 1.0);
                    Q_EMIT toast(ok ? name + " sent to phone (Download/Tandem)" : "Send failed: " + name);
                    sendNext();
                });
                return;
            }
            m_link->request("file.chunk", {{"tid", tid}, {"data", QString::fromLatin1(chunk.toBase64())}},
                            [this, f, name, pump](bool ok, const QJsonValue &r) {
                                if (!ok) {
                                    f->deleteLater();
                                    Q_EMIT toast("Send failed: " + r.toString());
                                    return sendNext();
                                }
                                setProgress(name, double(f->pos()) / qMax<qint64>(1, f->size()));
                                (*pump)();
                            });
        };
        (*pump)();
    });
}
