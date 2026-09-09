#include "core/Device.h"
#include "core/NetHints.h"

#include <QJsonArray>
#include <QJsonDocument>

QString normalizedMac(QString mac)
{
    mac = mac.trimmed().toUpper();
    mac.replace('-', ':');
    mac.replace('.', ':');
    return mac;
}

bool jsonFlag(const QJsonValue& v, bool fallback)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toInt() != 0;
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s.isEmpty())
            return fallback;
        if (s == QLatin1String("0") || s == QLatin1String("false") || s == QLatin1String("no")
            || s == QLatin1String("off") || s == QLatin1String("offline")
            || s == QLatin1String("inactive") || s == QLatin1String("n"))
            return false;
        if (s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes")
            || s == QLatin1String("on") || s == QLatin1String("online")
            || s == QLatin1String("active") || s == QLatin1String("y"))
            return true;
    }
    return fallback;
}

static int nameScore(const QString& n, const QString& ip)
{
    if (n.isEmpty() || n == ip)
        return 0;
    if (n.contains(':') && n.size() >= 17)
        return 1;
    return 10 + n.size();
}

bool isRandomizedMac(const QString& mac)
{
    const QString m = normalizedMac(mac);
    if (m.size() < 2)
        return false;
    bool ok = false;
    const int first = m.left(2).toInt(&ok, 16);
    return ok && (first & 0x02) != 0;
}

QString Device::id() const
{
    const QString m = normalizedMac(mac);
    if (m.size() >= 17)
        return m;
    if (!deviceId.isEmpty())
        return deviceId;
    return ip;
}

QString Device::typeEmoji() const
{
    const QString blob = (category + QLatin1Char(' ') + model + QLatin1Char(' ')
                          + capabilities.join(QLatin1Char(' ')) + QLatin1Char(' ') + notes)
                             .toLower();
    if (category == QLatin1String("box"))
        return QString::fromUtf8("\xF0\x9F\x8C\x90");        // globe (passerelle)
    if (category == QLatin1String("mesh_node"))
        return QString::fromUtf8("\xF0\x9F\x93\xA1");        // antenne (Deco)
    if (blob.contains(QLatin1String("telephone")) || blob.contains(QLatin1String("phone"))
        || blob.contains(QLatin1String("iphone")) || blob.contains(QLatin1String("mobile")))
        return QString::fromUtf8("\xF0\x9F\x93\xB1");        // telephone
    if (blob.contains(QLatin1String("tablet")) || blob.contains(QLatin1String("ipad")))
        return QString::fromUtf8("\xF0\x9F\x93\x9F");        // tablette-ish
    if (category == QLatin1String("printer") || blob.contains(QLatin1String("imprimante"))
        || blob.contains(QLatin1String("printer")))
        return QString::fromUtf8("\xF0\x9F\x96\xA8");        // imprimante
    if (category == QLatin1String("media") || blob.contains(QLatin1String("tv"))
        || blob.contains(QLatin1String("chromecast")) || blob.contains(QLatin1String("airplay")))
        return QString::fromUtf8("\xF0\x9F\x93\xBA");        // TV
    if (blob.contains(QLatin1String("enceinte")) || blob.contains(QLatin1String("sonos"))
        || blob.contains(QLatin1String("speaker")) || blob.contains(QLatin1String("homepod")))
        return QString::fromUtf8("\xF0\x9F\x94\x8A");        // enceinte
    if (blob.contains(QLatin1String("camera")) || blob.contains(QLatin1String("cam")))
        return QString::fromUtf8("\xF0\x9F\x93\xB7");        // camera
    if (category == QLatin1String("nas") || blob.contains(QLatin1String("nas"))
        || blob.contains(QLatin1String("stockage")))
        return QString::fromUtf8("\xF0\x9F\x92\xBE");        // disquette (NAS)
    if (blob.contains(QLatin1String("aspirateur")) || blob.contains(QLatin1String("cleaner")))
        return QString::fromUtf8("\xF0\x9F\xA7\xB9");        // balai
    if (category == QLatin1String("iot") || blob.contains(QLatin1String("objet connecte"))
        || blob.contains(QLatin1String("homekit")) || blob.contains(QLatin1String("hue")))
        return QString::fromUtf8("\xF0\x9F\x92\xA1");        // ampoule (IoT)
    if (category == QLatin1String("computer") || blob.contains(QLatin1String("ordinateur"))
        || blob.contains(QLatin1String("computer")) || blob.contains(QLatin1String("pc")))
        return QString::fromUtf8("\xF0\x9F\x92\xBB");        // ordinateur portable
    if (!morfApps.isEmpty())
        return QString::fromUtf8("\xE2\x9A\x99");            // engrenage (service morf)
    return QString::fromUtf8("\xF0\x9F\x94\x8C");            // prise (appareil generique)
}

