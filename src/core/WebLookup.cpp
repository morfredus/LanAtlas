#include "core/WebLookup.h"
#include "core/NetHints.h"

#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>

static void openQuery(const QString& q)
{
    QUrl u(QStringLiteral("https://duckduckgo.com/"));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("q"), q);
    u.setQuery(qq);
    QDesktopServices::openUrl(u);
}

void WebLookup::searchDevice(const Device& d)
{
    QStringList parts;
    if (!d.hostname.isEmpty())
        parts << d.hostname;
    if (!d.vendor.isEmpty())
        parts << d.vendor;
    if (!d.model.isEmpty())
        parts << d.model;
    const QString mac = normalizedMac(d.mac);
    if (mac.size() >= 8)
        parts << (QStringLiteral("OUI ") + NetHints::ouiCompact(d.mac));
    if (parts.isEmpty() && !d.ip.isEmpty())
        parts << d.ip;
    parts << QStringLiteral("appareil reseau");
    openQuery(parts.join(QLatin1Char(' ')));
}

void WebLookup::lookupMac(const QString& mac)
{
    const QString m = normalizedMac(mac);
    if (m.size() < 8)
        return;
    const QString compact = QString(m).remove(QLatin1Char(':'));
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://maclookup.app/macaddress/") + compact));
}

void WebLookup::lookupOui(const QString& mac)
{
    const QString p = NetHints::ouiCompact(mac);
    if (p.isEmpty())
        return;
    openQuery(QStringLiteral("IEEE OUI ") + p + QStringLiteral(" MAC vendor"));
}

void WebLookup::searchPorts(const Device& d)
{
    if (d.openPorts.isEmpty())
        return;
    openQuery(NetHints::namedPorts(d.openPorts) + QStringLiteral(" ports TCP"));
}

void WebLookup::openHttp(const Device& d)
{
    if (d.ip.isEmpty())
        return;
    const bool https = d.openPorts.contains(443) || d.openPorts.contains(8443);
    const int port = https ? (d.openPorts.contains(443) ? 443 : 8443)
                           : (d.openPorts.contains(80) ? 80 : (d.openPorts.contains(8080) ? 8080 : 0));
    if (port == 0)
        return;
    QString url = (port == 443 || port == 8443) ? QStringLiteral("https://") : QStringLiteral("http://");
    url += d.ip;
    if (port != 80 && port != 443)
        url += QLatin1Char(':') + QString::number(port);
    QDesktopServices::openUrl(QUrl(url));
}

void WebLookup::openStatus(const QString& url)
{
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}
