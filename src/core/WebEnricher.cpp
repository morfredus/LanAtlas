#include "core/WebEnricher.h"
#include "core/Device.h"
#include "core/Inventory.h"
#include "core/NetHints.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

static QString ouiOf(const QString& mac)
{
    const QString m = normalizedMac(mac);
    return m.size() >= 8 ? m.left(8).toUpper() : QString();
}

WebEnricher::WebEnricher(QObject* parent)
    : QObject(parent)
{
    http_ = new QNetworkAccessManager(this);
    loadCache();
}

void WebEnricher::loadCache()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    cachePath_ = dir + QStringLiteral("/oui-cache.json");
    QFile f(cachePath_);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        ouiCache_.insert(it.key(), it.value().toString());
}

void WebEnricher::saveCache() const
{
    if (cachePath_.isEmpty())
        return;
    QJsonObject o;
    for (auto it = ouiCache_.constBegin(); it != ouiCache_.constEnd(); ++it)
        o.insert(it.key(), it.value());
    QFile f(cachePath_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void WebEnricher::rememberVendor(const QString& mac, const QString& vendor)
{
    const QString oui = ouiOf(mac);
    if (oui.isEmpty() || vendor.isEmpty() || ouiCache_.value(oui) == vendor)
        return;
    ouiCache_.insert(oui, vendor);
    saveCache();
}

bool WebEnricher::applyFromCache(const QString& deviceId, const QString& mac)
{
    const QString oui = ouiOf(mac);
    if (oui.isEmpty() || !inventory_)
        return false;
    const QString vendor = ouiCache_.value(oui);
    if (vendor.isEmpty())
        return false;
    for (Device d : inventory_->devices()) {
        if (d.id() != deviceId)
            continue;
        if (d.vendor.isEmpty() || d.vendor.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0
            || d.vendor == QLatin1String("MAC aleatoire"))
            d.vendor = vendor;
        d.extra.insert(QStringLiteral("web_maclookup"),
                       QJsonObject{{QStringLiteral("company"), vendor}, {QStringLiteral("cache"), true}});
        if (!d.sources.contains(QLatin1String("web")))
            d.sources << QStringLiteral("web");
        inventory_->upsert(d);
        emit deviceUpdated(deviceId);
        return true;
    }
    return false;
}

void WebEnricher::setInventory(Inventory* inventory)
{
    inventory_ = inventory;
}

void WebEnricher::enrichAfterScan(const QString& wanIpv4)
{
    if (!inventory_)
        return;
    if (!wanIpv4.isEmpty()) {
        const QString kind = NetHints::ipKind(wanIpv4);
        if (!kind.contains(QLatin1String("prive")) && !kind.contains(QLatin1String("loopback"))
            && !kind.contains(QLatin1String("lien-local")))
            fetchWan(wanIpv4);
    }

    int n = 0;
    for (const Device& d : inventory_->devices()) {
        const QString mac = normalizedMac(d.mac);
        if (mac.size() < 8)
            continue;
        if (d.extra.contains(QStringLiteral("web_maclookup"))
            || d.extra.contains(QStringLiteral("web_macvendors")))
            continue;
        // Cache disque : si l'OUI est deja connu, pas de requete web.
        if (applyFromCache(d.id(), mac))
            continue;
        const QString token = d.id() + QLatin1Char('|') + mac;
        if (!queue_.contains(token))
            queue_.append(token);
        if (++n >= 40)
            break;
    }
    pump();
}

void WebEnricher::enrichMac(const QString& deviceId, const QString& mac)
{
    const QString m = normalizedMac(mac);
    if (deviceId.isEmpty() || m.size() < 8 || !inventory_)
        return;
    for (const Device& d : inventory_->devices()) {
        if (d.id() == deviceId
            && (d.extra.contains(QStringLiteral("web_maclookup"))
                || d.extra.contains(QStringLiteral("web_macvendors"))))
            return;
    }
    if (applyFromCache(deviceId, m))
        return;
    const QString token = deviceId + QLatin1Char('|') + m;
    if (!queue_.contains(token))
        queue_.prepend(token);
    pump();
}

void WebEnricher::pump()
{
    if (busy_ || queue_.isEmpty())
        return;
    const QString token = queue_.takeFirst();
    const int bar = token.indexOf(QLatin1Char('|'));
    currentId_ = token.left(bar);
    currentMac_ = token.mid(bar + 1);
    triedFallback_ = false;
    busy_ = true;
    fetchMacLookup(currentId_, currentMac_);
}

void WebEnricher::fetchMacLookup(const QString& deviceId, const QString& mac)
{
    Q_UNUSED(deviceId);
    const QString compact = QString(normalizedMac(mac)).remove(QLatin1Char(':'));
    QUrl url(QStringLiteral("https://api.maclookup.app/v2/macs/") + compact);
    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setRawHeader("User-Agent", "LanAtlas/0.2 (morfsystem; OUI lookup)");
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = http_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            if (o.value(QStringLiteral("success")).toBool()
                && o.value(QStringLiteral("found")).toBool()) {
                applyMacJson(currentId_, o, QStringLiteral("maclookup.app"));
                busy_ = false;
                QTimer::singleShot(250, this, &WebEnricher::pump);
                return;
            }
        }
        if (!triedFallback_) {
            triedFallback_ = true;
            fetchMacVendors(currentId_, currentMac_);
            return;
        }
        busy_ = false;
        QTimer::singleShot(250, this, &WebEnricher::pump);
    });
}

