#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {
const auto kOrg = QStringLiteral("morfredus");
const auto kApp = QStringLiteral("LanAtlas");

// Fichier de reglages, range dans le MEME dossier que l'etat de session
// (session.json, labels.json) : un seul dossier par appli, a l'emplacement
// standard du systeme.
//   Windows : C:\Users\<user>\AppData\Roaming\morfredus\LanAtlas\settings.ini
//   Linux   : ~/.local/share/morfredus/LanAtlas/settings.ini
QString settingsFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/settings.ini");
}

// Migration unique vers ce fichier depuis les anciens emplacements : le format
// natif (registre Windows / ancien .conf Linux) et l'INI intermediaire par
// portee utilisateur. On recopie les cles existantes pour ne rien perdre (local
// key Tuya, mots de passe Livebox/Deco...), puis on efface les anciens stockages
// pour ne laisser aucun secret en double.
bool ensureMigrated()
{
    QSettings target(settingsFilePath(), QSettings::IniFormat);
    if (!target.allKeys().isEmpty())
        return true;   // deja migre

    QSettings interIni(QSettings::IniFormat, QSettings::UserScope, kOrg, kApp);
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, kOrg, kApp);
    QSettings* src = nullptr;
    if (!interIni.allKeys().isEmpty())
        src = &interIni;
    else if (!native.allKeys().isEmpty())
        src = &native;

    if (src) {
        for (const QString& k : src->allKeys())
            target.setValue(k, src->value(k));
        target.sync();
    }

    // Nettoyer les anciens emplacements (registre + INI intermediaire).
    const QString interFile = interIni.fileName();
    interIni.clear();
    interIni.sync();
    native.clear();
    native.sync();
    QFile::remove(interFile);   // supprimer le fichier INI intermediaire devenu vide
    return true;
}
} // namespace

QSettings AppSettings::store()
{
    // Une seule fois par processus : recuperer les anciens reglages si besoin.
    static const bool migrated = ensureMigrated();
    Q_UNUSED(migrated);
    return QSettings(settingsFilePath(), QSettings::IniFormat);
}

AppSettings AppSettings::load()
{
    QSettings s = store();
    AppSettings a;
    a.liveboxHost = s.value("livebox/host", a.liveboxHost).toString();
    a.liveboxUser = s.value("livebox/user", a.liveboxUser).toString();
    a.liveboxPassword = s.value("livebox/password").toString();
    a.decoHost = s.value("deco/host").toString();
    a.decoPassword = s.value("deco/password").toString();
    a.pingTimeoutMs = s.value("scan/pingTimeoutMs", a.pingTimeoutMs).toInt();
    a.tcpTimeoutMs = s.value("scan/tcpTimeoutMs", a.tcpTimeoutMs).toInt();
    a.maxParallelPings = s.value("scan/maxParallelPings", a.maxParallelPings).toInt();
    a.deepPortScan = s.value("scan/deepPortScan", true).toBool();
    return a;
}

void AppSettings::save() const
{
    QSettings s = store();
    s.setValue("livebox/host", liveboxHost);
    s.setValue("livebox/user", liveboxUser);
    s.setValue("livebox/password", liveboxPassword);
    s.setValue("deco/host", decoHost);
    s.setValue("deco/password", decoPassword);
    s.setValue("scan/pingTimeoutMs", pingTimeoutMs);
    s.setValue("scan/tcpTimeoutMs", tcpTimeoutMs);
    s.setValue("scan/maxParallelPings", maxParallelPings);
    s.setValue("scan/deepPortScan", deepPortScan);
}

QString AppSettings::tuyaLocalKey(const QString& deviceId)
{
    if (deviceId.isEmpty())
        return {};
    QSettings s = store();
    return s.value(QStringLiteral("tuya/keys/") + deviceId).toString();
}

void AppSettings::setTuyaLocalKey(const QString& deviceId, const QString& key)
{
    if (deviceId.isEmpty())
        return;
    QSettings s = store();
    const QString path = QStringLiteral("tuya/keys/") + deviceId;
    if (key.isEmpty())
        s.remove(path);
    else
        s.setValue(path, key);
}
