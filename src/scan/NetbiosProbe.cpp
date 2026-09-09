#include "scan/NetbiosProbe.h"

#include <QUdpSocket>
#include <QElapsedTimer>

static QByteArray nbstatQuery()
{
    QByteArray q;
    q.append(char(0x12)); q.append(char(0x34)); // tid
    q.append(char(0x00)); q.append(char(0x00)); // flags
    q.append(char(0x00)); q.append(char(0x01)); // questions
    q.append(char(0x00)); q.append(char(0x00));
    q.append(char(0x00)); q.append(char(0x00));
    q.append(char(0x00)); q.append(char(0x00));
    q.append(char(32));
    q.append(QByteArray("CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    q.append(char(0x00));
    q.append(char(0x00)); q.append(char(0x21)); // NBSTAT
    q.append(char(0x00)); q.append(char(0x01));
    return q;
}

static QString decodeNbName(const QByteArray& pkt)
{
    const int idx = pkt.indexOf(char(0x20));
    if (idx < 0 || pkt.size() < idx + 50)
        return {};
    // Apres l'en-tete, le premier nom NetBIOS est souvent a offset variable.
    for (int i = 56; i + 15 < pkt.size(); ++i) {
        QByteArray slice = pkt.mid(i, 15);
        bool printable = true;
        for (char c : slice) {
            if (c != 0 && (c < 32 || c > 126)) {
                printable = false;
                break;
            }
        }
        if (printable) {
            const QString name = QString::fromLatin1(slice).trimmed();
            if (name.size() >= 3)
                return name;
        }
    }
    return {};
}

QVector<Device> netbiosDiscover(const QVector<Device>& seeds, int timeoutMs)
{
    QVector<Device> out;
    QUdpSocket sock;
    sock.bind(QHostAddress::AnyIPv4, 0);
    const QByteArray q = nbstatQuery();
    for (const Device& s : seeds) {
        if (!s.ip.isEmpty())
            sock.writeDatagram(q, QHostAddress(s.ip), 137);
    }
    QElapsedTimer t;
    t.start();
    QSet<QString> seen;
    while (t.elapsed() < timeoutMs) {
        sock.waitForReadyRead(50);
        while (sock.hasPendingDatagrams()) {
            QByteArray payload;
            payload.resize(int(sock.pendingDatagramSize()));
            QHostAddress from;
            quint16 port = 0;
            sock.readDatagram(payload.data(), payload.size(), &from, &port);
            const QString ip = QHostAddress(from.toIPv4Address()).toString();
            if (seen.contains(ip))
                continue;
            seen.insert(ip);
            Device d;
            d.ip = ip;
            d.hostname = decodeNbName(payload);
            d.online = true;
            d.sources << QStringLiteral("netbios");
            out.append(d);
        }
    }
    return out;
}
