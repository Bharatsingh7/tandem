#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

static QJsonObject defaults() {
    return QJsonObject{
        {"notify_mirror", true},
        {"clipboard_sync", true},
        {"call_audio_on_pc", true},
        {"pause_media_on_call", true},
        {"phone_media_on_pc", false},
        {"downloads", QDir::homePath() + "/Downloads/Tandem"},
        {"quick_replies", QJsonArray{"Busy right now, I'll call you back.", "On my way.", "Can't talk, text me?"}},
        {"muted_apps", QJsonArray{}},
    };
}

Config &Config::get() {
    static Config c;
    return c;
}

QString Config::configDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/tandem";
}
QString Config::dataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/tandem";
}
QString Config::certPath() { return configDir() + "/cert.pem"; }
QString Config::keyPath() { return configDir() + "/key.pem"; }

Config::Config() {
    QDir().mkpath(configDir());
    QDir().mkpath(dataDir());
    // Keys, pins, messages and contacts: readable by this user only.
    const auto dirPerms = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QFile::setPermissions(configDir(), dirPerms);
    QFile::setPermissions(dataDir(), dirPerms);
    for (const QString &f : {configDir() + "/config.json", configDir() + "/cert.pem", keyPath(), dataDir() + "/tandem.db",
                             dataDir() + "/tandem.db-wal", dataDir() + "/tandem.db-shm"})
        if (QFile::exists(f)) QFile::setPermissions(f, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_obj = defaults();
    QFile f(configDir() + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        const auto saved = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = saved.begin(); it != saved.end(); ++it) m_obj[it.key()] = it.value();
    }
}

void Config::ensureIdentity() {
    if (QFile::exists(certPath()) && QFile::exists(keyPath())) return;
    QProcess::execute("openssl", {"req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes",
                                  "-keyout", keyPath(), "-out", certPath(), "-days", "36500", "-subj", "/CN=Tandem PC"});
    QFile::setPermissions(keyPath(), QFile::ReadOwner | QFile::WriteOwner);
}

QVariant Config::value(const QString &key, const QVariant &def) const {
    const auto v = m_obj.value(key);
    return v.isUndefined() || v.isNull() ? def : v.toVariant();
}

void Config::setValue(const QString &key, const QVariant &v) {
    m_obj[key] = QJsonValue::fromVariant(v);
    save();
    Q_EMIT changed(key);
}

void Config::save() {
    QFile f(configDir() + "/config.json.tmp");
    if (!f.open(QIODevice::WriteOnly)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(m_obj).toJson());
    f.close();
    QFile::remove(configDir() + "/config.json");
    f.rename(configDir() + "/config.json");
}

QString adbBin() {
    static const QString path = [] {
        QString p = QStandardPaths::findExecutable("adb");
        if (p.isEmpty())
            p = QStandardPaths::findExecutable("adb", {QDir::homePath() + "/.local/bin", QDir::homePath() + "/Android/Sdk/platform-tools",
                                                       "/opt/android-sdk/platform-tools", "/usr/bin"});
        return p.isEmpty() ? QString("adb") : p;
    }();
    return path;
}
