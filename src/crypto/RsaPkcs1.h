#pragma once

#include <QByteArray>
#include <QString>

// RSA PKCS#1 v1.5 - chiffrement d'un petit secret (mot de passe Deco).
// Pourquoi un mini-moteur : eviter une dependance OpenSSL juste pour un login.
namespace RsaPkcs1 {
QByteArray encrypt(const QByteArray& modulusHex,
                   const QByteArray& exponentHex,
                   const QByteArray& plaintext,
                   QString* error = nullptr);
}
