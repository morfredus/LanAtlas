#pragma once

#include <QByteArray>

// AES-128-CBC + PKCS7. Necessaire pour le login luci Deco recent (enveloppe
// sign/data). Pas OpenSSL : une DLL de plus a deployer sous Windows.
namespace AesCbc {
QByteArray encrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& plain);
QByteArray decrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& cipher);
}