QString Device::displayName() const
{
    if (!customName.isEmpty())
        return customName;
    if (nameScore(hostname, ip) >= 10)
        return hostname;
    if (!model.isEmpty() && !vendor.isEmpty())
        return vendor + QLatin1Char(' ') + model;
    if (!model.isEmpty())
        return model;
    if (!vendor.isEmpty())
        return vendor;
    const QString m = normalizedMac(mac);
    if (m.size() >= 17)
        return QStringLiteral("Inconnu ") + m.right(8);
    if (!ip.isEmpty())
        return ip;
    return QStringLiteral("Inconnu");
}

QString Device::presenceLabel() const
{
    switch (presence) {
    case Presence::Connected:
        return QStringLiteral("connecte");
    case Presence::Technical:
        return QStringLiteral("technique");
    case Presence::Historical:
    default:
        return QStringLiteral("historique");
    }
}

QString Device::proofShort() const
{
    if (sources.contains(QLatin1String("morfbeacon")))
        return QStringLiteral("beacon");
    if (sources.contains(QLatin1String("morfhttp")))
        return QStringLiteral("/status");
    if (!morfApps.isEmpty())
        return QStringLiteral("morf");
    return QStringLiteral("-");
}

QString Device::linkShort() const
{
    QString s = connection;
    if (!band.isEmpty() && !s.contains(band, Qt::CaseInsensitive))
        s += (s.isEmpty() ? QString() : QStringLiteral(" ")) + band;
    if (rssi != 0)
        s += QStringLiteral(" %1dBm").arg(rssi);
    return s.trimmed();
}

QJsonObject Device::toJson() const
{
    QJsonObject o;
    o["id"] = id();
    o["display_name"] = displayName();
    o["ip"] = ip;
    o["mac"] = normalizedMac(mac);
    o["hostname"] = hostname;
    o["vendor"] = vendor;
    o["model"] = model;
    o["category"] = category;
    o["os_guess"] = osGuess;
    o["parent_id"] = parentId;
    o["parent_name"] = parentName;
    o["connection"] = connection;
    o["band"] = band;
    o["ssid"] = ssid;
    o["device_id"] = deviceId;
    o["rssi"] = rssi;
    o["online"] = online;
    o["presence"] = presenceLabel();
    o["infrastructure"] = infrastructure;
    o["notes"] = notes;
    o["custom_name"] = customName;
    o["last_seen"] = lastSeen.toString(Qt::ISODate);
    QJsonArray src;
    for (const QString& s : sources)
        src.append(s);
    o["sources"] = src;
    QJsonArray svc;
    for (const QString& s : services)
        svc.append(s);
    o["services"] = svc;
    QJsonArray caps;
    for (const QString& s : capabilities)
        caps.append(s);
    o["capabilities"] = caps;
    QJsonArray apps;
    for (const QString& s : morfApps)
        apps.append(s);
    o["morf_apps"] = apps;
    QJsonArray ports;
    for (int p : openPorts)
        ports.append(p);
    o["open_ports"] = ports;
    if (!extra.isEmpty())
        o["extra"] = extra;
    return o;
}

