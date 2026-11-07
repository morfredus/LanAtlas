#pragma once

#include "core/Device.h"

#include <QObject>
#include <QHash>
#include <QDateTime>
#include <QVector>
#include <QString>

class QUdpSocket;

// Une annonce Tuya entendue en broadcast UDP. Les appareils Tuya (ampoules,
// bandes LED, prises...) diffusent leur presence toutes les ~10 s vers le
// broadcast du LAN : port 6666 en clair (firmwares 3.1) ou 6667 chiffre (3.2+).
struct TuyaAnnounce {
    QString   ip;
    QString   deviceId;    // gwId : identifiant unique de l'appareil chez Tuya
    QString   productKey;  // reference du produit (famille de materiel)
    QString   version;     // "3.1", "3.3", "3.4"... = version du protocole LAN
    bool      encrypted = false;  // vrai si entendu chiffre (6667)
    QDateTime lastSeen;
};

// Plafond de la table des annonces et duree de conservation : le port UDP est
// ouvert a tout le LAN, un emetteur hostile ne doit pas pouvoir faire grossir la
// memoire sans limite.
constexpr int kMaxTuyaAnnounces = 512;
constexpr int kTuyaAnnounceTtlSecs = 300;

// Purge les annonces de plus de 5 minutes, puis, si la table depasse encore le
// plafond, retire les plus anciennes. Pure : testable sans reseau.
inline void pruneAnnounces(QHash<QString, TuyaAnnounce>& table, const QDateTime& now,
                           int maxEntries = kMaxTuyaAnnounces)
{
    for (auto it = table.begin(); it != table.end();) {
        if (it->lastSeen.secsTo(now) > kTuyaAnnounceTtlSecs)
            it = table.erase(it);
        else
            ++it;
    }
    while (table.size() > maxEntries) {
        auto oldest = table.begin();
        for (auto it = table.begin(); it != table.end(); ++it) {
            if (it->lastSeen < oldest->lastSeen)
                oldest = it;
        }
        table.erase(oldest);
    }
}

// Ecoute permanente des annonces Tuya sur le LAN. Meme esprit que BeaconListener
// pour morfBeacon : rien a sonder, les appareils se signalent d'eux-memes. On
// binde des l'ouverture de l'application pour avoir capte les broadcasts avant
// le premier scan (fenetre d'emission d'environ 10 s).
class TuyaListener : public QObject {
    Q_OBJECT
public:
    explicit TuyaListener(QObject* parent = nullptr);

    // Ouvre les deux sockets UDP. Rend faux si aucun des deux ports n'a pu etre
    // bind (peu probable en ShareAddress, mais on ne bloque jamais l'appli).
    bool start();

    QVector<TuyaAnnounce> announces() const;

signals:
    void changed();

private slots:
    void onReadyRead();

private:
    void drain(QUdpSocket* sock);
    void ingest(const QByteArray& datagram, const QString& fromIp);

    QUdpSocket* sock6666_ = nullptr;  // broadcast en clair
    QUdpSocket* sock6667_ = nullptr;  // broadcast chiffre (AES-ECB, cle fixe)
    QHash<QString, TuyaAnnounce> byIp_;  // derniere annonce connue par IP
};

// Superpose l'identite Tuya sur l'inventaire, par correspondance d'IP, comme
// attachMorfServices le fait pour les services morfSystem. Enrichit un appareil
// deja decouvert (ARP, ping...) ou en ajoute un si seul le broadcast l'a revele.
void attachTuyaDevices(QVector<Device>& devices, const QVector<TuyaAnnounce>& announces);
