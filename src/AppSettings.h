#pragma once

#include <QString>
#include <QSettings>
#include <QMetaType>

struct AppSettings {
    QString liveboxHost = QStringLiteral("192.168.1.1");
    QString liveboxUser = QStringLiteral("admin");
    QString liveboxPassword;
    QString decoHost;          // vide = auto (premier nœud TP-Link / 192.168.68.1)
    QString decoPassword;
    int     pingTimeoutMs = 250;
    int     tcpTimeoutMs = 80;
    int     maxParallelPings = 64;
    bool    deepPortScan = true;

    static AppSettings load();
    void save() const;
    static QSettings store();

    // Local keys Tuya, stockees par identifiant d'appareil (jamais dans le git).
    // Necessaires pour piloter un appareil ; obtenues via le cloud Tuya.
    static QString tuyaLocalKey(const QString& deviceId);
    static void setTuyaLocalKey(const QString& deviceId, const QString& key);
};

Q_DECLARE_METATYPE(AppSettings)
