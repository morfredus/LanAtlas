#include "scan/MdnsProbe.h"

#include <QHash>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>
#include <QtEndian>
#include <cstring>

// Requete PTR _services._dns-sd._udp.local (decouverte DNS-SD minimale).
static QByteArray mdnsQuery()
{
    QByteArray q;
    auto u16 = [&](quint16 v) {
        char b[2];
        qToBigEndian(v, b);
        q.append(b, 2);
    };
    u16(0x0001); // id
    u16(0x0100); // recursion desired
    u16(1); u16(0); u16(0); u16(0); // 1 question
    const char* labels[] = {"_services", "_dns-sd", "_udp", "local"};
    for (const char* lab : labels) {
        const int n = int(strlen(lab));
        q.append(char(n));
        q.append(lab, n);
    }
    q.append(char(0));
    u16(12); // PTR
    u16(1);  // IN
    return q;
}

static quint16 be16(const QByteArray& p, int off)
{
    if (off + 1 >= p.size())
        return 0;
    return quint16((quint8(p[off]) << 8) | quint8(p[off + 1]));
}

// Lit un nom DNS (avec compression). Avance `off` juste apres le nom (sauf saut).
static QString readName(const QByteArray& p, int& off)
{
    QStringList parts;
    int cur = off;
    bool jumped = false;
    int guard = 0;
    while (cur >= 0 && cur < p.size() && guard++ < 128) {
        const quint8 len = quint8(p[cur]);
        if (len == 0) {
            ++cur;
            if (!jumped)
                off = cur;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (cur + 1 >= p.size())
                break;
            const int ptr = ((len & 0x3F) << 8) | quint8(p[cur + 1]);
            if (!jumped)
                off = cur + 2;
            cur = ptr;
            jumped = true;
            continue;
        }
        ++cur;
        if (cur + len > p.size())
            break;
        parts << QString::fromUtf8(p.mid(cur, int(len)));
        cur += len;
    }
    return parts.join(QLatin1Char('.'));
}

// Ce qu'on accumule par IP au fil des paquets mDNS.
struct MdnsAgg {
    QSet<QString> serviceTypes;   // _googlecast._tcp, _airplay._tcp, ...
    QString hostname;             // depuis un nom .local (instance / A / SRV)
    QString model;                // TXT md= / model=
    QString friendly;             // TXT fn=
};

static void noteName(MdnsAgg& a, const QString& name)
{
    if (name.isEmpty())
        return;
    // Type de service : contient _tcp ou _udp.
    if (name.contains(QLatin1String("._tcp")) || name.contains(QLatin1String("._udp"))) {
        int i = name.indexOf(QLatin1String("._tcp"));
        if (i < 0)
            i = name.indexOf(QLatin1String("._udp"));
        // Garder "<service>._tcp" (dernier segment de service).
        const QString head = name.left(i);
        const QString svc = head.section(QLatin1Char('.'), -1);
        if (svc.startsWith(QLatin1Char('_')))
            a.serviceTypes.insert(svc + name.mid(i, 5));
        // Nom d'instance lisible (avant le premier _service) -> hostname candidat.
        const QString instance = name.section(QLatin1String("._"), 0, 0);
        if (!instance.startsWith(QLatin1Char('_')) && instance.contains(QLatin1Char(' ')) == false
            && a.hostname.isEmpty() && instance != name)
            a.hostname = instance;
        return;
    }
    // Nom d'hote .local (ex. "Freds-iPhone.local").
    if (name.endsWith(QLatin1String(".local")) && !name.startsWith(QLatin1Char('_'))) {
        const QString h = name.left(name.size() - 6);
        if (!h.contains(QLatin1Char('.')) && (a.hostname.isEmpty() || a.hostname.contains('_')))
            a.hostname = h;
    }
}

static void parseTxt(MdnsAgg& a, const QByteArray& p, int start, int len)
{
    int i = start;
    const int end = start + len;
    while (i < end && i < p.size()) {
        const int l = quint8(p[i++]);
        if (l == 0 || i + l > p.size())
            break;
        const QString kv = QString::fromUtf8(p.mid(i, l));
        i += l;
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = kv.left(eq).toLower();
        const QString val = kv.mid(eq + 1);
        if ((key == QLatin1String("md") || key == QLatin1String("model") || key == QLatin1String("am"))
            && a.model.isEmpty() && !val.isEmpty())
            a.model = val;
        else if (key == QLatin1String("fn") && a.friendly.isEmpty())
            a.friendly = val;
    }
}

static void parsePacket(const QByteArray& p, MdnsAgg& a)
{
    if (p.size() < 12)
        return;
    const int qd = be16(p, 4);
    const int an = be16(p, 6);
    const int ns = be16(p, 8);
    const int ar = be16(p, 10);
    int off = 12;
    for (int i = 0; i < qd && off < p.size(); ++i) {
        readName(p, off);
        off += 4; // QTYPE + QCLASS
    }
    const int total = an + ns + ar;
    for (int i = 0; i < total && off < p.size(); ++i) {
        const QString owner = readName(p, off);
        noteName(a, owner);
        const quint16 type = be16(p, off);
        off += 8; // TYPE(2) CLASS(2) TTL(4)
        const int rdlen = be16(p, off);
        off += 2;
        const int rdStart = off;
        if (type == 12) { // PTR
            int t = rdStart;
            noteName(a, readName(p, t));
        } else if (type == 16) { // TXT
            parseTxt(a, p, rdStart, rdlen);
        } else if (type == 33) { // SRV -> cible apres prio/weight/port
            int t = rdStart + 6;
            noteName(a, readName(p, t));
        }
        off = rdStart + rdlen;
    }
}