Device deviceFromJson(const QJsonObject& o)
{
    Device d;
    d.ip = o.value(QStringLiteral("ip")).toString();
    d.mac = o.value(QStringLiteral("mac")).toString();
    d.hostname = o.value(QStringLiteral("hostname")).toString();
    d.vendor = o.value(QStringLiteral("vendor")).toString();
    d.model = o.value(QStringLiteral("model")).toString();
    d.category = o.value(QStringLiteral("category")).toString();
    d.osGuess = o.value(QStringLiteral("os_guess")).toString();
    d.parentId = o.value(QStringLiteral("parent_id")).toString();
    d.parentName = o.value(QStringLiteral("parent_name")).toString();
    d.connection = o.value(QStringLiteral("connection")).toString();
    d.band = o.value(QStringLiteral("band")).toString();
    d.ssid = o.value(QStringLiteral("ssid")).toString();
    d.deviceId = o.value(QStringLiteral("device_id")).toString();
    d.rssi = o.value(QStringLiteral("rssi")).toInt();
    d.online = o.value(QStringLiteral("online")).toBool();
    d.infrastructure = o.value(QStringLiteral("infrastructure")).toBool();
    d.notes = o.value(QStringLiteral("notes")).toString();
    d.customName = o.value(QStringLiteral("custom_name")).toString();
    d.lastSeen = QDateTime::fromString(o.value(QStringLiteral("last_seen")).toString(), Qt::ISODate);
    const QString pres = o.value(QStringLiteral("presence")).toString();
    if (pres == QLatin1String("connecte"))
        d.presence = Device::Presence::Connected;
    else if (pres == QLatin1String("technique"))
        d.presence = Device::Presence::Technical;
    else
        d.presence = Device::Presence::Historical;
    auto strs = [&](const char* key, QStringList& dst) {
        for (const QJsonValue& v : o.value(QLatin1String(key)).toArray())
            dst << v.toString();
    };
    strs("sources", d.sources);
    strs("services", d.services);
    strs("capabilities", d.capabilities);
    strs("morf_apps", d.morfApps);
    for (const QJsonValue& v : o.value(QStringLiteral("open_ports")).toArray())
        d.openPorts << v.toInt();
    d.extra = o.value(QStringLiteral("extra")).toObject();
    return d;
}

QString Device::detailText() const
{
    QStringList lines;
    auto row = [&](const QString& k, const QString& v) {
        if (!v.isEmpty())
            lines << k + QStringLiteral(" : ") + v;
    };
    row(QStringLiteral("Nom affiche"), displayName());
    row(QStringLiteral("Hostname brut"), hostname);
    row(QStringLiteral("Adresse IP"), ip);
    row(QStringLiteral("Adresse MAC"), normalizedMac(mac));
    row(QStringLiteral("Presence"), presenceLabel());
    row(QStringLiteral("En ligne (preuve actuelle)"), online ? QStringLiteral("oui") : QStringLiteral("non"));
    row(QStringLiteral("Categorie"), category);
    row(QStringLiteral("Fabricant"), vendor);
    row(QStringLiteral("Modele"), model);
    row(QStringLiteral("OS (indice)"), osGuess);
    row(QStringLiteral("Rattache a"), parentName);
    row(QStringLiteral("Parent (id)"), parentId);
    row(QStringLiteral("Lien"), connection);
    row(QStringLiteral("Bande Wi-Fi"), band);
    row(QStringLiteral("SSID"), ssid);
    row(QStringLiteral("RSSI"), rssi != 0 ? QString::number(rssi) : QString());
    row(QStringLiteral("Id mesh"), deviceId);
    row(QStringLiteral("Infrastructure"), infrastructure ? QStringLiteral("oui") : QStringLiteral("non"));
    row(QStringLiteral("Ports TCP (indices, pas une identite)"), NetHints::namedPorts(openPorts));
    row(QStringLiteral("Nature IP"), NetHints::ipKind(ip));
    row(QStringLiteral("OUI"), NetHints::ouiCompact(mac));
    row(QStringLiteral("Indices de services"), services.join(QStringLiteral(" | ")));
    QStringList proofs;
    if (sources.contains(QLatin1String("morfbeacon")))
        proofs << QStringLiteral("heartbeat UDP morfbeacon/1");
    const QJsonArray httpProofs = extra.value(QStringLiteral("morf_http")).toArray();
    for (const QJsonValue& v : httpProofs) {
        const QJsonObject rec = v.toObject();
        const QString p = rec.value(QStringLiteral("proof")).toString();
        const int port = rec.value(QStringLiteral("port")).toInt();
        if (!p.isEmpty())
            proofs << (p + QStringLiteral(" :") + QString::number(port));
    }
    row(QStringLiteral("Preuve morfSystem"),
        proofs.isEmpty() ? QStringLiteral("aucune (un port ouvert ne suffit pas)")
                         : proofs.join(QStringLiteral(" ; ")));
    row(QStringLiteral("Capacites (beacon)"), capabilities.join(QStringLiteral(", ")));
    row(QStringLiteral("Apps morfSystem"), morfApps.join(QStringLiteral(", ")));
    row(QStringLiteral("Sources"), sources.join(QStringLiteral(", ")));
    row(QStringLiteral("Notes"), notes);
    row(QStringLiteral("Vu le"), lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (!extra.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Details API (brut) :");
        lines << QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Indented));
    }
    return lines.join('\n');
}

