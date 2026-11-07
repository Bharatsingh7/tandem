#include "app.h"

#include "bluetooth.h"
#include "calls.h"
#include "clipboard.h"
#include "config.h"
#include "link.h"
#include "messages.h"
#include "phone.h"
#include "transfers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

static QString unitPath() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/systemd/user/tandem.service"; }

App::App(Link *link, Phone *phone, Calls *calls, Messages *messages, Transfers *transfers, Clipboard *clip, Bluetooth *bt, QObject *parent)
    : QObject(parent), m_link(link), m_phone(phone), m_calls(calls), m_messages(messages), m_transfers(transfers), m_clip(clip), m_bt(bt) {}

static QString uriNumber(const QString &uri) {
    return QUrl::fromPercentEncoding(uri.section(':', 1).section('?', 0, 0).toUtf8());
}

void App::handleArgs(const QStringList &args, const QString &cwd, bool firstRun) {
    bool acted = false, background = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&](const QString &def = {}) {
            if (a.contains('=')) return a.section('=', 1);
            return i + 1 < args.size() && !args[i + 1].startsWith("--") ? args[++i] : def;
        };
        const QString opt = a.section('=', 0, 0);
        if (opt == "--background") background = true;
        else if (opt == "--quit") { quit(); return; }
        else if (opt == "--page") { Q_EMIT showPage(next("home"), {}); acted = true; }
        else if (opt == "--dial") { Q_EMIT showPage("calls", {{"number", next()}}); acted = true; }
        else if (opt == "--call") { m_calls->dial(next()); acted = true; }
        else if (opt == "--sms") { m_messages->openNumber(next()); Q_EMIT showPage("messages", {}); acted = true; }
        else if (opt == "--toggle") {
            const QString v = next();
            m_phone->setToggle(v.section('=', 0, 0), QStringList{"on", "1", "true", "yes"}.contains(v.section('=', 1).toLower()));
            acted = true;
        } else if (opt == "--ring") { m_phone->ring(true); acted = true; }
        else if (opt == "--lock") { m_phone->lock(); acted = true; }
        else if (opt == "--pair" || opt == "--forget") { m_link->forget(); acted = true; }
        else if (opt == "--reconnect") { m_link->reconnect(); acted = true; }
        else if (opt == "--pair-bluetooth") { m_pairTries = 0; pairBluetooth(); acted = true; }
        else if (opt == "--bt-pan") { if (m_bt) m_bt->connectPan(); acted = true; }
        else if (opt == "--sleep") { m_link->setSleeping(true); acted = true; }
        else if (opt == "--wake") { m_link->setSleeping(false); acted = true; }
        else if (opt == "--call-window") { Q_EMIT showCallWindow(); acted = true; }
        else if (opt == "--answer") { m_calls->answer(); acted = true; }
        else if (opt == "--decline") { m_calls->reject(); acted = true; }
        else if (opt == "--hangup") { m_calls->end(); acted = true; }
        else if (opt == "--mute") { m_calls->mute(!m_calls->audio().value("muted").toBool()); acted = true; }
        else if (opt == "--media") {
            QString c = next("toggle");
            if (c == "toggle") c = m_phone->media().value("state") == "playing" ? "pause" : "play";
            m_phone->mediaCmd(c);
            acted = true;
        } else if (opt == "--ringer") { m_phone->setRinger(next("normal")); acted = true; }
        else if (opt == "--clipboard") { m_clip->sendNow(); acted = true; }
        else if (opt == "--open-url") { m_phone->openUrl(next()); acted = true; }
        else if (opt == "--send") {
            QStringList files;
            while (i + 1 < args.size() && !args[i + 1].startsWith("--")) files << QDir(cwd).absoluteFilePath(args[++i]);
            m_transfers->sendPaths(files);
            acted = true;
        } else if (a.startsWith("tel:")) { Q_EMIT showPage("calls", {{"number", uriNumber(a)}}); acted = true; }
        else if (a.startsWith("sms:") || a.startsWith("smsto:")) { m_messages->openNumber(uriNumber(a)); Q_EMIT showPage("messages", {}); acted = true; }
    }
    if (!acted && !(background && firstRun)) Q_EMIT showPage("", {});
}

bool App::autostart() const {
    QProcess p;
    p.start("systemctl", {"--user", "is-enabled", "tandem.service"});
    p.waitForFinished(1500);
    return p.readAllStandardOutput().trimmed() == "enabled";
}

void App::setAutostart(bool on) {
    if (on) {
        QDir().mkpath(QFileInfo(unitPath()).absolutePath());
        QFile f(unitPath());
        if (f.open(QIODevice::WriteOnly))
            f.write(QString("[Unit]\nDescription=Tandem phone link\nPartOf=graphical-session.target\nAfter=graphical-session.target\n\n"
                            "[Service]\nExecStart=%1 --background\nRestart=on-failure\nRestartSec=5\n\n"
                            "[Install]\nWantedBy=graphical-session.target\n").arg(QCoreApplication::applicationFilePath()).toUtf8());
        f.close();
        QProcess::execute("systemctl", {"--user", "daemon-reload"});
        QProcess::execute("systemctl", {"--user", "enable", "tandem.service"});
    } else {
        QProcess::execute("systemctl", {"--user", "disable", "tandem.service"});
    }
    Q_EMIT autostartChanged();
}

// Pair the phone with this PC as its call headset: PC becomes discoverable with an agent, the phone starts the bond.
void App::pairBluetooth() {
    if (!m_bt->available()) { Q_EMIT toast("This PC has no Bluetooth adapter"); return; }
    if (!m_link->online()) {
        if (++m_pairTries <= 10) QTimer::singleShot(2000, this, &App::pairBluetooth);  // wait for the link after a reconnect
        else Q_EMIT toast("Phone not connected");
        return;
    }
    const bool phoneBtOn = m_phone->status().value("toggles").toMap().value("bluetooth").toBool();
    if (!phoneBtOn) {
        if (m_pairTries++ == 0) { m_phone->setToggle("bluetooth", true); Q_EMIT toast("Turning on the phone's Bluetooth…"); }
        if (m_pairTries <= 10) QTimer::singleShot(2000, this, &App::pairBluetooth);
        else Q_EMIT toast("Couldn't turn on the phone's Bluetooth");
        return;
    }
    m_pairTries = 0;
    Config::get().setValue("phone_bt_name", m_phone->status().value("btName").toString());
    if (!m_bt->beginPairing(120)) return;
    m_link->request("bt.pair", {{"address", m_bt->address()}}, [this](bool ok, const QJsonValue &r) {
        if (!ok) { Q_EMIT toast("Pairing failed: " + r.toString()); m_bt->endPairing(); return; }
        if (r.toObject()["bonded"].toBool()) { Q_EMIT toast("Already paired"); m_bt->endPairing(); return; }
        Q_EMIT toast("Tap Pair on the phone to finish");
    });
}

void App::quit() { QCoreApplication::quit(); }
QString App::fileUrlToPath(const QUrl &u) const { return u.toLocalFile(); }
