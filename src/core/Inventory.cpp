#include "core/Inventory.h"
#include <QJsonArray>
#include <QDateTime>

void Inventory::clear()
{
    QMutexLocker lock(&mutex_);
    devices_.clear();
    liveboxSummary.clear();
    decoSummary.clear();
    wanIpv4.clear();
    lastError.clear();
    logLines.clear();
}

void Inventory::beginScan(bool incremental)
{
    QMutexLocker lock(&mutex_);
    liveboxSummary.clear();
    decoSummary.clear();
    wanIpv4.clear();
    lastError.clear();
    logLines.clear();
    if (!incremental) {
        devices_.clear();
        return;
    }
    // Incremental : on garde ce qu'on sait (mac, nom, fabricant, modele, parent)
    // mais on efface toute preuve de presence de la passe precedente, sinon
    // classifyPresence croirait tout encore connecte. Ce qui n'est pas revu ce
    // scan-ci retombe donc en historique.
    for (Device& d : devices_) {
        d.online = false;
        d.sources.clear();
        d.services.clear();
        d.morfApps.clear();
        d.openPorts.clear();
        d.extra = {};
        d.presence = Device::Presence::Historical;
    }
}

void Inventory::loadJson(const QJsonObject& root)
{
    QMutexLocker lock(&mutex_);
    devices_.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("devices")).toArray())
        devices_.append(deviceFromJson(v.toObject()));
    wanIpv4 = root.value(QStringLiteral("wan_ipv4")).toString();
    lanCidr = root.value(QStringLiteral("lan_cidr")).toString();
    liveboxSummary = root.value(QStringLiteral("livebox")).toString();
    decoSummary = root.value(QStringLiteral("deco")).toString();
}

void Inventory::upsert(Device device)
{
    if (!device.lastSeen.isValid())
        device.lastSeen = QDateTime::currentDateTime();
    QMutexLocker lock(&mutex_);
    const QString id = device.id();
    const QString mac = normalizedMac(device.mac);
    for (Device& existing : devices_) {
        const QString emac = normalizedMac(existing.mac);
        const bool sameId = existing.id() == id;
        const bool sameMac = mac.size() >= 17 && emac == mac;
        const bool sameIp = !device.ip.isEmpty() && existing.ip == device.ip;
        // Deux MAC distinctes ne sont pas le meme appareil, meme IP DHCP recyclee :
        // fusionner les archives Livebox avec le present faisait disparaitre l'historique.
        const bool twoMacs = mac.size() >= 17 && emac.size() >= 17 && mac != emac;
        if (sameId || sameMac || (sameIp && !twoMacs)) {
            existing.mergeFrom(device);
            return;
        }
    }
    devices_.append(device);
}

void Inventory::replaceAll(QVector<Device> devices)
{
    QMutexLocker lock(&mutex_);
    devices_ = std::move(devices);
}

QVector<Device> Inventory::devices() const
{
    QMutexLocker lock(&mutex_);
    return devices_;
}

void Inventory::log(const QString& line)
{
    QMutexLocker lock(&mutex_);
    logLines.append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + line);
}

QJsonObject Inventory::toJson() const
{
    QMutexLocker lock(&mutex_);
    QJsonObject root;
    root["app"] = "LanAtlas";
    root["wan_ipv4"] = wanIpv4;
    root["lan_cidr"] = lanCidr;
    root["livebox"] = liveboxSummary;
    root["deco"] = decoSummary;
    QJsonArray arr;
    for (const Device& d : devices_)
        arr.append(d.toJson());
    root["devices"] = arr;
    root["device_count"] = devices_.size();
    return root;
}
