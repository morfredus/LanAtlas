#include "scan/SsdpProbe.h"
#include "scan/HttpUtil.h"

#include <QUdpSocket>
#include <QUrl>
#include <QElapsedTimer>
#include <QRegularExpression>

// Extrait une balise simple d'un XML de description UPnP (<friendlyName>...).
static QString xmlTag(const QString& xml, const QString& tag)
{
    QRegularExpression re(QStringLiteral("<%1>\\s*(.*?)\\s*</%1>").arg(tag),
                          QRegularExpression::CaseInsensitiveOption
                              | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(xml);
    return m.hasMatch() ? m.captured(1).simplified() : QString();
}

// Categorie deduite du deviceType UPnP (pour un rendu plus parlant).
static QString upnpCategory(const QString& deviceType)
{
    const QString t = deviceType.toLower();
    if (t.contains(QLatin1String("mediarenderer")) || t.contains(QLatin1String("mediaserver"))
        || t.contains(QLatin1String("dial")))
        return QStringLiteral("media");
    if (t.contains(QLatin1String("printer")))
        return QStringLiteral("printer");
    if (t.contains(QLatin1String("internetgateway")) || t.contains(QLatin1String("wandevice")))
        return QStringLiteral("box");
    return {};
}

// 2e passe : recuperer le XML pointe par LOCATION et en tirer nom/modele/fabricant.
// C'est la meilleure source de modele pour TV, imprimantes, NAS, box, lecteurs.
static void enrichFromDescription(Device& d)
{
    QString locUrl;
    for (const QString& s : d.services) {
        if (s.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
            locUrl = s;
            break;
        }
    }
    if (locUrl.isEmpty())
        return;
    HttpClient http;
    http.setTlsRelaxed(true);
    const HttpResult r = http.get(QUrl(locUrl), 2500);
    if (!r.ok() || r.body.isEmpty())
        return;
    const QString xml = QString::fromUtf8(r.body);
    const QString friendly = xmlTag(xml, QStringLiteral("friendlyName"));
    const QString maker = xmlTag(xml, QStringLiteral("manufacturer"));
    const QString modelName = xmlTag(xml, QStringLiteral("modelName"));
    const QString modelDesc = xmlTag(xml, QStringLiteral("modelDescription"));
    const QString deviceType = xmlTag(xml, QStringLiteral("deviceType"));
    if (!friendly.isEmpty() && !friendly.contains(QLatin1String("urn:")))
        d.hostname = friendly;
    if (d.vendor.isEmpty() && !maker.isEmpty())
        d.vendor = maker;
    if (d.model.isEmpty()) {
        if (!modelName.isEmpty())
            d.model = modelName;
        else if (!modelDesc.isEmpty())
            d.model = modelDesc;
    }
    const QString cat = upnpCategory(deviceType);
    if (!cat.isEmpty() && (d.category.isEmpty() || d.category == QLatin1String("box")))
        d.category = cat;
    d.sources << QStringLiteral("upnp");
}

QVector<Device> ssdpDiscover(int timeoutMs)
{
    QVector<Device> out;
    QUdpSocket sock;
    sock.bind(QHostAddress::AnyIPv4, 0);
    const QByteArray req =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";
    sock.writeDatagram(req, QHostAddress(QStringLiteral("239.255.255.250")), 1900);

    QElapsedTimer t;
    t.start();
    QSet<QString> seen;
    while (t.elapsed() < timeoutMs) {
        sock.waitForReadyRead(100);
        while (sock.hasPendingDatagrams()) {
            QByteArray payload;
            payload.resize(int(sock.pendingDatagramSize()));
            QHostAddress from;
            quint16 port = 0;
            sock.readDatagram(payload.data(), payload.size(), &from, &port);
            const QString text = QString::fromUtf8(payload);
            const QString ip = QHostAddress(from.toIPv4Address()).toString();
            if (seen.contains(ip))
                continue;
            seen.insert(ip);
            Device d;
            d.ip = ip;
            d.online = true;
            d.sources << QStringLiteral("ssdp");
            static const QRegularExpression locRe("LOCATION:\\s*(\\S+)", QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression srvRe("SERVER:\\s*(.+)", QRegularExpression::CaseInsensitiveOption);
            const auto loc = locRe.match(text);
            const auto srv = srvRe.match(text);
            if (loc.hasMatch())
                d.services << loc.captured(1);
            if (srv.hasMatch())
                d.notes = srv.captured(1).trimmed();
            if (text.contains(QLatin1String("InternetGatewayDevice"), Qt::CaseInsensitive)) {
                d.category = QStringLiteral("box");
                d.infrastructure = true;
            }
            if (text.contains(QLatin1String("livebox"), Qt::CaseInsensitive)) {
                d.vendor = QStringLiteral("Orange");
                d.model = QStringLiteral("Livebox");
                d.infrastructure = true;
                d.category = QStringLiteral("box");
            }
            out.append(d);
        }
    }
    // 2e passe : description UPnP (nom/modele/fabricant reels).
    for (Device& d : out)
        enrichFromDescription(d);
    return out;
}
