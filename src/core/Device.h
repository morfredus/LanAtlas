#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>
#include <QList>
#include <QVector>
#include "core/MorfService.h"

QString normalizedMac(QString mac);
bool jsonFlag(const QJsonValue& v, bool fallback);
// MAC "localement administree" (bit 0x02 du 1er octet) : typiquement une adresse
// privee/aleatoire de smartphone -> pas de fabricant OUI a esperer.
bool isRandomizedMac(const QString& mac);

// Equipement vu sur le LAN. Cle primaire : MAC si connue, sinon IP.
struct Device {
    QString ip;
    QString mac;
    QString hostname;
    QString vendor;
    QString model;
    QString category;          // box, mesh_node, client, unknown
    QString osGuess;
    QString parentId;          // MAC ou device_id Deco
    QString parentName;
    QString connection;        // ethernet, wifi 2.4, wifi 5, ...
    QString band;
    QString ssid;
    QString deviceId;          // identifiant interne Deco (owner_id)
    QStringList sources;
    QStringList services;
    QStringList capabilities;  // morfBeacon + indices LAN
    QStringList morfApps;      // applications entendues sur cette IP
    QList<int> openPorts;
    QJsonObject extra;         // tout le reste (API brute)
    int     rssi = 0;
    bool    online = false;    // preuve de presence actuelle, pas un defaut optimiste
    bool    infrastructure = false;
    enum class Presence { Connected, Historical, Technical };
    Presence presence = Presence::Historical;
    QString notes;
    QDateTime lastSeen;
    QString customName;        // nom donne par l'utilisateur (persistant)
    bool justAppeared = false; // apparu depuis le dernier scan (transitoire)
    bool justGone = false;     // disparu depuis le dernier scan (transitoire)

    QString id() const;
    QString typeEmoji() const; // pictogramme selon la categorie/services
    QString displayName() const;   // jamais vide
    QString presenceLabel() const;
    QString proofShort() const;    // beacon, /status, ou tiret
    QString linkShort() const;     // wifi 5 / ethernet, compact pour la carte
    QJsonObject toJson() const;
    QString detailText() const;    // fiche complete pour le panneau
    void mergeFrom(const Device& other);
};

Device deviceFromJson(const QJsonObject& o);   // inverse de Device::toJson
void relinkDeviceParents(QVector<Device>& devices);
void attachMorfServices(QVector<Device>& devices, const QVector<MorfService>& services);
void classifyPresence(QVector<Device>& devices);
