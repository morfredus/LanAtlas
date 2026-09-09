#pragma once

#include <QByteArray>

// AES-128-GCM. Tuya est passe a ce mode pour le protocole LAN 3.5 : le broadcast
// de decouverte au format "6699" est chiffre en GCM (nonce 12 octets, tag 16),
// et le protocole de controle 3.5 sur TCP 6668 l'utilise aussi. Reutilise le
// coeur AES de AesCbc.cpp (chiffrement de bloc + expansion de cle), donc pas de
// dependance OpenSSL a livrer.
namespace AesGcm {
// Dechiffre et VERIFIE le tag d'authentification. Rend un tableau vide si le tag
// est invalide (donnees alterees ou mauvaise cle) ou si une taille est invalide.
// nonce : 12 octets. tag : 16 octets. aad : donnees authentifiees non chiffrees.
QByteArray decrypt(const QByteArray& key16, const QByteArray& nonce12,
                   const QByteArray& aad, const QByteArray& cipher,
                   const QByteArray& tag);

// Chiffre et rend le chiffre suivi du tag d'authentification (16 octets) :
// resultat = ciphertext (meme longueur que plain) || tag. Le nonce (12 octets)
// doit etre unique pour un couple (cle, message). Vide si une taille est invalide.
QByteArray encrypt(const QByteArray& key16, const QByteArray& nonce12,
                   const QByteArray& aad, const QByteArray& plain);
}
