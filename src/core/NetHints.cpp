#include "core/NetHints.h"
#include "core/Device.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QMap>

QString NetHints::wellKnownPort(int port)
{
    static const QMap<int, QString> names = {
        {22, QStringLiteral("ssh")},
        {53, QStringLiteral("dns")},
        {80, QStringLiteral("http")},
        {139, QStringLiteral("netbios")},
        {443, QStringLiteral("https")},
        {445, QStringLiteral("smb")},
        {515, QStringLiteral("lpd")},
        {548, QStringLiteral("afp")},
        {554, QStringLiteral("rtsp")},
        {631, QStringLiteral("ipp")},
        {1883, QStringLiteral("mqtt")},
        {3389, QStringLiteral("rdp")},
        {5000, QStringLiteral("upnp/syno")},
        {5357, QStringLiteral("wsd")},
        {5480, QStringLiteral("afp")},
        {5900, QStringLiteral("vnc")},
        {8006, QStringLiteral("proxmox")},
        {8080, QStringLiteral("http-alt")},
        {8096, QStringLiteral("http-8096")},
        {8123, QStringLiteral("http-8123")},
        {8443, QStringLiteral("https-alt")},
        {8787, QStringLiteral("tcp-8787")},
        {8788, QStringLiteral("tcp-8788")},
        {8789, QStringLiteral("tcp-8789")},
        {8790, QStringLiteral("tcp-8790")},
        {8791, QStringLiteral("tcp-8791")},
        {8883, QStringLiteral("mqtt-tls")},
        {8888, QStringLiteral("http-8888")},
        {9100, QStringLiteral("jetdirect")},
        {32400, QStringLiteral("plex")},
    };
    return names.value(port);
}

QString NetHints::namedPorts(const QList<int>& ports)
{
    QStringList bits;
    for (int p : ports) {
        const QString n = wellKnownPort(p);
        bits << (n.isEmpty() ? QString::number(p) : (QString::number(p) + QLatin1Char('/') + n));
    }
    return bits.join(QStringLiteral(", "));
}

QString NetHints::ipKind(const QString& ip)
{
    const QHostAddress a(ip);
    if (a.isNull())
        return {};
    if (a.isLoopback())
        return QStringLiteral("loopback");
    if (a.isLinkLocal())
        return QStringLiteral("lien-local (APIPA/169.254)");
    if (a.isMulticast())
        return QStringLiteral("multicast");
    if (a.protocol() != QAbstractSocket::IPv4Protocol)
        return QStringLiteral("IPv6");
    const quint32 v = a.toIPv4Address();
    if ((v >> 24) == 10)
        return QStringLiteral("prive RFC1918 (10/8)");
    if ((v >> 16) == 0xC0A8)
        return QStringLiteral("prive RFC1918 (192.168/16)");
    if ((v >> 16) >= 0xAC10 && (v >> 16) <= 0xAC1F)
        return QStringLiteral("prive RFC1918 (172.16/12)");
    if ((v >> 16) == 0xA9FE)
        return QStringLiteral("lien-local");
    if ((v >> 24) == 127)
        return QStringLiteral("loopback");
    return QStringLiteral("adresse publique ou autre");
}

QString NetHints::ouiCompact(const QString& mac)
{
    const QString m = normalizedMac(mac);
    if (m.size() < 8)
        return {};
    return m.left(8);
}

bool NetHints::isTechnicalIp(const QString& ip)
{
    if (ip.isEmpty())
        return true;
    const QHostAddress a(ip);
    if (a.isNull())
        return false;
    if (a.isLoopback() || a.isMulticast() || a.isBroadcast())
        return true;
    if (a.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    const quint32 v = a.toIPv4Address();
    const quint32 host = v & 0xFFu;
    if (host == 0 || host == 255)
        return true;
    if ((v >> 24) >= 224)
        return true;
    return false;
}

bool NetHints::isHttpQualifyPort(int port)
{
    // Plage reservee morfTools + 8888 (souvent HTTP). 80/8080 trop courants :
    // les qualifier tous allongerait le scan pour des pages HTML.
    if (port == 8888)
        return true;
    return port >= 8787 && port <= 8899;
}
