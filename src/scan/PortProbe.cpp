#include "scan/PortProbe.h"
#include "core/Device.h"
#include "core/NetHints.h"

#include <QTcpSocket>
#include <QThread>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

static const int kQuickPorts[] = {80, 443, 22, 53, 139, 445, 8080};
static const int kDeepPorts[] = {
    22, 53, 80, 139, 443, 445, 515, 548, 554, 631,
    1883, 3389, 5000, 5357, 5900, 8006, 8080, 8096, 8123,
    8443, 8787, 8788, 8789, 8790, 8791, 8883, 8888, 9100, 32400
};

static bool skipIp(const QString& ip)
{
    if (ip.isEmpty() || ip.startsWith(QLatin1String("127.")))
        return true;
    return NetHints::isTechnicalIp(ip);
}

static bool liveboxSaysAbsent(const Device& d)
{
    if (!d.sources.contains(QLatin1String("livebox")))
        return false;
    const bool has = d.extra.contains(QStringLiteral("Active"))
                     || d.extra.contains(QStringLiteral("active"));
    if (!has)
        return false;
    return !jsonFlag(d.extra.value(QStringLiteral("Active")),
                     jsonFlag(d.extra.value(QStringLiteral("active")), false));
}

static bool tryTcp(const QString& ip, quint16 port, int timeoutMs)
{
    // abort() est obligatoire : si waitForConnected echoue, le socket reste
    // "en connexion" et le destructeur Qt peut bloquer plusieurs secondes.
    QTcpSocket sock;
    sock.connectToHost(ip, port);
    const bool ok = sock.waitForConnected(qMax(30, timeoutMs));
    sock.abort();
    return ok;
}

void probePorts(QVector<Device>& devices, int timeoutMs, bool deep,
                const std::function<void(int done, int total)>& onProgress)
{
    const int* ports = deep ? kDeepPorts : kQuickPorts;
    const int nPorts = deep ? int(sizeof(kDeepPorts) / sizeof(kDeepPorts[0]))
                            : int(sizeof(kQuickPorts) / sizeof(kQuickPorts[0]));

    int targets = 0;
    for (const Device& d : devices) {
        if (!skipIp(d.ip) && !liveboxSaysAbsent(d))
            ++targets;
    }
    if (targets == 0)
        return;

    int finishedHosts = 0;
    const int portWorkers = qBound(4, nPorts, 12);

    for (Device& d : devices) {
        if (skipIp(d.ip) || liveboxSaysAbsent(d))
            continue;

        std::atomic<int> next{0};
        std::mutex mu;
        auto worker = [&]() {
            while (true) {
                const int i = next.fetch_add(1);
                if (i >= nPorts)
                    break;
                const int port = ports[i];
                if (!tryTcp(d.ip, quint16(port), timeoutMs))
                    continue;
                std::lock_guard<std::mutex> lock(mu);
                if (!d.openPorts.contains(port))
                    d.openPorts.append(port);
                if ((port == 80 || port == 443 || port == 8080 || port == 8443)
                    && !d.services.contains(QLatin1String("http")))
                    d.services << QStringLiteral("http");
                if (port == 22 && !d.services.contains(QLatin1String("ssh")))
                    d.services << QStringLiteral("ssh");
                if ((port == 445 || port == 139)
                    && !d.services.contains(QLatin1String("smb")))
                    d.services << QStringLiteral("smb");
                if (port == 1883 && !d.services.contains(QLatin1String("mqtt")))
                    d.services << QStringLiteral("mqtt");
                if (port == 8883 && !d.services.contains(QLatin1String("mqtt-tls")))
                    d.services << QStringLiteral("mqtt-tls");
                if (port == 3389 && !d.services.contains(QLatin1String("rdp")))
                    d.services << QStringLiteral("rdp");
                if (port == 8123 && !d.services.contains(QLatin1String("http-8123")))
                    d.services << QStringLiteral("http-8123");
                if (port == 32400 && !d.services.contains(QLatin1String("plex")))
                    d.services << QStringLiteral("plex");
            }
        };

        std::vector<std::thread> pool;
        for (int w = 0; w < portWorkers; ++w)
            pool.emplace_back(worker);
        for (auto& t : pool)
            t.join();

        if (!d.openPorts.isEmpty() && !d.sources.contains(QLatin1String("ports")))
            d.sources << QStringLiteral("ports");

        ++finishedHosts;
        if (onProgress)
            onProgress(finishedHosts, targets);
    }
}
