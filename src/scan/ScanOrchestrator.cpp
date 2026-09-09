#include "scan/ScanOrchestrator.h"

#include "core/IspDetect.h"
#include "scan/ArpTable.h"
#include "scan/PingSweep.h"
#include "scan/PortProbe.h"
#include "scan/MorfHttpQualify.h"
#include "scan/SsdpProbe.h"
#include "scan/MdnsProbe.h"
#include "scan/NetbiosProbe.h"
#include "adapters/LiveboxClient.h"
#include "adapters/DecoClient.h"

ScanOrchestrator::ScanOrchestrator(Inventory* inventory, OuiLookup* oui, QObject* parent)
    : QObject(parent), inventory_(inventory), oui_(oui)
{
}

void ScanOrchestrator::run(AppSettings settings, bool incremental)
{
    inventory_->beginScan(incremental);
    emit progress(2, QStringLiteral("Interface locale"));
    const LanContext ctx = detectLanContext();
    inventory_->lanCidr = ctx.cidr;
    inventory_->log(QStringLiteral("LAN detecte : ") + ctx.cidr + " via " + ctx.interfaceName);

    emit progress(8, QStringLiteral("Balayage ICMP (cache ARP)"));
    primeArpCache(ctx, settings.pingTimeoutMs, settings.maxParallelPings);

    emit progress(25, QStringLiteral("Lecture table ARP"));
    for (Device d : readArpTable(ctx)) {
        d.vendor = oui_->vendor(d.mac);
        IspDetect::enrich(d);
        inventory_->upsert(d);
    }

    emit progress(35, QStringLiteral("Confirmation ICMP"));
    QStringList ips;
    for (const Device& d : inventory_->devices()) {
        if (!d.ip.isEmpty())
            ips << d.ip;
    }
    for (Device d : pingSweep(ips, settings.pingTimeoutMs, settings.maxParallelPings))
        inventory_->upsert(d);

    emit progress(48, QStringLiteral("SSDP / UPnP"));
    for (Device d : ssdpDiscover(2500))
        inventory_->upsert(d);

    emit progress(55, QStringLiteral("mDNS"));
    for (Device d : mdnsDiscover(1800))
        inventory_->upsert(d);

    emit progress(62, QStringLiteral("NetBIOS"));
    for (Device d : netbiosDiscover(inventory_->devices(), 600))
        inventory_->upsert(d);

    emit progress(68, QStringLiteral("API Livebox Orange"));
    QString boxIp;
    for (Device d : queryLivebox(settings, *inventory_, &boxIp)) {
        if (d.vendor.isEmpty())
            d.vendor = oui_->vendor(d.mac);
        IspDetect::enrich(d);
        inventory_->upsert(d);
    }

    emit progress(78, QStringLiteral("API mesh Deco X50"));
    QStringList decoCandidates;
    if (!settings.decoHost.isEmpty())
        decoCandidates << settings.decoHost;
    for (const Device& d : inventory_->devices()) {
        const QString blob = (d.hostname + d.vendor + d.model + d.notes).toLower();
        if (blob.contains(QLatin1String("deco")) || blob.contains(QLatin1String("tp-link"))
            || blob.contains(QLatin1String("tplink")) || blob.contains(QLatin1String("x50"))
            || d.category == QLatin1String("mesh_node")) {
            if (!d.ip.isEmpty())
                decoCandidates << d.ip;
        }
    }
    for (Device d : queryDeco(settings, *inventory_, decoCandidates)) {
        if (d.vendor.isEmpty())
            d.vendor = oui_->vendor(d.mac);
        IspDetect::enrich(d);
        inventory_->upsert(d);
    }

    emit progress(85, QStringLiteral("Ports TCP"));
    {
        auto snapshot = inventory_->devices();
        probePorts(snapshot, settings.tcpTimeoutMs, settings.deepPortScan,
                   [this](int done, int total) {
                       if (total <= 0)
                           return;
                       const int pct = 85 + (6 * done) / total;
                       emit progress(qMin(91, pct),
                                     QStringLiteral("Ports TCP (%1/%2)").arg(done).arg(total));
                   });
        for (const Device& d : snapshot)
            inventory_->upsert(d);
    }

    emit progress(92, QStringLiteral("Qualification HTTP morfSystem"));
    {
        auto snapshot = inventory_->devices();
        qualifyMorfHttp(snapshot, settings.tcpTimeoutMs,
                        [this](int done, int total) {
                            if (total <= 0)
                                return;
                            const int pct = 92 + (6 * done) / total;
                            emit progress(qMin(98, pct),
                                          QStringLiteral("Qualification HTTP (%1/%2)").arg(done).arg(total));
                        });
        for (const Device& d : snapshot)
            inventory_->upsert(d);
        int httpMorf = 0;
        for (const Device& d : snapshot) {
            if (d.sources.contains(QLatin1String("morfhttp")))
                ++httpMorf;
        }
        inventory_->log(QStringLiteral("Qualification HTTP : %1 hote(s) avec /status contrat.")
                            .arg(httpMorf));
    }

    emit progress(90, QStringLiteral("Empreinte HTTP (titre / serveur)"));
    {
        auto snapshot = inventory_->devices();
        fingerprintHttp(snapshot, settings.tcpTimeoutMs);
        for (const Device& d : snapshot)
            inventory_->upsert(d);
    }

    {
        auto snap = inventory_->devices();
        // MAC aleatoire : signaler l'absence de fabricant OUI plutot que de
        // laisser une colonne vide inexpliquee (surtout les mobiles).
        for (Device& d : snap) {
            if (d.vendor.isEmpty() && !d.infrastructure && isRandomizedMac(d.mac))
                d.vendor = QStringLiteral("MAC aleatoire");
        }
        relinkDeviceParents(snap);
        classifyPresence(snap);
        inventory_->replaceAll(snap);
        int nConn = 0, nHist = 0, nTech = 0;
        for (const Device& d : snap) {
            if (d.presence == Device::Presence::Connected)
                ++nConn;
            else if (d.presence == Device::Presence::Technical)
                ++nTech;
            else
                ++nHist;
        }
        inventory_->log(QStringLiteral("Presence : %1 connecte(s), %2 historique(s), %3 technique(s).")
                            .arg(nConn)
                            .arg(nHist)
                            .arg(nTech));
        inventory_->log(QStringLiteral("Rattachement parent recalcule (Deco / Livebox)."));
    }

    // Pas de PTR bloquant : QHostInfo::fromName n'a pas de timeout, et Windows
    // attend souvent plusieurs secondes par IP sans enregistrement inverse
    // (24 hotes = scan colle a 94 %). Les noms viennent de Livebox, Deco, mDNS, NetBIOS.
    inventory_->log(QStringLiteral("PTR DNS omis (evite le blocage du resolveur Windows)."));

    emit progress(100, QStringLiteral("Termine"));
    inventory_->log(QStringLiteral("Inventaire : %1 equipement(s).").arg(inventory_->devices().size()));
    emit finished();
}
