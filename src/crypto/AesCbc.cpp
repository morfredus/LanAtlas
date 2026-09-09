#include "crypto/AesCbc.h"
#include "crypto/AesEcb.h"
#include "crypto/AesGcm.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// AES-128 d'apres FIPS-197, CBC, padding PKCS7. Intentionnellement petit.

static const unsigned char sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static const unsigned char invs[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

static unsigned char xtime(unsigned char x)
{
    return static_cast<unsigned char>((x << 1) ^ ((x >> 7) * 0x1b));
}

static unsigned char mul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1)
            p ^= a;
        const unsigned char hi = a & 0x80;
        a = static_cast<unsigned char>(a << 1);
        if (hi)
            a ^= 0x1b;
        b = static_cast<unsigned char>(b >> 1);
    }
    return p;
}

static void expand(const unsigned char* key, unsigned char rk[176])
{
    std::memcpy(rk, key, 16);
    static const unsigned char rcon[] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};
    int bytes = 16;
    int r = 1;
    unsigned char t[4];
    while (bytes < 176) {
        std::memcpy(t, rk + bytes - 4, 4);
        if (bytes % 16 == 0) {
            const unsigned char tmp = t[0];
            t[0] = static_cast<unsigned char>(sbox[t[1]] ^ rcon[r]);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[tmp];
            ++r;
        }
        for (int i = 0; i < 4; ++i)
            rk[bytes + i] = static_cast<unsigned char>(rk[bytes - 16 + i] ^ t[i]);
        bytes += 4;
    }
}

static void addRound(unsigned char* s, const unsigned char* rk)
{
    for (int i = 0; i < 16; ++i)
        s[i] ^= rk[i];
}

static void subBytes(unsigned char* s)
{
    for (int i = 0; i < 16; ++i)
        s[i] = sbox[s[i]];
}

static void invSub(unsigned char* s)
{
    for (int i = 0; i < 16; ++i)
        s[i] = invs[s[i]];
}

static void shiftRows(unsigned char* s)
{
    unsigned char t;
    t = s[1];
    s[1] = s[5];
    s[5] = s[9];
    s[9] = s[13];
    s[13] = t;
    t = s[2];
    s[2] = s[10];
    s[10] = t;
    t = s[6];
    s[6] = s[14];
    s[14] = t;
    t = s[15];
    s[15] = s[11];
    s[11] = s[7];
    s[7] = s[3];
    s[3] = t;
}

static void invShift(unsigned char* s)
{
    unsigned char t;
    t = s[13];
    s[13] = s[9];
    s[9] = s[5];
    s[5] = s[1];
    s[1] = t;
    t = s[2];
    s[2] = s[10];
    s[10] = t;
    t = s[6];
    s[6] = s[14];
    s[14] = t;
    t = s[3];
    s[3] = s[7];
    s[7] = s[11];
    s[11] = s[15];
    s[15] = t;
}

static void mix(unsigned char* s)
{
    for (int c = 0; c < 4; ++c) {
        unsigned char* p = s + 4 * c;
        const unsigned char a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = static_cast<unsigned char>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
        p[1] = static_cast<unsigned char>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
        p[2] = static_cast<unsigned char>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
        p[3] = static_cast<unsigned char>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
    }
}

