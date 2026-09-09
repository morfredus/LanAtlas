#include "scan/TuyaListener.h"
#include "crypto/AesEcb.h"
#include "crypto/AesGcm.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QUdpSocket>

namespace {

// Cle de dechiffrement des broadcasts 6667 : elle est FIXE et publique (la meme
// pour tous les appareils Tuya du monde), c'est md5("yGAdlopoPVldABfn"). Elle ne
// sert qu'a lire l'annonce de decouverte ; piloter l'appareil exige, lui, la
// local key propre a chaque appareil (recuperee via le cloud Tuya).
QByteArray discoveryKey()
{
    static const QByteArray key =
        QCryptographicHash::hash(QByteArrayLiteral("yGAdlopoPVldABfn"),
                                 QCryptographicHash::Md5);
    return key;
}

// Extrait le JSON meme s'il est precede d'un entete (numero de version, octets
// reserves...) : on cherche simplement la premiere accolade ouvrante.
QJsonObject looseJson(const QByteArray& buf)
{
    const int start = buf.indexOf('{');
    if (start < 0)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(buf.mid(start));
    return doc.object();
}

// Nettoie l'IPv4 : les sockets Qt rendent parfois une forme mappee ::ffff:x.x.x.x.
QString cleanIp(QHostAddress addr)
{
    if (addr.protocol() == QAbstractSocket::IPv6Protocol
        && addr.toString().startsWith(QLatin1String("::ffff:")))
        addr = QHostAddress(addr.toIPv4Address());
    return addr.toString();
}

} // namespace

TuyaListener::TuyaListener(QObject* parent)
    : QObject(parent)
{
}

bool TuyaListener::start()
{
    auto bindPort = [this](quint16 port) -> QUdpSocket* {
        auto* s = new QUdpSocket(this);
        // ShareAddress : un autre outil (ou une seconde instance) peut ecouter
        // le meme port de broadcast sans conflit.
        if (!s->bind(QHostAddress::AnyIPv4, port,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            delete s;
            return nullptr;
        }
        connect(s, &QUdpSocket::readyRead, this, &TuyaListener::onReadyRead);
        return s;
    };
    sock6666_ = bindPort(6666);
    sock6667_ = bindPort(6667);
    return sock6666_ || sock6667_;
}

QVector<TuyaAnnounce> TuyaListener::announces() const
{
    QVector<TuyaAnnounce> out;
    out.reserve(byIp_.size());
    for (const TuyaAnnounce& a : byIp_)
        out.append(a);
    return out;
}

void TuyaListener::onReadyRead()
{
    drain(sock6666_);
    drain(sock6667_);
}

void TuyaListener::drain(QUdpSocket* sock)
{
    if (!sock)
        return;
    while (sock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = sock->receiveDatagram();
        ingest(dg.data(), cleanIp(dg.senderAddress()));
    }
}

void TuyaListener::ingest(const QByteArray& dg, const QString& fromIp)
{
    // Tuya connait deux formats de trame :
    //  - "55aa" (protocoles 3.1 a 3.4) : entete 16 o, fin 8 o (crc + suffixe),
    //    payload clair (6666) ou chiffre en AES-ECB (6667).
    //  - "6699" (protocole 3.5) : entete 18 o, fin 4 o (suffixe), payload en
    //    AES-GCM (nonce 12 o + chiffre + tag 16 o), authentifie par un AAD.
    // La cle du broadcast est la meme cle fixe publique dans les deux cas.
    if (dg.size() < 24)
        return;
    const auto* p = reinterpret_cast<const unsigned char*>(dg.constData());

    QByteArray plain;
    bool encrypted = false;

    if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x55 && p[3] == 0xaa) {
        const int declared = (p[12] << 24) | (p[13] << 16) | (p[14] << 8) | p[15];
        int payloadLen = declared - 8;   // la longueur couvre payload + crc + suffixe
        if (payloadLen <= 0 || 16 + payloadLen > dg.size())
            payloadLen = dg.size() - 16 - 8;  // repli tolerant
        const QByteArray payload = dg.mid(16, payloadLen);
        plain = payload;                       // 6666 : JSON en clair
        if (looseJson(plain).isEmpty()) {      // 6667 : AES-ECB
            plain = AesEcb::decrypt(discoveryKey(), payload);
            encrypted = true;
        }
    } else if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x66 && p[3] == 0x99) {
        // entete : prefixe(4) inconnu(2) seq(4) commande(4) longueur(4) = 18 o.
        const int length = (p[14] << 24) | (p[15] << 16) | (p[16] << 8) | p[17];
        if (length < 12 + 16 || 18 + length > dg.size())
            return;
        const QByteArray payload = dg.mid(18, length);
        const QByteArray iv = payload.left(12);
        const QByteArray tag = payload.right(16);
        const QByteArray cipher = payload.mid(12, payload.size() - 12 - 16);
        const QByteArray aad = dg.mid(4, 14);  // tout l'entete apres le prefixe
        plain = AesGcm::decrypt(discoveryKey(), iv, aad, cipher, tag);
        encrypted = true;
    } else {
        return;  // format inconnu
    }

    const QJsonObject o = looseJson(plain);
    if (o.isEmpty())
        return;

    TuyaAnnounce a;
    a.ip = o.value(QStringLiteral("ip")).toString();
    if (a.ip.isEmpty())
        a.ip = fromIp;
    a.deviceId = o.value(QStringLiteral("gwId")).toString(
        o.value(QStringLiteral("devId")).toString());
    a.productKey = o.value(QStringLiteral("productKey")).toString();
    a.version = o.value(QStringLiteral("version")).toString();
    a.encrypted = encrypted;
    a.lastSeen = QDateTime::currentDateTime();
    if (a.ip.isEmpty())
        return;

    byIp_.insert(a.ip, a);
    emit changed();
}

