#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Libelles LAN : ports connus, nature d'une IP. Pas de requete reseau.
namespace NetHints {
QString wellKnownPort(int port);
QString namedPorts(const QList<int>& ports);
QString ipKind(const QString& ip);
QString ouiCompact(const QString& mac);
bool isTechnicalIp(const QString& ip);
// Port HTTP plausible pour une qualification /status (pas une identite).
bool isHttpQualifyPort(int port);
}