void Device::mergeFrom(const Device& other)
{
    if (ip.isEmpty())
        ip = other.ip;
    if (normalizedMac(mac).size() < 17 && normalizedMac(other.mac).size() >= 17)
        mac = other.mac;

    if (nameScore(other.hostname, other.ip) > nameScore(hostname, ip))
        hostname = other.hostname;

    if (vendor.isEmpty())
        vendor = other.vendor;
    if (model.isEmpty())
        model = other.model;
    if (osGuess.isEmpty())
        osGuess = other.osGuess;
    if (band.isEmpty())
        band = other.band;
    if (ssid.isEmpty())
        ssid = other.ssid;
    if (deviceId.isEmpty())
        deviceId = other.deviceId;
    if (notes.isEmpty())
        notes = other.notes;
    if (customName.isEmpty())
        customName = other.customName;
    if (category.isEmpty() || category == QLatin1String("unknown")
        || (category == QLatin1String("client") && other.infrastructure))
        category = other.category;

    // Un rattachement Deco est plus precis qu'un parent generique "Livebox".
    const bool otherDeco = other.sources.contains(QLatin1String("deco"))
                           && (!other.parentName.isEmpty() || !other.parentId.isEmpty());
    const bool selfDeco = sources.contains(QLatin1String("deco"))
                          && (!parentName.isEmpty() || !parentId.isEmpty());
    if (otherDeco && !selfDeco) {
        parentId = other.parentId;
        parentName = other.parentName;
        if (!other.connection.isEmpty())
            connection = other.connection;
    } else {
        if (parentId.isEmpty())
            parentId = other.parentId;
        if (parentName.isEmpty() || parentName.compare(QLatin1String("Livebox"), Qt::CaseInsensitive) == 0) {
            if (!other.parentName.isEmpty()
                && other.parentName.compare(QLatin1String("Livebox"), Qt::CaseInsensitive) != 0)
                parentName = other.parentName;
            else if (parentName.isEmpty())
                parentName = other.parentName;
        }
        if (connection.isEmpty())
            connection = other.connection;
    }

    if (other.rssi != 0 && rssi == 0)
        rssi = other.rssi;
    online = online || other.online;
    infrastructure = infrastructure || other.infrastructure;
    for (const QString& s : other.sources) {
        if (!sources.contains(s))
            sources.append(s);
    }
    for (const QString& s : other.services) {
        if (!services.contains(s))
            services.append(s);
    }
    for (const QString& s : other.capabilities) {
        if (!capabilities.contains(s))
            capabilities.append(s);
    }
    for (const QString& s : other.morfApps) {
        if (!morfApps.contains(s))
            morfApps.append(s);
    }
    for (int p : other.openPorts) {
        if (!openPorts.contains(p))
            openPorts.append(p);
    }
    for (auto it = other.extra.begin(); it != other.extra.end(); ++it) {
        if (!extra.contains(it.key()))
            extra.insert(it.key(), it.value());
    }
    if (other.lastSeen.isValid() && other.lastSeen > lastSeen)
        lastSeen = other.lastSeen;
}