// Type de service DNS-SD -> (categorie, libelle par defaut si pas de modele).
static void roleFromServices(const QSet<QString>& svc, QString& category, QString& fallbackModel)
{
    auto has = [&](const char* s) {
        for (const QString& v : svc)
            if (v.contains(QLatin1String(s)))
                return true;
        return false;
    };
    if (has("_googlecast")) { category = QStringLiteral("media"); fallbackModel = QStringLiteral("Chromecast"); }
    else if (has("_sonos")) { category = QStringLiteral("media"); fallbackModel = QStringLiteral("Sonos"); }
    else if (has("_spotify-connect")) { category = QStringLiteral("media"); fallbackModel = QStringLiteral("Enceinte"); }
    else if (has("_airplay") || has("_raop")) { category = QStringLiteral("media"); fallbackModel = QStringLiteral("AirPlay"); }
    else if (has("_ipp") || has("_printer") || has("_pdl-datastream") || has("_scanner"))
        { category = QStringLiteral("printer"); fallbackModel = QStringLiteral("Imprimante"); }
    else if (has("_hap") || has("_homekit")) { category = QStringLiteral("iot"); fallbackModel = QStringLiteral("Accessoire HomeKit"); }
    else if (has("_hue")) { category = QStringLiteral("iot"); fallbackModel = QStringLiteral("Philips Hue"); }
    else if (has("_nvstream") || has("_steam")) { category = QStringLiteral("computer"); }
    else if (has("_smb") || has("_afpovertcp") || has("_nfs") || has("_adisk"))
        { category = QStringLiteral("nas"); fallbackModel = QStringLiteral("Stockage reseau"); }
    else if (has("_ssh") || has("_sftp-ssh") || has("_workstation") || has("_rfb"))
        { category = QStringLiteral("computer"); }
}

// Codes modele Apple courants -> nom commercial (best effort).
static QString appleModel(const QString& code)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("J413AP"), QStringLiteral("MacBook Air M2")},
        {QStringLiteral("AudioAccessory5,1"), QStringLiteral("HomePod mini")},
        {QStringLiteral("AppleTV11,1"), QStringLiteral("Apple TV 4K")},
        {QStringLiteral("iPhone14,2"), QStringLiteral("iPhone 13 Pro")},
        {QStringLiteral("iPhone14,3"), QStringLiteral("iPhone 13 Pro Max")},
        {QStringLiteral("iPhone14,5"), QStringLiteral("iPhone 13")},
        {QStringLiteral("iPhone15,2"), QStringLiteral("iPhone 14 Pro")},
        {QStringLiteral("iPhone15,3"), QStringLiteral("iPhone 14 Pro Max")},
        {QStringLiteral("iPad13,1"), QStringLiteral("iPad Air 4")},
    };
    return map.value(code);
}

QVector<Device> mdnsDiscover(int timeoutMs)
{
    QUdpSocket sock;
    sock.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);
    sock.joinMulticastGroup(QHostAddress(QStringLiteral("224.0.0.251")));
    sock.writeDatagram(mdnsQuery(), QHostAddress(QStringLiteral("224.0.0.251")), 5353);

    QElapsedTimer t;
    t.start();
    QHash<QString, MdnsAgg> byIp;
    while (t.elapsed() < timeoutMs) {
        sock.waitForReadyRead(80);
        while (sock.hasPendingDatagrams()) {
            QByteArray payload;
            payload.resize(int(sock.pendingDatagramSize()));
            QHostAddress from;
            quint16 port = 0;
            sock.readDatagram(payload.data(), payload.size(), &from, &port);
            const QString ip = QHostAddress(from.toIPv4Address()).toString();
            parsePacket(payload, byIp[ip]);
        }
    }

    QVector<Device> out;
    for (auto it = byIp.constBegin(); it != byIp.constEnd(); ++it) {
        const MdnsAgg& a = it.value();
        Device d;
        d.ip = it.key();
        d.online = true;
        d.sources << QStringLiteral("mdns");
        d.services << QStringLiteral("mdns");
        if (!a.hostname.isEmpty())
            d.hostname = a.hostname;
        else if (!a.friendly.isEmpty())
            d.hostname = a.friendly;
        QString category, fallback;
        roleFromServices(a.serviceTypes, category, fallback);
        if (!category.isEmpty())
            d.category = category;
        // Modele : TXT md=/model= (mappe si code Apple), sinon libelle de role.
        if (!a.model.isEmpty()) {
            const QString friendly = appleModel(a.model);
            d.model = friendly.isEmpty() ? a.model : friendly;
        } else if (!fallback.isEmpty()) {
            d.model = fallback;
        }
        // Consigner les services vus (utile dans la fiche detail).
        for (const QString& s : a.serviceTypes)
            d.capabilities << s;
        out.append(d);
    }
    return out;
}
