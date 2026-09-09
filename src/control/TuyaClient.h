#pragma once

#include <QByteArray>
#include <QString>
#include <QJsonObject>

class QTcpSocket;

// Client LAN minimal pour un appareil Tuya en protocole 3.5 (le seul cas de la
// bande LED du parc). Volontairement reduit a ce dont on a besoin : se connecter,
// negocier la cle de session, lire l'etat, ecrire des datapoints. Bloquant, a
// utiliser dans un thread de travail (jamais sur le thread UI).
//
// Le protocole 3.5 : chaque trame est au format "6699" chiffre en AES-GCM. Une
// poignee de session (nonces + HMAC-SHA256) etablit d'abord une cle de session
// derivee de la local key ; toutes les commandes suivantes l'utilisent.
class TuyaClient {
public:
    TuyaClient(QString host, QByteArray deviceId, QByteArray localKey);
    ~TuyaClient();

    // Connexion TCP (port 6668) + negociation de session. Faux + error() sinon.
    bool connectAndNegotiate(int timeoutMs = 5000);

    // Lit l'etat courant (DP_QUERY). Rend les datapoints {"20":false,...}.
    // ok (optionnel) passe a faux en cas d'echec.
    QJsonObject queryStatus(bool* ok = nullptr);

    // Ecrit un ou plusieurs datapoints (CONTROL). Ex. {"20": true, "22": 500}.
    bool setDps(const QJsonObject& dps);

    void close();
    QString error() const { return error_; }

private:
    QByteArray packFrame(quint32 cmd, const QByteArray& payload, const QByteArray& key);
    // Lit une trame complete et la dechiffre avec key. Rend le texte clair
    // (retcode eventuel non retire : l'appelant cherche le JSON).
    bool readFrame(quint32& cmd, QByteArray& plain, const QByteArray& key, int timeoutMs);
    bool readExact(QByteArray& into, int n, int timeoutMs);
    bool sendPayload(quint32 cmd, const QByteArray& payload, const QByteArray& key);
    // Vide les trames deja arrivees (l'appareil pousse un statut non sollicite
    // apres chaque commande : sans ce drainage, la lecture suivante se decale).
    void drainPending();

    QString    host_;
    QByteArray deviceId_;
    QByteArray localKey_;    // 16 octets
    QByteArray sessionKey_;  // 16 octets, valide apres negociation
    QTcpSocket* sock_ = nullptr;
    quint32    seqno_ = 1;
    QString    error_;
};
