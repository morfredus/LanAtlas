#include "core/MorfService.h"

bool MorfService::stale(int maxAgeSec) const
{
    if (!lastSeen.isValid())
        return true;
    return lastSeen.secsTo(QDateTime::currentDateTime()) > maxAgeSec;
}

QString MorfService::statusUrl() const
{
    if (ip.isEmpty() || statusPort == 0)
        return {};
    return QStringLiteral("http://%1:%2/status").arg(ip).arg(statusPort);
}
