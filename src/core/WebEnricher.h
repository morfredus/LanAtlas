#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QJsonObject>

class Inventory;
class QNetworkAccessManager;
class QNetworkReply;

// Interroge des bases publiques (OUI/MAC, geo WAN). Pas un moteur de recherche.
class WebEnricher : public QObject {
    Q_OBJECT
public:
    explicit WebEnricher(QObject* parent = nullptr);

    void setInventory(Inventory* inventory);
    void enrichAfterScan(const QString& wanIpv4);
    void enrichMac(const QString& deviceId, const QString& mac);

signals:
    void deviceUpdated(const QString& deviceId);
    void wanUpdated();

private:
    void pump();
    void fetchMacLookup(const QString& deviceId, const QString& mac);
    void fetchMacVendors(const QString& deviceId, const QString& mac);
    void fetchWan(const QString& wanIp);
    void applyMacJson(const QString& deviceId, const QJsonObject& obj, const QString& source);
    void applyMacText(const QString& deviceId, const QString& vendor, const QString& source);

    // Cache disque OUI -> fabricant : evite de re-interroger le web a chaque scan.
    void loadCache();
    void saveCache() const;
    bool applyFromCache(const QString& deviceId, const QString& mac);
    void rememberVendor(const QString& mac, const QString& vendor);

    Inventory* inventory_ = nullptr;
    QNetworkAccessManager* http_ = nullptr;
    QStringList queue_; // "id|mac"
    bool busy_ = false;
    QString currentId_;
    QString currentMac_;
    bool triedFallback_ = false;
    QHash<QString, QString> ouiCache_;   // "DC:A6:32" -> "Raspberry Pi"
    QString cachePath_;
    bool cacheDirty_ = false;
};
