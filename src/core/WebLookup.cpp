#include "core/WebLookup.h"

#include <QDesktopServices>
#include <QUrl>

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
