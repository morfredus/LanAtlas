#include "scan/ArpTable.h"
#include "scan/LanContext.h"

#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QtEndian>
#include <atomic>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <iphlpapi.h>
#endif

#ifdef Q_OS_WIN
QVector<Device> readArpTable(const LanContext&)
{
    QVector<Device> out;
    ULONG size = 0;
    GetIpNetTable(nullptr, &size, FALSE);
    QByteArray buf(int(size), 0);
    auto* table = reinterpret_cast<MIB_IPNETTABLE*>(buf.data());
    if (GetIpNetTable(table, &size, FALSE) != NO_ERROR)
        return out;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (row.dwType == MIB_IPNET_TYPE_INVALID || row.dwPhysAddrLen < 6)
            continue;
        Device d;
        d.ip = QHostAddress(qFromBigEndian(row.dwAddr)).toString();
        d.mac = QStringLiteral("%1:%2:%3:%4:%5:%6")
                    .arg(row.bPhysAddr[0], 2, 16, QChar('0'))
                    .arg(row.bPhysAddr[1], 2, 16, QChar('0'))
                    .arg(row.bPhysAddr[2], 2, 16, QChar('0'))
                    .arg(row.bPhysAddr[3], 2, 16, QChar('0'))
                    .arg(row.bPhysAddr[4], 2, 16, QChar('0'))
                    .arg(row.bPhysAddr[5], 2, 16, QChar('0'))
                    .toUpper();
        if (d.mac == QLatin1String("00:00:00:00:00:00"))
            continue;
        d.sources << QStringLiteral("arp");
        d.online = false;
        out.append(d);
    }
    return out;
}
#else
QVector<Device> readArpTable(const LanContext&)
{
    QVector<Device> out;
    QFile f(QStringLiteral("/proc/net/arp"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    f.readLine();
    while (!f.atEnd()) {
        const auto parts = QString::fromUtf8(f.readLine())
                               .split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 6)
            continue;
        if (parts[3] == QLatin1String("00:00:00:00:00:00"))
            continue;
        Device d;
        d.ip = parts[0];
        d.mac = parts[3].toUpper();
        d.sources << QStringLiteral("arp");
        d.online = false;
        out.append(d);
    }
    return out;
}
#endif

void primeArpCache(const LanContext& ctx, int timeoutMs, int maxParallel)
{
    if (!ctx.valid())
        return;
#ifdef Q_OS_WIN
    const QString bin = QStringLiteral("ping");
    const QStringList argsBase = {QStringLiteral("-n"), QStringLiteral("1"),
                                  QStringLiteral("-w"), QString::number(qMax(50, timeoutMs))};
#else
    const QString bin = QStringLiteral("ping");
    const QStringList argsBase = {QStringLiteral("-c"), QStringLiteral("1"),
                                  QStringLiteral("-W"), QString::number(qMax(1, timeoutMs / 1000))};
#endif
    std::atomic<int> running{0};
    std::vector<std::thread> workers;
    workers.reserve(size_t(qMax(1, int(ctx.lastHost - ctx.firstHost + 1))));
    for (quint32 h = ctx.firstHost; h <= ctx.lastHost; ++h) {
        while (running.load() >= maxParallel)
            QThread::msleep(2);
        running.fetch_add(1);
        const QString ip = ipv4String(h);
        workers.emplace_back([&, ip]() {
            QProcess::execute(bin, QStringList(argsBase) << ip);
            running.fetch_sub(1);
        });
    }
    for (auto& t : workers)
        t.join();
}