static bool sameNode(const Device& node, const QString& token)
{
    if (token.isEmpty())
        return false;
    const QString t = token.trimmed();
    const QString tMac = normalizedMac(t);
    if (!node.deviceId.isEmpty() && node.deviceId.compare(t, Qt::CaseInsensitive) == 0)
        return true;
    if (normalizedMac(node.mac).size() >= 17 && tMac == normalizedMac(node.mac))
        return true;
    if (!node.ip.isEmpty() && node.ip == t)
        return true;
    if (!node.hostname.isEmpty() && node.hostname.compare(t, Qt::CaseInsensitive) == 0)
        return true;
    if (node.displayName().compare(t, Qt::CaseInsensitive) == 0)
        return true;
    if (node.extra.value(QStringLiteral("device_id")).toString().compare(t, Qt::CaseInsensitive) == 0)
        return true;
    const QString bssid2 = normalizedMac(node.extra.value(QStringLiteral("bssid_2g")).toString());
    const QString bssid5 = normalizedMac(node.extra.value(QStringLiteral("bssid_5g")).toString());
    if (tMac.size() >= 17 && (tMac == bssid2 || tMac == bssid5))
        return true;
    return false;
}

void relinkDeviceParents(QVector<Device>& devices)
{
    QVector<int> boxes;
    QVector<int> nodes;
    for (int i = 0; i < devices.size(); ++i) {
        if (devices[i].category == QLatin1String("box"))
            boxes.append(i);
        else if (devices[i].category == QLatin1String("mesh_node"))
            nodes.append(i);
    }

    auto attach = [&](Device& c, const Device& p) {
        c.parentId = p.id();
        c.parentName = p.displayName();
    };

    for (Device& c : devices) {
        if (c.infrastructure)
            continue;
        bool linked = false;
        for (int ni : nodes) {
            const Device& n = devices[ni];
            if (sameNode(n, c.parentId) || sameNode(n, c.parentName)
                || sameNode(n, c.extra.value(QStringLiteral("owner_id")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("owner_name")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("access_host")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("ap_mac")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("parent_id")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("connection_to")).toString())
                || sameNode(n, c.extra.value(QStringLiteral("parent_mac")).toString())) {
                attach(c, n);
                linked = true;
                break;
            }
        }
        if (linked)
            continue;
        const QString conn = (c.connection + c.band).toLower();
        if (conn.contains(QLatin1String("eth")) || conn.contains(QLatin1String("wire"))
            || conn.contains(QLatin1String("lan"))) {
            if (!boxes.isEmpty())
                attach(c, devices[boxes.first()]);
        }
    }
}

void attachMorfServices(QVector<Device>& devices, const QVector<MorfService>& services)
{
    for (Device& d : devices) {
        d.morfApps.clear();
        QStringList kept;
        for (const QString& c : d.capabilities) {
            if (c.startsWith(QLatin1String("lan:")) || c.startsWith(QLatin1String("indice:")))
                kept << c;
        }
        d.capabilities = kept;
        // Preuves HTTP du scan : le overlay beacon ne doit pas les effacer.
        const QJsonArray httpProofs = d.extra.value(QStringLiteral("morf_http")).toArray();
        for (const QJsonValue& v : httpProofs) {
            const QJsonObject rec = v.toObject();
            const QString app = rec.value(QStringLiteral("app")).toString();
            if (!app.isEmpty() && !d.morfApps.contains(app))
                d.morfApps << app;
            const QJsonArray caps = rec.value(QStringLiteral("capabilities")).toArray();
            for (const QJsonValue& c : caps) {
                const QString cap = c.toString();
                if (!cap.isEmpty() && !d.capabilities.contains(cap))
                    d.capabilities << cap;
            }
        }
    }
    for (const MorfService& s : services) {
        if (s.stale())
            continue;
        for (Device& d : devices) {
            // Preuve : le datagramme vient de cette IP. Le hostname seul est trop faible.
            if (d.ip.isEmpty() || d.ip != s.ip)
                continue;
            if (!d.morfApps.contains(s.app))
                d.morfApps << s.app;
            for (const QString& c : s.capabilities) {
                if (!d.capabilities.contains(c))
                    d.capabilities << c;
            }
            if (!d.sources.contains(QLatin1String("morfbeacon")))
                d.sources << QStringLiteral("morfbeacon");
            const QString prev = d.extra.value(QStringLiteral("morf_proof")).toString();
            if (!prev.contains(QLatin1String("morfbeacon")))
                d.extra.insert(QStringLiteral("morf_proof"),
                               prev.isEmpty() ? QStringLiteral("heartbeat morfbeacon/1")
                                              : prev + QStringLiteral(" + heartbeat morfbeacon/1"));
        }
    }
    for (Device& d : devices) {
        d.services.removeAll(QStringLiteral("morfsystem"));
        for (const QString& s : d.services) {
            if (s == QLatin1String("morfsystem"))
                continue;
            const QString cap = QStringLiteral("indice:") + s;
            if (!d.capabilities.contains(cap))
                d.capabilities << cap;
        }
    }
}