void WebEnricher::fetchMacVendors(const QString& deviceId, const QString& mac)
{
    Q_UNUSED(deviceId);
    QUrl url(QStringLiteral("https://api.macvendors.com/") + QUrl::toPercentEncoding(normalizedMac(mac)));
    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setRawHeader("User-Agent", "LanAtlas/0.2 (morfsystem; OUI lookup)");
    QNetworkReply* reply = http_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll().trimmed();
        if (reply->error() == QNetworkReply::NoError && !body.isEmpty()
            && !body.startsWith('{') && !QString::fromUtf8(body).contains(QLatin1String("errors")))
            applyMacText(currentId_, QString::fromUtf8(body), QStringLiteral("macvendors.com"));
        busy_ = false;
        QTimer::singleShot(250, this, &WebEnricher::pump);
    });
}

void WebEnricher::fetchWan(const QString& wanIp)
{
    QUrl url(QStringLiteral("http://ip-api.com/json/") + wanIp);
    url.setQuery(QStringLiteral("fields=status,country,regionName,city,isp,org,as,query"));
    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setRawHeader("User-Agent", "LanAtlas/0.2 (morfsystem; WAN geo)");
    QNetworkReply* reply = http_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!inventory_ || reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value(QStringLiteral("status")).toString() != QLatin1String("success"))
            return;
        for (Device d : inventory_->devices()) {
            if (d.category != QLatin1String("box"))
                continue;
            d.extra.insert(QStringLiteral("web_wan"), o);
            if (!d.sources.contains(QLatin1String("web")))
                d.sources << QStringLiteral("web");
            inventory_->upsert(d);
        }
        inventory_->log(QStringLiteral("WAN (ip-api) : ")
                        + o.value(QStringLiteral("isp")).toString() + QStringLiteral(" / ")
                        + o.value(QStringLiteral("country")).toString());
        emit wanUpdated();
    });
}

void WebEnricher::applyMacJson(const QString& deviceId, const QJsonObject& obj, const QString& source)
{
    if (!inventory_)
        return;
    for (Device d : inventory_->devices()) {
        if (d.id() != deviceId)
            continue;
        d.extra.insert(QStringLiteral("web_maclookup"), obj);
        const QString company = obj.value(QStringLiteral("company")).toString();
        if (!company.isEmpty()
            && (d.vendor.isEmpty()
                || d.vendor.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0
                || d.vendor == QLatin1String("MAC aleatoire")))
            d.vendor = company;
        if (!d.sources.contains(QLatin1String("web")))
            d.sources << QStringLiteral("web");
        d.extra.insert(QStringLiteral("web_source"), source);
        inventory_->upsert(d);
        rememberVendor(currentMac_, company);
        emit deviceUpdated(deviceId);
        return;
    }
}

void WebEnricher::applyMacText(const QString& deviceId, const QString& vendor, const QString& source)
{
    if (!inventory_)
        return;
    for (Device d : inventory_->devices()) {
        if (d.id() != deviceId)
            continue;
        d.extra.insert(QStringLiteral("web_macvendors"), vendor);
        if (!vendor.isEmpty()
            && (d.vendor.isEmpty() || d.vendor.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0
                || d.vendor == QLatin1String("MAC aleatoire")))
            d.vendor = vendor;
        if (!d.sources.contains(QLatin1String("web")))
            d.sources << QStringLiteral("web");
        d.extra.insert(QStringLiteral("web_source"), source);
        inventory_->upsert(d);
        rememberVendor(currentMac_, vendor);
        emit deviceUpdated(deviceId);
        return;
    }
}
