#include "adapters/LiveboxClient.h"
#include "scan/HttpUtil.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QUrlQuery>

static Device hostFromLiveboxJson(const QJsonObject& o)
{
    Device d;
    d.extra = o;
    d.ip = o.value(QStringLiteral("IPAddress")).toString();
    if (d.ip.isEmpty())
        d.ip = o.value(QStringLiteral("ipaddress")).toString();
    d.mac = o.value(QStringLiteral("PhysAddress")).toString();
    if (d.mac.isEmpty())
        d.mac = o.value(QStringLiteral("physAddress")).toString();
    d.hostname = o.value(QStringLiteral("HostName")).toString();
    if (d.hostname.isEmpty())
        d.hostname = o.value(QStringLiteral("UserFriendlyName")).toString();
    if (d.hostname.isEmpty())
        d.hostname = o.value(QStringLiteral("name")).toString();
    d.vendor = o.value(QStringLiteral("Manufacturer")).toString();
    d.model = o.value(QStringLiteral("ProductClass")).toString();
    if (d.model.isEmpty())
        d.model = o.value(QStringLiteral("DeviceType")).toString();
    const QString iface = o.value(QStringLiteral("InterfaceName")).toString()
                          + QLatin1Char(' ')
                          + o.value(QStringLiteral("Layer2Interface")).toString();
    d.extra.insert(QStringLiteral("livebox_interface"), iface.trimmed());
    if (iface.contains(QLatin1String("ETH"), Qt::CaseInsensitive)
        || iface.contains(QLatin1String("lan"), Qt::CaseInsensitive))
        d.connection = QStringLiteral("ethernet");
    else if (iface.contains(QLatin1String("SSID"), Qt::CaseInsensitive)
             || iface.contains(QLatin1String("wifi"), Qt::CaseInsensitive)
             || iface.contains(QLatin1String("wlan"), Qt::CaseInsensitive))
        d.connection = QStringLiteral("wifi");
    d.ssid = o.value(QStringLiteral("SSID")).toString();
    // Absent ou false : historique. Un defaut "true" faisait passer un Roomba
    // (ou n'importe quel hote archive) pour encore present.
    d.online = jsonFlag(o.value(QStringLiteral("Active")),
                        jsonFlag(o.value(QStringLiteral("active")), false));
    const QString lastConn = o.value(QStringLiteral("LastConnection")).toString();
    if (lastConn.isEmpty())
        d.lastSeen = QDateTime();
    else
        d.lastSeen = QDateTime::fromString(lastConn, Qt::ISODate);
    if (!d.lastSeen.isValid() && !lastConn.isEmpty())
        d.lastSeen = QDateTime::fromString(lastConn, Qt::ISODateWithMs);
    d.sources << QStringLiteral("livebox");
    // Wi-Fi Livebox : ne pas figer le parent, le mesh Deco est plus precis.
    if (d.connection == QLatin1String("ethernet"))
        d.parentName = QStringLiteral("Livebox");
    return d;
}

static bool trySahLogin(HttpClient& http, const QUrl& base, const AppSettings& s, QString* context)
{
    const QJsonObject params{{QStringLiteral("applicationName"), QStringLiteral("webui")},
                             {QStringLiteral("username"), s.liveboxUser},
                             {QStringLiteral("password"), s.liveboxPassword}};
    const QJsonObject body{{QStringLiteral("service"), QStringLiteral("sah.Device.Information")},
                           {QStringLiteral("method"), QStringLiteral("createContext")},
                           {QStringLiteral("parameters"), params}};
    QUrl url(base);
    url.setPath(QStringLiteral("/ws"));
    auto r = http.post(url, QJsonDocument(body).toJson(QJsonDocument::Compact),
                       "application/x-sah-ws-4-call+json");
    const auto obj = QJsonDocument::fromJson(r.body).object();
    const auto data = obj.value(QStringLiteral("data")).toObject();
    const QString ctx = data.value(QStringLiteral("contextID")).toString();
    if (ctx.isEmpty())
        return false;
    *context = ctx;
    http.setHeader("Authorization", QByteArray("X-Sah ") + ctx.toUtf8());
    http.setHeader("X-Context", ctx.toUtf8());
    return true;
}

static bool tryAuthenticate(HttpClient& http, const QUrl& base, const AppSettings& s, QString* context)
{
    QUrl url(base);
    url.setPath(QStringLiteral("/authenticate"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("username"), s.liveboxUser);
    q.addQueryItem(QStringLiteral("password"), s.liveboxPassword);
    url.setQuery(q);
    auto r = http.post(url, QByteArray(), "application/x-sah-ws-4-call+json");
    const auto obj = QJsonDocument::fromJson(r.body).object();
    const QString ctx = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("contextID")).toString();
    if (ctx.isEmpty())
        return false;
    *context = ctx;
    http.setHeader("Authorization", QByteArray("X-Sah ") + ctx.toUtf8());
    return true;
}

static QJsonObject sahCall(HttpClient& http, const QUrl& base, const QString& service,
                           const QString& method)
{
    QUrl url(base);
    url.setPath(QStringLiteral("/ws"));
    const QJsonObject body{{QStringLiteral("service"), service},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("parameters"), QJsonObject{}}};
    auto r = http.post(url, QJsonDocument(body).toJson(QJsonDocument::Compact),
                       "application/x-sah-ws-4-call+json");
    return QJsonDocument::fromJson(r.body).object();
}

