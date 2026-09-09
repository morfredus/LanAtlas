#pragma once

#include <QHostAddress>
#include <QString>

struct LanContext {
    QHostAddress localIp;
    QHostAddress netmask;
    QHostAddress gateway;
    QHostAddress broadcast;
    QString interfaceName;
    quint32 firstHost = 0;
    quint32 lastHost = 0;
    QString cidr;

    bool valid() const { return !localIp.isNull() && firstHost != 0; }
};

LanContext detectLanContext();
QString ipv4String(quint32 host);
