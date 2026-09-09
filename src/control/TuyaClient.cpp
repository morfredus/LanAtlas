#include "control/TuyaClient.h"
#include "crypto/AesGcm.h"

#include <QTcpSocket>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QDateTime>
#include <QtEndian>
#include <QRandomGenerator>

namespace {

// Trame Tuya 3.5 : prefixe et suffixe du format "6699".
constexpr quint32 kPrefix6699 = 0x00006699;
constexpr quint32 kSuffix6699 = 0x00009966;

// Commandes utilisees (voir command_types Tuya).
constexpr quint32 kSessStart = 3;    // SESS_KEY_NEG_START
constexpr quint32 kSessResp = 4;     // SESS_KEY_NEG_RESP
constexpr quint32 kSessFinish = 5;   // SESS_KEY_NEG_FINISH
constexpr quint32 kControlNew = 0x0d;  // CONTROL_NEW : ecrire des dps
constexpr quint32 kDpQueryNew = 0x10;  // DP_QUERY_NEW : lire l'etat

QByteArray hmacSha256(const QByteArray& key, const QByteArray& msg)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256);
    mac.setKey(key);
    mac.addData(msg);
    return mac.result();
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32*>(b.data()), n / 4);
    return b;
}

void appendBE32(QByteArray& b, quint32 v)
{
    char buf[4];
    qToBigEndian(v, buf);
    b.append(buf, 4);
}

} // namespace

TuyaClient::TuyaClient(QString host, QByteArray deviceId, QByteArray localKey)
    : host_(std::move(host)), deviceId_(std::move(deviceId)), localKey_(std::move(localKey))
{
}

TuyaClient::~TuyaClient()
{
    close();
}

// Construit une trame 6699 chiffree : entete + iv + (chiffre + tag) + suffixe.
// L'entete apres le prefixe (14 octets) sert de donnees authentifiees (AAD).
QByteArray TuyaClient::packFrame(quint32 cmd, const QByteArray& payload, const QByteArray& key)
{
    QByteArray header;
    appendBE32(header, kPrefix6699);
    header.append(char(0));
    header.append(char(0));            // champ "inconnu" (2 octets a zero)
    appendBE32(header, seqno_);
    appendBE32(header, cmd);
    // longueur = iv(12) + chiffre(=payload) + tag(16), hors suffixe.
    appendBE32(header, quint32(payload.size() + 12 + 16));

    const QByteArray aad = header.mid(4);          // tout l'entete apres le prefixe
    const QByteArray iv = randomBytes(12);
    const QByteArray ctTag = AesGcm::encrypt(key, iv, aad, payload);  // chiffre || tag

    QByteArray frame = header;
    frame += iv;
    frame += ctTag;
    appendBE32(frame, kSuffix6699);
    return frame;
}

bool TuyaClient::readExact(QByteArray& into, int n, int timeoutMs)
{
    QDateTime deadline = QDateTime::currentDateTime().addMSecs(timeoutMs);
    while (into.size() < n) {
        if (sock_->bytesAvailable() > 0) {
            into += sock_->read(n - into.size());
            continue;
        }
        const int remaining = int(QDateTime::currentDateTime().msecsTo(deadline));
        if (remaining <= 0 || !sock_->waitForReadyRead(remaining)) {
            error_ = QStringLiteral("delai depasse en lecture");
            return false;
        }
    }
    return true;
}

bool TuyaClient::readFrame(quint32& cmd, QByteArray& plain, const QByteArray& key, int timeoutMs)
{
    QByteArray header;
    if (!readExact(header, 18, timeoutMs))
        return false;
    const auto* p = reinterpret_cast<const unsigned char*>(header.constData());
    const quint32 prefix = qFromBigEndian<quint32>(p);
    if (prefix != kPrefix6699) {
        error_ = QStringLiteral("prefixe de trame inattendu");
        return false;
    }
    cmd = qFromBigEndian<quint32>(p + 10);
    const quint32 length = qFromBigEndian<quint32>(p + 14);
    if (length < 12 + 16 || length > 65535) {
        error_ = QStringLiteral("longueur de trame invalide");
        return false;
    }

    QByteArray body;
    if (!readExact(body, int(length) + 4, timeoutMs))  // + suffixe (4 octets)
        return false;

    const QByteArray iv = body.left(12);
    const QByteArray tag = body.mid(int(length) - 16, 16);
    const QByteArray cipher = body.mid(12, int(length) - 12 - 16);
    const QByteArray aad = header.mid(4);
    plain = AesGcm::decrypt(key, iv, aad, cipher, tag);
    if (plain.isEmpty() && !cipher.isEmpty()) {
        error_ = QStringLiteral("dechiffrement/tag invalide (cle ou version ?)");
        return false;
    }
    return true;
}

bool TuyaClient::sendPayload(quint32 cmd, const QByteArray& payload, const QByteArray& key)
{
    const QByteArray frame = packFrame(cmd, payload, key);
    ++seqno_;
    if (sock_->write(frame) != frame.size() || !sock_->waitForBytesWritten(3000)) {
        error_ = QStringLiteral("echec d'ecriture socket");
        return false;
    }
    return true;
}

