#pragma once

#include <QByteArray>

// AES-128 en mode ECB. Tuya chiffre ses trames LAN en ECB : d'abord le broadcast
// de decouverte UDP 6667, puis (phase pilotage) le protocole de controle 3.3 sur
// TCP 6668. Reutilise le coeur AES de AesCbc.cpp, donc aucune DLL OpenSSL a livrer.
namespace AesEcb {
// Dechiffre puis retire le padding PKCS7. Rend un tableau vide si l'entree est
// invalide (cle != 16 octets, longueur non multiple de 16, padding incoherent).
QByteArray decrypt(const QByteArray& key16, const QByteArray& cipher);
// Chiffre avec padding PKCS7. Utile pour envoyer une commande a l'appareil.
QByteArray encrypt(const QByteArray& key16, const QByteArray& plain);
}
