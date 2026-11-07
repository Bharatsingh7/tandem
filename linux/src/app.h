#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Link;
class Phone;
class Calls;
class Messages;
class Transfers;
class Clipboard;
class Bluetooth;

// Window-level glue for QML: navigation requests, toasts, autostart, and command-line actions.
class App : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
public:
    App(Link *link, Phone *phone, Calls *calls, Messages *messages, Transfers *transfers, Clipboard *clip, Bluetooth *bt, QObject *parent = nullptr);
    void handleArgs(const QStringList &args, const QString &cwd, bool firstRun);
    bool autostart() const;
    void setAutostart(bool on);
    Q_INVOKABLE void pairBluetooth();
    Q_INVOKABLE void quit();
    Q_INVOKABLE QString fileUrlToPath(const QUrl &u) const;

Q_SIGNALS:
    void showPage(const QString &page, const QVariantMap &args);
    void showCallWindow();
    void toast(const QString &text);
    void autostartChanged();

private:
    Link *m_link;
    Phone *m_phone;
    Calls *m_calls;
    Messages *m_messages;
    Transfers *m_transfers;
    Clipboard *m_clip;
    Bluetooth *m_bt;
    int m_pairTries = 0;
};
