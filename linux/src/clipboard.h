#pragma once
#include <QObject>
#include <QProcess>

class Link;

// Two-way clipboard on Wayland via wl-clipboard (an unfocused app can't watch the clipboard otherwise).
class Clipboard : public QObject {
    Q_OBJECT
public:
    explicit Clipboard(Link *link, QObject *parent = nullptr);
    Q_INVOKABLE QString sendNow();
    Q_INVOKABLE void setLocal(const QString &text);
private:
    void readAndPush();
    Link *m_link;
    QProcess m_watch;
    QString m_last;
};