static void invMix(unsigned char* s)
{
    for (int c = 0; c < 4; ++c) {
        unsigned char* p = s + 4 * c;
        const unsigned char a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = static_cast<unsigned char>(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
        p[1] = static_cast<unsigned char>(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
        p[2] = static_cast<unsigned char>(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
        p[3] = static_cast<unsigned char>(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
    }
}

static void encryptBlock(unsigned char* s, const unsigned char rk[176])
{
    addRound(s, rk);
    for (int r = 1; r < 10; ++r) {
        subBytes(s);
        shiftRows(s);
        mix(s);
        addRound(s, rk + 16 * r);
    }
    subBytes(s);
    shiftRows(s);
    addRound(s, rk + 160);
}

static void decryptBlock(unsigned char* s, const unsigned char rk[176])
{
    addRound(s, rk + 160);
    for (int r = 9; r >= 1; --r) {
        invShift(s);
        invSub(s);
        addRound(s, rk + 16 * r);
        invMix(s);
    }
    invShift(s);
    invSub(s);
    addRound(s, rk);
}

static QByteArray pkcs7(const QByteArray& in)
{
    const int pad = 16 - (in.size() % 16);
    return in + QByteArray(pad, char(pad));
}

static QByteArray unpkcs7(const QByteArray& in)
{
    if (in.isEmpty() || in.size() % 16)
        return {};
    const int pad = static_cast<unsigned char>(in.back());
    if (pad < 1 || pad > 16 || in.size() < pad)
        return {};
    return in.left(in.size() - pad);
}

QByteArray AesCbc::encrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& plain)
{
    if (key16.size() != 16 || iv16.size() != 16)
        return {};
    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);
    QByteArray buf = pkcs7(plain);
    unsigned char prev[16];
    std::memcpy(prev, iv16.constData(), 16);
    QByteArray out(buf.size(), 0);
    for (int i = 0; i < buf.size(); i += 16) {
        unsigned char block[16];
        std::memcpy(block, buf.constData() + i, 16);
        for (int j = 0; j < 16; ++j)
            block[j] ^= prev[j];
        encryptBlock(block, rk);
        std::memcpy(prev, block, 16);
        std::memcpy(out.data() + i, block, 16);
    }
    return out;
}

QByteArray AesCbc::decrypt(const QByteArray& key16, const QByteArray& iv16, const QByteArray& cipher)
{
    if (key16.size() != 16 || iv16.size() != 16 || cipher.size() % 16)
        return {};
    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);
    unsigned char prev[16];
    std::memcpy(prev, iv16.constData(), 16);
    QByteArray buf(cipher.size(), 0);
    for (int i = 0; i < cipher.size(); i += 16) {
        unsigned char block[16];
        unsigned char raw[16];
        std::memcpy(raw, cipher.constData() + i, 16);
        std::memcpy(block, raw, 16);
        decryptBlock(block, rk);
        for (int j = 0; j < 16; ++j)
            block[j] ^= prev[j];
        std::memcpy(prev, raw, 16);
        std::memcpy(buf.data() + i, block, 16);
    }
    return unpkcs7(buf);
}

// --- AES-128-ECB -----------------------------------------------------------
// Meme cle de tour, mais chaque bloc est traite independamment (pas de
// chainage ni d'IV). C'est ce qu'utilise Tuya pour ses trames LAN.

QByteArray AesEcb::encrypt(const QByteArray& key16, const QByteArray& plain)
{
    if (key16.size() != 16)
        return {};
    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);
    QByteArray buf = pkcs7(plain);
    QByteArray out(buf.size(), 0);
    for (int i = 0; i < buf.size(); i += 16) {
        unsigned char block[16];
        std::memcpy(block, buf.constData() + i, 16);
        encryptBlock(block, rk);
        std::memcpy(out.data() + i, block, 16);
    }
    return out;
}

QByteArray AesEcb::decrypt(const QByteArray& key16, const QByteArray& cipher)
{
    if (key16.size() != 16 || cipher.isEmpty() || cipher.size() % 16)
        return {};
    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);
    QByteArray buf(cipher.size(), 0);
    for (int i = 0; i < cipher.size(); i += 16) {
        unsigned char block[16];
        std::memcpy(block, cipher.constData() + i, 16);
        decryptBlock(block, rk);
        std::memcpy(buf.data() + i, block, 16);
    }
    return unpkcs7(buf);
}

// --- AES-128-GCM -----------------------------------------------------------
// GCM = chiffrement en mode compteur (CTR) + authentification GHASH dans le
// corps de Galois GF(2^128). On ne s'appuie que sur le chiffrement de bloc AES
// deja present (encryptBlock) ; le dechiffrement de bloc n'est pas utilise.

// Multiplication de deux blocs de 128 bits dans GF(2^128), convention GCM :
// le bit de poids fort est le premier bit du premier octet, et la reduction se
// fait par le polynome R = 0xE1 suivi de zeros lors d'un decalage a droite.
static void gcmMul(unsigned char* x, const unsigned char* y)
{
    unsigned char z[16] = {0};
    unsigned char v[16];
    std::memcpy(v, y, 16);
    for (int i = 0; i < 128; ++i) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            for (int j = 0; j < 16; ++j)
                z[j] ^= v[j];
        }
        const unsigned char lsb = v[15] & 1;
        for (int j = 15; j > 0; --j)
            v[j] = static_cast<unsigned char>((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb)
            v[0] ^= 0xe1;   // reduction par le polynome de GCM
    }
    std::memcpy(x, z, 16);
}

// GHASH : accumule les donnees par blocs de 16 octets (completes par des zeros)
// dans l'etat s, chaque bloc etant XORe puis multiplie par le sous-cle H.
static void ghash(unsigned char* s, const unsigned char* h,
                  const unsigned char* data, int len)
{
    int off = 0;
    while (off < len) {
        const int n = std::min(16, len - off);
        for (int j = 0; j < n; ++j)
            s[j] ^= data[off + j];
        gcmMul(s, h);
        off += n;
    }
}