bool TuyaClient::connectAndNegotiate(int timeoutMs)
{
    if (localKey_.size() != 16) {
        error_ = QStringLiteral("local key absente ou de taille invalide (16 attendus)");
        return false;
    }
    close();
    sock_ = new QTcpSocket();
    sock_->connectToHost(host_, 6668);
    if (!sock_->waitForConnected(timeoutMs)) {
        error_ = QStringLiteral("connexion TCP 6668 impossible : ") + sock_->errorString();
        return false;
    }
    seqno_ = 1;

    // Etape 1 : envoyer notre nonce (16 octets), chiffre avec la local key.
    const QByteArray localNonce = randomBytes(16);
    if (!sendPayload(kSessStart, localNonce, localKey_))
        return false;

    // Etape 2 : reponse = nonce distant (16) + HMAC-SHA256(localKey, localNonce).
    quint32 cmd = 0;
    QByteArray resp;
    if (!readFrame(cmd, resp, localKey_, timeoutMs))
        return false;
    if (cmd != kSessResp || resp.size() < 48) {
        error_ = QStringLiteral("reponse de negociation inattendue");
        return false;
    }
    // Certaines reponses portent un retcode de 4 octets en tete : on cale l'offset
    // sur celui dont le HMAC du nonce local concorde.
    const QByteArray wantHmac = hmacSha256(localKey_, localNonce);
    int off = -1;
    for (int candidate : {0, 4}) {
        if (resp.size() >= candidate + 48
            && resp.mid(candidate + 16, 32) == wantHmac) {
            off = candidate;
            break;
        }
    }
    if (off < 0) {
        error_ = QStringLiteral("HMAC de negociation invalide (local key incorrecte ?)");
        return false;
    }
    const QByteArray remoteNonce = resp.mid(off, 16);

    // Etape 3 : renvoyer HMAC-SHA256(localKey, remoteNonce).
    if (!sendPayload(kSessFinish, hmacSha256(localKey_, remoteNonce), localKey_))
        return false;

    // Cle de session = GCM(localKey, iv=localNonce[:12], plain=localNonce XOR remoteNonce),
    // on garde les 16 octets de chiffre (sans le tag).
    QByteArray x(16, 0);
    for (int i = 0; i < 16; ++i)
        x[i] = char(localNonce[i] ^ remoteNonce[i]);
    const QByteArray ctTag = AesGcm::encrypt(localKey_, localNonce.left(12), QByteArray(), x);
    if (ctTag.size() < 16) {
        error_ = QStringLiteral("derivation de cle de session echouee");
        return false;
    }
    sessionKey_ = ctTag.left(16);
    error_.clear();
    return true;
}

void TuyaClient::drainPending()
{
    while (sock_ && sock_->bytesAvailable() >= 22) {
        quint32 cmd = 0;
        QByteArray plain;
        if (!readFrame(cmd, plain, sessionKey_, 200))
            break;
    }
}

QJsonObject TuyaClient::queryStatus(bool* ok)
{
    if (ok)
        *ok = false;
    if (sessionKey_.size() != 16) {
        error_ = QStringLiteral("session non etablie");
        return {};
    }
    // Ecarter d'abord un eventuel statut pousse spontanement, sinon on le lirait
    // a la place de la reponse a notre requete.
    drainPending();

    // Payload de requete pour la 3.5 : objet JSON vide.
    if (!sendPayload(kDpQueryNew, QByteArrayLiteral("{}"), sessionKey_))
        return {};

    // Plusieurs trames peuvent arriver (accuse puis statut) : on garde la premiere
    // qui contient reellement des datapoints.
    for (int attempt = 0; attempt < 4; ++attempt) {
        quint32 cmd = 0;
        QByteArray plain;
        if (!readFrame(cmd, plain, sessionKey_, 5000))
            return {};
        const int brace = plain.indexOf('{');
        if (brace < 0)
            continue;
        const QJsonObject root = QJsonDocument::fromJson(plain.mid(brace)).object();
        // La 3.5 encapsule l'etat dans {"dps":{...}} (parfois sous "data").
        QJsonObject dps = root.value(QStringLiteral("dps")).toObject();
        if (dps.isEmpty())
            dps = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("dps")).toObject();
        if (!dps.isEmpty()) {
            if (ok)
                *ok = true;
            return dps;
        }
    }
    error_ = QStringLiteral("aucune reponse avec datapoints");
    return {};
}

bool TuyaClient::setDps(const QJsonObject& dps)
{
    if (sessionKey_.size() != 16) {
        error_ = QStringLiteral("session non etablie");
        return false;
    }
    // {"protocol":5,"t":<epoch>,"data":{"dps":{...}}}, sans espaces. L'ordre des
    // cles compte pour la firmware : on l'assemble a la main (QJsonObject trierait
    // les cles par ordre alphabetique, ce que l'appareil refuse en silence).
    const QByteArray dpsJson = QJsonDocument(dps).toJson(QJsonDocument::Compact).trimmed();
    const QByteArray json =
        QByteArrayLiteral("{\"protocol\":5,\"t\":")
        + QByteArray::number(qint64(QDateTime::currentSecsSinceEpoch()))
        + QByteArrayLiteral(",\"data\":{\"dps\":") + dpsJson + QByteArrayLiteral("}}");

    // Les commandes de controle (contrairement a DP_QUERY) doivent etre prefixees
    // par l'entete de version : "3.5" suivi de 12 octets nuls. Sans lui, l'appareil
    // acquitte la trame mais n'applique jamais le changement.
    const QByteArray payload =
        QByteArrayLiteral("3.5") + QByteArray(12, '\0') + json;

    drainPending();  // ecarter un statut en attente avant d'emettre
    if (!sendPayload(kControlNew, payload, sessionKey_))
        return false;

    // La commande renvoie un accuse puis un statut : on lit l'accuse et on vide le
    // reste pour laisser la connexion propre pour la prochaine operation.
    quint32 cmd = 0;
    QByteArray plain;
    readFrame(cmd, plain, sessionKey_, 3000);
    drainPending();
    return true;
}

void TuyaClient::close()
{
    if (sock_) {
        sock_->abort();
        delete sock_;
        sock_ = nullptr;
    }
    sessionKey_.clear();
}
