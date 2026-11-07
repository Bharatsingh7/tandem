#include "app.h"
#include "bluetooth.h"
#include "calls.h"
#include "clipboard.h"
#include "config.h"
#include "contacts.h"
#include "db.h"
#include "images.h"
#include "link.h"
#include "messages.h"
#include "mpris.h"
#include "notifs.h"
#include "phone.h"
#include "stateexport.h"
#include "transfers.h"

#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSocketNotifier>

#include <csignal>
#include <unistd.h>

// Lives in $XDG_RUNTIME_DIR (0700, per user) rather than shared /tmp, so nobody else can squat or reach it.
// SIGTERM/SIGINT (systemd stop, logout) → normal quit, so children stop and the notch state file is removed.
static int sigPipe[2];
static void onSignal(int) { char c = 1; [[maybe_unused]] auto n = ::write(sigPipe[1], &c, 1); }

static QString socketName() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.local/share/tandem";
    return dir + "/tandem.sock";
}

int main(int argc, char **argv) {
    QGuiApplication::setDesktopFileName("com.cyberwolf.Tandem");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Tandem");
    app.setOrganizationName("cyberwolf");
    app.setQuitOnLastWindowClosed(false);
    if (::pipe(sigPipe) == 0) {
        auto *sn = new QSocketNotifier(sigPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
    }

    QStringList args = app.arguments().mid(1);

    // Single instance: hand our arguments to the running one and leave.
    {
        QLocalSocket probe;
        probe.connectToServer(socketName());
        if (probe.waitForConnected(300)) {
            probe.write((QDir::currentPath() + '\n' + args.join('\n')).toUtf8());
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            return 0;
        }
    }
    QLocalServer::removeServer(socketName());
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(socketName());

    Db::open();
    QQuickStyle::setStyle("Basic");

    auto *link = new Link(&app);
    auto *bt = new Bluetooth(&app);
    link->setBluetooth(bt);
    auto *phone = new Phone(link, &app);
    auto *contacts = new Contacts(link, &app);
    auto *notifs = new Notifs(link, &app);
    auto *messages = new Messages(link, contacts, notifs, &app);
    auto *calls = new Calls(link, contacts, bt, &app);
    auto *transfers = new Transfers(link, &app);
    auto *clip = new Clipboard(link, &app);
    auto *mpris = new Mpris(phone, &app);
    new StateExport(link, phone, calls, messages, notifs, bt, &app);
    auto *glue = new App(link, phone, calls, messages, transfers, clip, bt, &app);

    QObject::connect(phone, &Phone::statusChanged, notifs, [phone, notifs] { notifs->smsPkg = phone->status().value("smsApp").toString(); });
    QObject::connect(bt, &Bluetooth::paired, calls, [calls] { calls->pushPrefs(); });
    QObject::connect(notifs, &Notifs::openChat, messages, [messages, glue](const QJsonObject &n) {
        messages->openForNotification(n);
        Q_EMIT glue->showPage("messages", {});
    });
    QObject::connect(notifs, &Notifs::openPage, glue, [glue](const QString &p) { Q_EMIT glue->showPage(p, {}); });
    QObject::connect(calls, &Calls::showSms, glue, [messages, glue](const QString &n) { messages->openNumber(n); Q_EMIT glue->showPage("messages", {}); });
    QObject::connect(mpris, &Mpris::raiseRequested, glue, [glue] { Q_EMIT glue->showPage("home", {}); });
    for (QObject *src : std::initializer_list<QObject *>{phone, contacts, notifs, messages, calls, transfers})
        QObject::connect(src, SIGNAL(toast(QString)), glue, SIGNAL(toast(QString)));

    QQmlApplicationEngine engine;
    link->setEngine(&engine);
    engine.addImageProvider("t", &Images::get());
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Link", link);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Phone", phone);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Contacts", contacts);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Notifs", notifs);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Messages", messages);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Calls", calls);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Transfers", transfers);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Bt", bt);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Clip", clip);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "App", glue);
    qmlRegisterSingletonInstance("Tandem", 1, 0, "Settings", &Config::get());
    qmlRegisterUncreatableType<ListModel>("Tandem", 1, 0, "ListModelCpp", "from C++");
    qmlRegisterSingletonType(QUrl("qrc:/qml/singletons/Theme.qml"), "Tandem", 1, 0, "Theme");
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;

    QObject::connect(&server, &QLocalServer::newConnection, glue, [&server, glue] {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, glue] {
                s->waitForReadyRead(50);
                QStringList lines = QString::fromUtf8(s->readAll()).split('\n');
                const QString cwd = lines.takeFirst();
                lines.removeAll(QString());
                glue->handleArgs(lines, cwd, false);
                s->deleteLater();
            });
        }
    });
    notifs->messagesVisible = [&engine] {
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        return w && w->isActive() && w->property("page").toString() == "messages";
    };
    glue->handleArgs(args, QDir::currentPath(), true);
    return app.exec();
}
