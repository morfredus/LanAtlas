#include "scan/BeaconListener.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QUdpSocket>

namespace {
int ipv4Score(const QHostAddress& addr)
{
    if (addr.protocol() != QAbstractSocket::IPv4Protocol)
        return 0;
    if (addr.isLoopback())
        return 1;
    if (addr.isLinkLocal())
        return 5;
    const quint32 v = addr.toIPv4Address();
    // Preferer le LAN maison (192.168) aux reseaux de VM / Docker.
    if ((v >> 16) == 0xC0A8)
        return 80;
    if ((v >> 24) == 10)
        return 50;
    if ((v >> 16) == 0xAC11)
        return 8; // 172.17.x Docker
    if ((v >> 8) == 0xAC10)
        return 40; // 172.16
    return 20;
}

QString cleanIp(QHostAddress addr)
{
    if (addr.protocol() == QAbstractSocket::IPv6Protocol && addr.toString().startsWith(QLatin1String("::ffff:")))
        addr = QHostAddress(addr.toIPv4Address());
    return addr.toString();
}
} // namespace

BeaconListener::BeaconListener(QObject* parent)
    : QObject(parent)
{
}

bool BeaconListener::start(quint16 port)
{
    if (socket_)
        return bindError_.isEmpty();
    socket_ = new QUdpSocket(this);
    // ShareAddress : morfMonitor, le Dashboard, etc. ecoutent le meme port.
    if (!socket_->bind(QHostAddress::AnyIPv4, port,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        bindError_ = socket_->errorString();
        return false;
    }
    connect(socket_, &QUdpSocket::readyRead, this, &BeaconListener::onReadyRead);
    return true;
}

QVector<MorfService> BeaconListener::services() const
{
    QVector<MorfService> out;
    out.reserve(byKey_.size());
    for (const MorfService& s : byKey_)
        out.append(s);
    return out;
}

void BeaconListener::onReadyRead()
{
    bool dirty = false;
    while (socket_ && socket_->hasPendingDatagrams()) {
        const QNetworkDatagram dg = socket_->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.value(QStringLiteral("proto")).toString() != QLatin1String("morfbeacon/1"))
            continue;
        const QString app = o.value(QStringLiteral("app")).toString();
        if (app.isEmpty())
            continue;

        MorfService s;
        s.app = app;
        const QString instance = o.value(QStringLiteral("instance")).toString();
        s.host = o.value(QStringLiteral("host")).toString();
        s.version = o.value(QStringLiteral("version")).toString();
        s.state = o.value(QStringLiteral("state")).toString();
        s.role = o.value(QStringLiteral("role")).toString(QStringLiteral("host"));
        s.statusPort = static_cast<quint16>(o.value(QStringLiteral("status_port")).toInt());
        s.ip = cleanIp(dg.senderAddress());
        s.addressScore = ipv4Score(QHostAddress(s.ip));
        s.lastSeen = QDateTime::currentDateTime();
        s.datagram = o;
        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
            s.capabilities << c.toString();

        s.key = instance.isEmpty()
                    ? (app + QLatin1Char('@') + s.ip)
                    : instance;

        if (byKey_.contains(s.key)) {
            const MorfService& old = byKey_[s.key];
            if (old.addressScore > s.addressScore && !old.ip.isEmpty()) {
                s.ip = old.ip;
                s.addressScore = old.addressScore;
            }
        }
        byKey_.insert(s.key, s);
        dirty = true;
    }
    if (dirty)
        emit changed();
}
