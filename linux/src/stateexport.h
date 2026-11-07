#pragma once
#include <QObject>
#include <QTimer>

class Link;
class Phone;
class Calls;
class Messages;
class Notifs;
class Bluetooth;

// Publishes a small live snapshot to $XDG_RUNTIME_DIR/tandem/state.json (owner-only) for shell widgets
// such as the Quickshell notch. Writes are coalesced; the file is removed on exit.
class StateExport : public QObject {
    Q_OBJECT
public:
    StateExport(Link *link, Phone *phone, Calls *calls, Messages *messages, Notifs *notifs, Bluetooth *bt, QObject *parent = nullptr);
    ~StateExport() override;
    static QString dir();

private:
    void write();
    Link *m_link;
    Phone *m_phone;
    Calls *m_calls;
    Messages *m_messages;
    Notifs *m_notifs;
    Bluetooth *m_bt;
    QTimer m_debounce, m_heartbeat;
    int m_artRev = -1;
};