void classifyPresence(QVector<Device>& devices)
{
    for (Device& d : devices) {
        d.services.removeAll(QStringLiteral("morfsystem"));
        // IP vide n'est "technique" que s'il n'y a pas d'appareil derriere
        // (pas de MAC). Un hote Livebox eteint n'a souvent plus d'IP : c'est
        // de l'historique, pas une adresse de broadcast.
        if (NetHints::isTechnicalIp(d.ip)) {
            const bool knownBox = d.sources.contains(QLatin1String("livebox"))
                                  || d.sources.contains(QLatin1String("deco"))
                                  || normalizedMac(d.mac).size() >= 17;
            if (d.ip.isEmpty() && knownBox) {
                d.presence = Device::Presence::Historical;
                d.online = false;
                continue;
            }
            d.presence = Device::Presence::Technical;
            d.online = false;
            continue;
        }
        // ARP Windows survit longtemps apres extinction : ce n'est pas une
        // preuve. ICMP, mDNS, ports ouverts, Livebox Active, oui.
        const bool strong = d.sources.contains(QLatin1String("icmp"))
                            || d.sources.contains(QLatin1String("ssdp"))
                            || d.sources.contains(QLatin1String("mdns"))
                            || d.sources.contains(QLatin1String("netbios"))
                            || d.sources.contains(QLatin1String("morfbeacon"))
                            || d.sources.contains(QLatin1String("morfhttp"))
                            || d.sources.contains(QLatin1String("ports"));
        bool apiPresent = false;
        bool apiAbsent = false;
        if (d.sources.contains(QLatin1String("livebox"))) {
            const bool has = d.extra.contains(QStringLiteral("Active"))
                             || d.extra.contains(QStringLiteral("active"));
            const bool active = jsonFlag(d.extra.value(QStringLiteral("Active")),
                                         jsonFlag(d.extra.value(QStringLiteral("active")), false));
            if (has) {
                if (active)
                    apiPresent = true;
                else
                    apiAbsent = true;
            }
        }
        if (d.sources.contains(QLatin1String("deco")) && !d.infrastructure) {
            const QString inet = d.extra.value(QStringLiteral("inet_status")).toString().toLower();
            bool on = jsonFlag(d.extra.value(QStringLiteral("online")), false);
            if (inet == QLatin1String("offline") || inet == QLatin1String("off"))
                on = false;
            else if (inet == QLatin1String("online") || inet == QLatin1String("on"))
                on = true;
            if (on)
                apiPresent = true;
            else if (d.extra.contains(QStringLiteral("online"))
                     || d.extra.contains(QStringLiteral("inet_status")))
                apiAbsent = true;
        }
        // Livebox/Deco "absent" gagne sur un ping opportuniste (IP reattribuee).
        const bool live = d.infrastructure
                          || ((strong || apiPresent) && !apiAbsent);
        d.presence = live ? Device::Presence::Connected : Device::Presence::Historical;
        d.online = live;
    }
}
