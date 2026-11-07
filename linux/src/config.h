#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

// ~/.config/tandem/config.json, shared with the earlier Python build so pairing carries over.
// adb isn't on the PATH systemd gives user services, so look in the usual install spots too.
QString adbBin();

class Config : public QObject {
    Q_OBJECT
public:
    static Config &get();
    static QString configDir();
    static QString dataDir();
    static QString certPath();
    static QString keyPath();
    void ensureIdentity();

    QVariant value(const QString &key, const QVariant &def = {}) const;
    void setValue(const QString &key, const QVariant &v);
    Q_INVOKABLE QVariant read(const QString &key) const { return value(key); }
    Q_INVOKABLE void write(const QString &key, const QVariant &v) { setValue(key, v); }

Q_SIGNALS:
    void changed(const QString &key);

private:
    Config();
    void save();
    QJsonObject m_obj;
};
