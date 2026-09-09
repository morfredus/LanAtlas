#include "scan/LanContext.h"

#include <QNetworkInterface>

QString ipv4String(quint32 host)
{
    return QHostAddress(host).toString();
}

LanContext detectLanContext()
{
    LanContext ctx;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (e.ip().isLoopback())
                continue;
            const quint32 ip = e.ip().toIPv4Address();
            const quint32 mask = e.netmask().toIPv4Address();
            if (mask == 0)
                continue;
            ctx.localIp = e.ip();
            ctx.netmask = e.netmask();
            ctx.broadcast = e.broadcast();
            ctx.interfaceName = iface.humanReadableName();
            const quint32 network = ip & mask;
            const quint32 bcast = network | ~mask;
            ctx.firstHost = network + 1;
            ctx.lastHost = bcast - 1;
            int prefix = 0;
            quint32 m = mask;
            while (m) {
                prefix += m & 1u;
                m >>= 1;
            }
            ctx.cidr = e.ip().toString() + "/" + QString::number(prefix);
            // passerelle : premiere adresse du sous-reseau, hypothese Livebox
            ctx.gateway = QHostAddress(network + 1);
            return ctx;
        }
    }
    return ctx;
}