QVector<Device> queryLivebox(const AppSettings& settings, Inventory& inventory, QString* boxIpOut)
{
    QVector<Device> out;
    if (settings.liveboxPassword.isEmpty()) {
        inventory.log(QStringLiteral("Livebox : mot de passe non renseigne (Parametres)."));
        return out;
    }

    QUrl base;
    base.setScheme(QStringLiteral("http"));
    base.setHost(settings.liveboxHost);

    HttpClient http;
    http.setHeader("Authorization", "X-Sah-Login");
    QString ctx;
    if (!trySahLogin(http, base, settings, &ctx) && !tryAuthenticate(http, base, settings, &ctx)) {
        inventory.log(QStringLiteral("Livebox : authentification refusee sur ") + settings.liveboxHost);
        inventory.lastError = QStringLiteral("Livebox login failed");
        return out;
    }
    inventory.log(QStringLiteral("Livebox : session ouverte."));

    Device box;
    box.ip = settings.liveboxHost;
    box.category = QStringLiteral("box");
    box.vendor = QStringLiteral("Orange");
    box.model = QStringLiteral("Livebox");
    box.infrastructure = true;
    box.hostname = QStringLiteral("livebox");
    box.sources << QStringLiteral("livebox");
    box.online = true;
    if (boxIpOut)
        *boxIpOut = box.ip;

    const auto info = sahCall(http, base, QStringLiteral("DeviceInfo"), QStringLiteral("get"));
    const auto infoStatus = info.value(QStringLiteral("status")).toObject();
    if (!infoStatus.isEmpty()) {
        box.model = infoStatus.value(QStringLiteral("ProductClass")).toString(box.model);
        box.hostname = infoStatus.value(QStringLiteral("DeviceName")).toString(box.hostname);
        box.notes = infoStatus.value(QStringLiteral("SoftwareVersion")).toString();
        box.extra = infoStatus;
        box.mac = infoStatus.value(QStringLiteral("MACAddress")).toString(box.mac);
        inventory.liveboxSummary = box.model + "  fw " + box.notes;
        inventory.wanIpv4 = infoStatus.value(QStringLiteral("ExternalIPAddress")).toString();
    }
    const auto wan = sahCall(http, base, QStringLiteral("NMC"), QStringLiteral("getWANStatus"));
    const auto wanData = wan.value(QStringLiteral("data")).toObject();
    if (wanData.contains(QStringLiteral("IPAddress")))
        inventory.wanIpv4 = wanData.value(QStringLiteral("IPAddress")).toString();
    out.append(box);

    // Devices.get = souvent les presents. Hosts.getDevices = le carnet
    // (y compris eteints). On fusionne les deux, sans ecraser un Active false.
    QMap<QString, QJsonObject> byKey;
    auto ingest = [&](const QJsonArray& arr) {
        for (const auto& h : arr) {
            const QJsonObject o = h.toObject();
            QString mac = o.value(QStringLiteral("PhysAddress")).toString();
            if (mac.isEmpty())
                mac = o.value(QStringLiteral("physAddress")).toString();
            QString key = normalizedMac(mac);
            if (key.size() < 17)
                key = o.value(QStringLiteral("IPAddress")).toString();
            if (key.isEmpty())
                key = o.value(QStringLiteral("ipaddress")).toString();
            if (key.isEmpty())
                continue;
            if (!byKey.contains(key)) {
                byKey.insert(key, o);
                continue;
            }
            QJsonObject& dest = byKey[key];
            const bool destOn = jsonFlag(dest.value(QStringLiteral("Active")),
                                         jsonFlag(dest.value(QStringLiteral("active")), false));
            const bool srcOn = jsonFlag(o.value(QStringLiteral("Active")),
                                        jsonFlag(o.value(QStringLiteral("active")), false));
            if (srcOn && !destOn) {
                dest = o;
                continue;
            }
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (!dest.contains(it.key()))
                    dest.insert(it.key(), it.value());
            }
        }
    };
    const auto devices = sahCall(http, base, QStringLiteral("Devices"), QStringLiteral("get"));
    if (devices.value(QStringLiteral("status")).isArray())
        ingest(devices.value(QStringLiteral("status")).toArray());
    const auto hosts2 = sahCall(http, base, QStringLiteral("Hosts"), QStringLiteral("getDevices"));
    if (hosts2.value(QStringLiteral("status")).isArray())
        ingest(hosts2.value(QStringLiteral("status")).toArray());
    const auto hosts3 = sahCall(http, base, QStringLiteral("Hosts"), QStringLiteral("get"));
    if (hosts3.value(QStringLiteral("status")).isArray())
        ingest(hosts3.value(QStringLiteral("status")).toArray());

    int nActive = 0;
    int nInactive = 0;
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it) {
        const bool on = jsonFlag(it.value().value(QStringLiteral("Active")),
                                 jsonFlag(it.value().value(QStringLiteral("active")), false));
        if (on)
            ++nActive;
        else
            ++nInactive;
    }
    inventory.log(QStringLiteral("Livebox : %1 hote(s) declares (%2 actif(s), %3 archive(s)).")
                      .arg(byKey.size())
                      .arg(nActive)
                      .arg(nInactive));
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it) {
        Device d = hostFromLiveboxJson(it.value());
        if (d.connection == QLatin1String("ethernet")) {
            d.parentId = box.id();
            d.parentName = box.displayName();
        }
        if (d.ip == box.ip)
            continue;
        out.append(d);
    }
    return out;
}
