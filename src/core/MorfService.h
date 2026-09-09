#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Une instance entendue sur 45454/UDP (contrat morfbeacon/1).
struct MorfService {
    QString key;              // instance, sinon app@ip
    QString app;
    QString host;
    QString ip;
    QString version;
    QString state;
    QString role;
    quint16 statusPort = 0;
    QStringList capabilities;
    QDateTime lastSeen;
    QJsonObject datagram;
    int addressScore = 0;

    bool stale(int maxAgeSec = 45) const;
    QString statusUrl() const;
};
