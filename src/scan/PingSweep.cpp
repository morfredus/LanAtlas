#include "scan/PingSweep.h"
#include <QProcess>
#include <QThread>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

QVector<Device> pingSweep(const QStringList& ips, int timeoutMs, int maxParallel)
{
#ifdef Q_OS_WIN
    const QString bin = QStringLiteral("ping");
    const QStringList base = {QStringLiteral("-n"), QStringLiteral("1"),
                              QStringLiteral("-w"), QString::number(qMax(50, timeoutMs))};
#else
    const QString bin = QStringLiteral("ping");
    const QStringList base = {QStringLiteral("-c"), QStringLiteral("1"),
                              QStringLiteral("-W"), QString::number(qMax(1, timeoutMs / 1000))};
#endif
    QVector<Device> hits;
    std::mutex mu;
    std::atomic<int> running{0};
    std::vector<std::thread> workers;
    for (const QString& ip : ips) {
        while (running.load() >= maxParallel)
            QThread::msleep(2);
        running.fetch_add(1);
        workers.emplace_back([&, ip]() {
            const int code = QProcess::execute(bin, QStringList(base) << ip);
            if (code == 0) {
                Device d;
                d.ip = ip;
                d.online = true;
                d.sources << QStringLiteral("icmp");
                std::lock_guard<std::mutex> lock(mu);
                hits.append(d);
            }
            running.fetch_sub(1);
        });
    }
    for (auto& t : workers)
        t.join();
    return hits;
}
