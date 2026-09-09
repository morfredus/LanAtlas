#include "crypto/RsaPkcs1.h"

#include <QRandomGenerator>
#include <cstring>
#include <vector>

using Limb = uint32_t;
using Big = std::vector<Limb>;

static void trim(Big& a)
{
    while (a.size() > 1 && a.back() == 0)
        a.pop_back();
    if (a.empty())
        a.push_back(0);
}

static int cmp(const Big& a, const Big& b)
{
    if (a.size() != b.size())
        return a.size() > b.size() ? 1 : -1;
    for (int i = int(a.size()) - 1; i >= 0; --i) {
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

static Big fromHex(QByteArray hex)
{
    hex = hex.trimmed().toLower();
    if (hex.startsWith("0x"))
        hex = hex.mid(2);
    if (hex.size() % 2)
        hex.prepend('0');
    Big r(1, 0);
    for (int i = 0; i < hex.size(); i += 2) {
        bool ok = false;
        const Limb byte = Limb(hex.mid(i, 2).toUInt(&ok, 16));
        uint64_t carry = 0;
        for (size_t j = 0; j < r.size(); ++j) {
            const uint64_t v = uint64_t(r[j]) * 256u + carry;
            r[j] = Limb(v);
            carry = v >> 32;
        }
        if (carry)
            r.push_back(Limb(carry));
        uint64_t add = byte;
        for (size_t j = 0; add; ++j) {
            if (j == r.size())
                r.push_back(0);
            const uint64_t v = uint64_t(r[j]) + add;
            r[j] = Limb(v);
            add = v >> 32;
        }
    }
    trim(r);
    return r;
}

static Big mul(const Big& a, const Big& b)
{
    Big r(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size() || carry; ++j) {
            const uint64_t bv = j < b.size() ? b[j] : 0;
            const uint64_t v = uint64_t(r[i + j]) + uint64_t(a[i]) * bv + carry;
            r[i + j] = Limb(v);
            carry = v >> 32;
        }
    }
    trim(r);
    return r;
}

static void subInPlace(Big& a, const Big& b)
{
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int64_t bv = i < b.size() ? int64_t(b[i]) : 0;
        int64_t v = int64_t(a[i]) - bv - borrow;
        if (v < 0) {
            v += int64_t(1) << 32;
            borrow = 1;
        } else {
            borrow = 0;
        }
        a[i] = Limb(v);
    }
    trim(a);
}

static int bitLength(const Big& a)
{
    if (a.empty() || (a.size() == 1 && a[0] == 0))
        return 0;
    int bits = int(a.size() - 1) * 32;
    Limb top = a.back();
    while (top) {
        ++bits;
        top >>= 1;
    }
    return bits;
}

static Big shl(const Big& a, int bits)
{
    if (bits <= 0)
        return a;
    const int limbOff = bits / 32;
    const int bitOff = bits % 32;
    Big r(a.size() + limbOff + 1, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const uint64_t v = (uint64_t(a[i]) << bitOff) | carry;
        r[i + limbOff] = Limb(v);
        carry = v >> 32;
    }
    if (carry)
        r[a.size() + limbOff] = Limb(carry);
    trim(r);
    return r;
}

static Big mod(Big a, const Big& m)
{
    trim(a);
    if (cmp(a, m) < 0)
        return a;
    for (int shift = bitLength(a) - bitLength(m); shift >= 0; --shift) {
        const Big shifted = shl(m, shift);
        if (cmp(a, shifted) >= 0)
            subInPlace(a, shifted);
    }
    trim(a);
    return a;
}

static Big modMul(const Big& a, const Big& b, const Big& m)
{
    return mod(mul(a, b), m);
}

static Big shr1(Big a)
{
    uint32_t carry = 0;
    for (int i = int(a.size()) - 1; i >= 0; --i) {
        const uint32_t cur = a[i];
        a[i] = (cur >> 1) | (carry << 31);
        carry = cur & 1u;
    }
    trim(a);
    return a;
}

static Big modPow(Big base, Big exp, const Big& m)
{
    Big result(1, 1);
    base = mod(base, m);
    while (!(exp.size() == 1 && exp[0] == 0)) {
        if (exp[0] & 1u)
            result = modMul(result, base, m);
        base = modMul(base, base, m);
        exp = shr1(std::move(exp));
    }
    return result;
}

static int byteLength(const Big& n)
{
    const int bits = bitLength(n);
    return (bits + 7) / 8;
}

static QByteArray toBe(const Big& n, int k)
{
    QByteArray out(k, 0);
    for (int i = 0; i < k; ++i) {
        const int limb = i / 4;
        const int sh = (i % 4) * 8;
        const unsigned char b = limb < int(n.size()) ? unsigned((n[size_t(limb)] >> sh) & 0xFF) : 0;
        out[k - 1 - i] = char(b);
    }
    return out;
}

QByteArray RsaPkcs1::encrypt(const QByteArray& modulusHex,
                             const QByteArray& exponentHex,
                             const QByteArray& plaintext,
                             QString* error)
{
    const Big n = fromHex(modulusHex);
    const Big e = fromHex(exponentHex);
    const int k = byteLength(n);
    if (k < 12 || plaintext.size() > k - 11) {
        if (error)
            *error = QStringLiteral("cle RSA trop petite pour le mot de passe");
        return {};
    }

    QByteArray em(k, '\0');
    em[0] = 0x00;
    em[1] = 0x02;
    const int psLen = k - plaintext.size() - 3;
    for (int i = 0; i < psLen; ++i) {
        unsigned char b = 0;
        while (b == 0)
            b = static_cast<unsigned char>(QRandomGenerator::global()->bounded(1, 256));
        em[2 + i] = char(b);
    }
    em[2 + psLen] = 0x00;
    std::memcpy(em.data() + 3 + psLen, plaintext.constData(), size_t(plaintext.size()));

    const Big m = fromHex(em.toHex());
    const Big c = modPow(m, e, n);
    return toBe(c, k).toHex();
}