QByteArray AesGcm::decrypt(const QByteArray& key16, const QByteArray& nonce12,
                           const QByteArray& aad, const QByteArray& cipher,
                           const QByteArray& tag)
{
    if (key16.size() != 16 || nonce12.size() != 12 || tag.size() != 16)
        return {};

    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);

    // H = E(0^128), sous-cle d'authentification.
    unsigned char h[16] = {0};
    encryptBlock(h, rk);

    // J0 = nonce (12 octets) || compteur 0x00000001 (nonce de 96 bits).
    unsigned char j0[16] = {0};
    std::memcpy(j0, nonce12.constData(), 12);
    j0[15] = 1;

    // GHASH sur AAD puis sur le chiffre, puis sur les longueurs (en bits).
    unsigned char s[16] = {0};
    ghash(s, h, reinterpret_cast<const unsigned char*>(aad.constData()), aad.size());
    ghash(s, h, reinterpret_cast<const unsigned char*>(cipher.constData()), cipher.size());
    unsigned char lenBlock[16] = {0};
    const std::uint64_t aadBits = static_cast<std::uint64_t>(aad.size()) * 8;
    const std::uint64_t cBits = static_cast<std::uint64_t>(cipher.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        lenBlock[7 - i] = static_cast<unsigned char>((aadBits >> (8 * i)) & 0xff);
        lenBlock[15 - i] = static_cast<unsigned char>((cBits >> (8 * i)) & 0xff);
    }
    for (int j = 0; j < 16; ++j)
        s[j] ^= lenBlock[j];
    gcmMul(s, h);

    // Tag attendu = S XOR E(J0).
    unsigned char eJ0[16];
    std::memcpy(eJ0, j0, 16);
    encryptBlock(eJ0, rk);
    unsigned char calc[16];
    for (int j = 0; j < 16; ++j)
        calc[j] = static_cast<unsigned char>(s[j] ^ eJ0[j]);

    // Comparaison a temps constant : ne pas court-circuiter au premier octet.
    unsigned char diff = 0;
    for (int j = 0; j < 16; ++j)
        diff |= static_cast<unsigned char>(calc[j] ^ static_cast<unsigned char>(tag[j]));
    if (diff)
        return {};

    // Dechiffrement CTR : les blocs de donnees utilisent inc32(J0), inc32^2(J0)...
    // (E(J0) est reserve au tag, on incremente donc avant de chiffrer).
    unsigned char ctr[16];
    std::memcpy(ctr, j0, 16);
    QByteArray out(cipher.size(), 0);
    int off = 0;
    while (off < cipher.size()) {
        for (int k = 15; k >= 12; --k) {   // inc32 sur les 4 derniers octets
            if (++ctr[k])
                break;
        }
        unsigned char ks[16];
        std::memcpy(ks, ctr, 16);
        encryptBlock(ks, rk);
        const int n = std::min(16, static_cast<int>(cipher.size()) - off);
        for (int j = 0; j < n; ++j)
            out[off + j] = static_cast<char>(
                static_cast<unsigned char>(cipher[off + j]) ^ ks[j]);
        off += n;
    }
    return out;
}

QByteArray AesGcm::encrypt(const QByteArray& key16, const QByteArray& nonce12,
                           const QByteArray& aad, const QByteArray& plain)
{
    if (key16.size() != 16 || nonce12.size() != 12)
        return {};

    unsigned char rk[176];
    expand(reinterpret_cast<const unsigned char*>(key16.constData()), rk);

    unsigned char h[16] = {0};
    encryptBlock(h, rk);

    unsigned char j0[16] = {0};
    std::memcpy(j0, nonce12.constData(), 12);
    j0[15] = 1;

    // Chiffrement CTR a partir de inc32(J0).
    QByteArray cipher(plain.size(), 0);
    unsigned char ctr[16];
    std::memcpy(ctr, j0, 16);
    int off = 0;
    while (off < plain.size()) {
        for (int k = 15; k >= 12; --k) {
            if (++ctr[k])
                break;
        }
        unsigned char ks[16];
        std::memcpy(ks, ctr, 16);
        encryptBlock(ks, rk);
        const int n = std::min(16, static_cast<int>(plain.size()) - off);
        for (int j = 0; j < n; ++j)
            cipher[off + j] = static_cast<char>(
                static_cast<unsigned char>(plain[off + j]) ^ ks[j]);
        off += n;
    }

    // Tag = GHASH(AAD || cipher || longueurs) XOR E(J0).
    unsigned char s[16] = {0};
    ghash(s, h, reinterpret_cast<const unsigned char*>(aad.constData()), aad.size());
    ghash(s, h, reinterpret_cast<const unsigned char*>(cipher.constData()), cipher.size());
    unsigned char lenBlock[16] = {0};
    const std::uint64_t aadBits = static_cast<std::uint64_t>(aad.size()) * 8;
    const std::uint64_t cBits = static_cast<std::uint64_t>(cipher.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        lenBlock[7 - i] = static_cast<unsigned char>((aadBits >> (8 * i)) & 0xff);
        lenBlock[15 - i] = static_cast<unsigned char>((cBits >> (8 * i)) & 0xff);
    }
    for (int j = 0; j < 16; ++j)
        s[j] ^= lenBlock[j];
    gcmMul(s, h);
    unsigned char eJ0[16];
    std::memcpy(eJ0, j0, 16);
    encryptBlock(eJ0, rk);

    QByteArray out = cipher;
    out.reserve(cipher.size() + 16);
    for (int j = 0; j < 16; ++j)
        out.append(static_cast<char>(s[j] ^ eJ0[j]));
    return out;
}
