#include "clipboard.h"

#include "config.h"
#include "link.h"

#include <QJsonObject>

#include <csignal>
#include <sys/prctl.h>

Clipboard::Clipboard(Link *link, QObject *parent) : QObject(parent), m_link(link) {
    // The watcher must not outlive us if we're killed.
    m_watch.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(link, &Link::event, this, [this](const QString &e, const QJsonValue &d) {
        if (e == "clip" && Config::get().value("clipboard_sync", true).toBool()) setLocal(d.toObject()["text"].toString());
    });
    // wl-paste prints a line for every selection change.
    connect(&m_watch, &QProcess::readyReadStandardOutput, this, [this] {
        m_watch.readAllStandardOutput();
        readAndPush();
    });
    auto sync = [this] {
        const bool want = !m_link->sleeping();
        if (want && m_watch.state() == QProcess::NotRunning) m_watch.start("wl-paste", {"-t", "text", "--watch", "echo", "x"});
        else if (!want && m_watch.state() != QProcess::NotRunning) { m_watch.kill(); m_watch.waitForFinished(500); }
    };
    connect(link, &Link::stateChanged, this, sync);
    sync();
}

void Clipboard::setLocal(const QString &text) {
    if (text.isEmpty()) return;
    m_last = text;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    p->start("wl-copy", {"--", text});
}

void Clipboard::readAndPush() {
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        p->deleteLater();
        const QString text = QString::fromUtf8(p->readAllStandardOutput());
        if (code != 0 || text.isEmpty() || text == m_last || text.size() > 1000000) return;
        m_last = text;
        if (Config::get().value("clipboard_sync", true).toBool() && m_link->online()) m_link->request("clip.set", {{"text", text}});
    });
    p->start("wl-paste", {"-n", "-t", "text"});
}

QString Clipboard::sendNow() {
    QProcess p;
    p.start("wl-paste", {"-n", "-t", "text"});
    p.waitForFinished(1500);
    const QString text = QString::fromUtf8(p.readAllStandardOutput());
    if (!text.isEmpty()) {
        m_last = text;
        m_link->request("clip.set", {{"text", text}});
    }
    return text;
}