void attachTuyaDevices(QVector<Device>& devices, const QVector<TuyaAnnounce>& announces)
{
    const QDateTime now = QDateTime::currentDateTime();
    for (const TuyaAnnounce& a : announces) {
        Device* found = nullptr;
        for (Device& d : devices) {
            if (!d.ip.isEmpty() && d.ip == a.ip) {
                found = &d;
                break;
            }
        }
        if (!found) {
            // Entendu en broadcast mais pas (encore) vu par le scan : on l'ajoute
            // pour qu'il apparaisse tout de suite sur la carte.
            Device nd;
            nd.ip = a.ip;
            devices.append(nd);
            found = &devices.last();
        }
        Device& d = *found;

        if (d.vendor.isEmpty())
            d.vendor = QStringLiteral("Tuya");
        // "client" est la categorie par defaut d'un hote generique : un Tuya est
        // un objet connecte, on precise. On ne touche jamais box / mesh_node.
        if (d.category.isEmpty() || d.category == QLatin1String("unknown")
            || d.category == QLatin1String("client"))
            d.category = QStringLiteral("iot");
        if (d.deviceId.isEmpty())
            d.deviceId = a.deviceId;
        if (!d.sources.contains(QLatin1String("tuya")))
            d.sources << QStringLiteral("tuya");
        if (!d.capabilities.contains(QLatin1String("tuya-lan")))
            d.capabilities << QStringLiteral("tuya-lan");

        d.extra.insert(QStringLiteral("tuya_device_id"), a.deviceId);
        d.extra.insert(QStringLiteral("tuya_product_key"), a.productKey);
        d.extra.insert(QStringLiteral("tuya_version"), a.version);
        d.extra.insert(QStringLiteral("tuya_encrypted"), a.encrypted);

        // Une annonce recente est une preuve de presence : l'appareil vient de se
        // signaler. On evite de fausser l'etat avec une annonce trop ancienne.
        if (a.lastSeen.isValid() && a.lastSeen.secsTo(now) < 90) {
            d.online = true;
            if (!d.lastSeen.isValid() || a.lastSeen > d.lastSeen)
                d.lastSeen = a.lastSeen;
        }
    }
}
