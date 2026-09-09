#include "adapters/DecoClient.h"
#include "crypto/AesCbc.h"
#include "crypto/RsaPkcs1.h"
#include "scan/HttpUtil.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

static QUrl luciUrl(const QString& scheme, const QString& host, const QString& stok,
                    const QString& tail)
{
    // Le point-virgule fait partie du chemin luci ; fromEncoded evite que Qt
    // le prenne pour autre chose, et on ne recolle plus le stok deux fois.
    const QString raw = QStringLiteral("%1://%2/cgi-bin/luci/;stok=%3%4")
                            .arg(scheme, host, stok, tail);
    return QUrl::fromEncoded(raw.toUtf8());
}

static QString jsonErr(const QJsonObject& o, const HttpResult& http)
{
    const int code = o.value(QStringLiteral("error_code")).toInt();
    const QString msg = o.value(QStringLiteral("msg")).toString();
    QString s;
    if (code)
        s += QStringLiteral("error_code=%1 ").arg(code);
    if (!msg.isEmpty())
        s += msg + QLatin1Char(' ');
    if (http.status)
        s += QStringLiteral("HTTP %1 ").arg(http.status);
    if (!http.error.isEmpty())
        s += http.error;
    const QByteArray clip = http.body.left(160).simplified();
    if (s.isEmpty() && !clip.isEmpty())
        s = QString::fromUtf8(clip);
    return s.trimmed();
}

static QByteArray jsonAtom(const QJsonValue& v)
{
    if (v.isString())
        return v.toString().toUtf8();
    if (v.isDouble())
        return QByteArray::number(v.toInt(), 16);
    return {};
}

// Luci Deco : result.password est un tableau [module, exposant] en hex,
// pas une chaine. toString() sur un array renvoyait vide → "pas de cles RSA"
// malgre HTTP 200.
static bool rsaFromKeysJson(const QJsonObject& root, QByteArray* n, QByteArray* e)
{
    const QJsonObject result = root.value(QStringLiteral("result")).toObject();
    const QJsonValue pwd = result.contains(QStringLiteral("password"))
                               ? result.value(QStringLiteral("password"))
                               : root.value(QStringLiteral("password"));
    const QJsonValue user = result.value(QStringLiteral("username"));
    if (pwd.isArray()) {
        const QJsonArray a = pwd.toArray();
        if (a.size() >= 2) {
            *n = jsonAtom(a.at(0));
            *e = jsonAtom(a.at(1));
        }
    } else if (pwd.isObject()) {
        const QJsonObject o = pwd.toObject();
        *n = jsonAtom(o.value(QStringLiteral("n")));
        if (n->isEmpty())
            *n = jsonAtom(o.value(QStringLiteral("password")));
        *e = jsonAtom(o.value(QStringLiteral("e")));
        if (e->isEmpty())
            *e = jsonAtom(o.value(QStringLiteral("username")));
    } else {
        *n = jsonAtom(pwd);
        *e = jsonAtom(user);
    }
    if (e->isEmpty())
        *e = QByteArrayLiteral("010001");
    return n->size() >= 8 && !e->isEmpty();
}

static bool rsaFromArray(const QJsonValue& v, QByteArray* n, QByteArray* e)
{
    if (!v.isArray() || v.toArray().size() < 2)
        return false;
    *n = jsonAtom(v.toArray().at(0));
    *e = jsonAtom(v.toArray().at(1));
    if (e->isEmpty())
        *e = QByteArrayLiteral("010001");
    return n->size() >= 8;
}

static QString bodyClip(const HttpResult& http)
{
    return QString::fromUtf8(http.body.left(220).simplified());
}

static QJsonObject decoPostJson(HttpClient& http, const QUrl& url, const QJsonObject& payload)
{
    auto r = http.post(url, QJsonDocument(payload).toJson(QJsonDocument::Compact),
                       "application/json", 10000);
    return QJsonDocument::fromJson(r.body).object();
}

static HttpResult decoReadKeys(HttpClient& http, const QUrl& url)
{
    // La web UI poste du formulaire, pas du JSON. On essaie les deux.
    HttpResult r = http.post(url, QByteArrayLiteral("operation=read"),
                             "application/x-www-form-urlencoded", 10000);
    QJsonObject o = QJsonDocument::fromJson(r.body).object();
    QByteArray n, e;
    if (rsaFromKeysJson(o, &n, &e)
        || rsaFromArray(o.value(QStringLiteral("result")).toObject().value(QStringLiteral("key")),
                        &n, &e))
        return r;
    r = http.post(url,
                  QJsonDocument(QJsonObject{{QStringLiteral("operation"),
                                            QStringLiteral("read")}})
                      .toJson(QJsonDocument::Compact),
                  "application/json", 10000);
    return r;
}

struct DecoSess {
    HttpClient* http = nullptr;
    QString scheme;
    QString host;
    QString stok;
    QByteArray aesKey;
    QByteArray aesIv;
    QByteArray sigN;
    QByteArray sigE;
    qint64 seq = 0;
    QByteArray credHash;
    bool enc = false;
};

static QByteArray rsaChunks(const QByteArray& n, const QByteArray& e, const QByteArray& msg,
                            QString* err)
{
    int hexLen = n.size();
    if (hexLen % 2)
        ++hexLen;
    const int k = hexLen / 2;
    const int step = k - 11;
    if (step < 8) {
        if (err)
            *err = QStringLiteral("cle RSA trop petite");
        return {};
    }
    QByteArray out;
    for (int i = 0; i < msg.size(); i += step) {
        QString one;
        const QByteArray part = RsaPkcs1::encrypt(n, e, msg.mid(i, step), &one);
        if (part.isEmpty()) {
            if (err)
                *err = one;
            return {};
        }
        out += part;
    }
    return out;
}

static QByteArray randomDigits16()
{
    // La web UI / ha-tplink-deco : 16 chiffres, pas de zero en tete, pas d'hex.
    const quint64 min = 1000000000000000ULL;
    const quint64 span = 9000000000000000ULL;
    const quint64 v = min + (QRandomGenerator::global()->generate64() % span);
    return QByteArray::number(v);
}

static QJsonObject decoUnwrap(const DecoSess& s, const QJsonObject& env)
{
    const QByteArray b64 = env.value(QStringLiteral("data")).toString().toUtf8();
    if (b64.isEmpty() || !s.enc)
        return env;
    const QByteArray bin = QByteArray::fromBase64(b64);
    const QByteArray plain = AesCbc::decrypt(s.aesKey, s.aesIv, bin);
    const QJsonObject inner = QJsonDocument::fromJson(plain).object();
    return inner.isEmpty() ? env : inner;
}

static HttpResult decoEnvelope(DecoSess& s, const QUrl& url, const QJsonObject& payload,
                               bool isLogin)
{
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray dataB64 = AesCbc::encrypt(s.aesKey, s.aesIv, json).toBase64();
    QString rsaErr;
    // La web UI Deco (getSignature) n'inclut la cle AES (k=/i=) dans la signature
    // qu'au moment du login. Apres login, les requetes (device_list, client_list...)
    // ne signent que h=&s= : renvoyer k=&i= partout casse le mapping post-login.
    const QByteArray seqField = QByteArrayLiteral("&s=")
                                + QByteArray::number(s.seq + dataB64.size());
    QByteArray toSign;
    if (isLogin)
        toSign = QByteArrayLiteral("k=") + s.aesKey + QByteArrayLiteral("&i=") + s.aesIv
                 + QByteArrayLiteral("&h=") + s.credHash + seqField;
    else
        toSign = QByteArrayLiteral("h=") + s.credHash + seqField;
    const QByteArray sign = rsaChunks(s.sigN, s.sigE, toSign, &rsaErr);
    QByteArray body = QByteArrayLiteral("sign=") + sign + QByteArrayLiteral("&data=")
                      + QUrl::toPercentEncoding(dataB64);
    return s.http->post(url, body, "application/json", 12000);
}

static QJsonObject decoForm(DecoSess& s, const QString& tail, const QString& form,
                            const QJsonObject& payload)
{
    QUrl url = luciUrl(s.scheme, s.host, s.stok, tail);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("form"), form);
    url.setQuery(q);
    if (!s.enc)
        return decoPostJson(*s.http, url, payload);
    // Requete post-login : signature h=&s= (pas de k=/i=).
    const HttpResult r = decoEnvelope(s, url, payload, false);
    return decoUnwrap(s, QJsonDocument::fromJson(r.body).object());
}

static QJsonArray firstArray(const QJsonObject& root)
{
    const QJsonValue result = root.value(QStringLiteral("result"));
    if (result.isArray())
        return result.toArray();
    if (!result.isObject())
        return {};
    const QJsonObject o = result.toObject();
    const QStringList keys = {QStringLiteral("device_list"), QStringLiteral("client_list"),
                              QStringLiteral("list"), QStringLiteral("deviceList"),
                              QStringLiteral("clientList")};
    for (const QString& k : keys) {
        if (o.value(k).isArray())
            return o.value(k).toArray();
    }
    return {};
}

static bool decoLogin(DecoSess& s, const QString& password, QString* err)
{
    HttpClient& http = *s.http;
    http.get(QUrl(s.scheme + QStringLiteral("://") + s.host
                  + QStringLiteral("/webpages/login.html")),
             5000);
    QUrl keys = luciUrl(s.scheme, s.host, QString(), QStringLiteral("/login"));
    QUrlQuery kq;
    kq.addQueryItem(QStringLiteral("form"), QStringLiteral("keys"));
    keys.setQuery(kq);
    const HttpResult kr = decoReadKeys(http, keys);
    const QJsonObject ko = QJsonDocument::fromJson(kr.body).object();
    QByteArray pwdN, pwdE;
    if (!rsaFromKeysJson(ko, &pwdN, &pwdE)) {
        if (err) {
            QString why = jsonErr(ko, kr);
            const QString clip = bodyClip(kr);
            if (!clip.isEmpty())
                why += (why.isEmpty() ? QString() : QStringLiteral(" ")) + clip;
            if (why.isEmpty())
                why = QStringLiteral("pas un Deco, ou API locale fermee");
            *err = QStringLiteral("%1 (%2) : pas de cles RSA luci (%3)")
                       .arg(s.host, s.scheme, why);
        }
        return false;
    }

    QUrl authUrl = luciUrl(s.scheme, s.host, QString(), QStringLiteral("/login"));
    QUrlQuery aq;
    aq.addQueryItem(QStringLiteral("form"), QStringLiteral("auth"));
    authUrl.setQuery(aq);
    const HttpResult ar = decoReadKeys(http, authUrl);
    const QJsonObject ao = QJsonDocument::fromJson(ar.body).object();
    const QJsonObject ares = ao.value(QStringLiteral("result")).toObject();
    rsaFromArray(ares.value(QStringLiteral("key")), &s.sigN, &s.sigE);
    s.seq = ares.value(QStringLiteral("seq")).toVariant().toLongLong();

    QUrl login = luciUrl(s.scheme, s.host, QString(), QStringLiteral("/login"));
    QUrlQuery lq;
    lq.addQueryItem(QStringLiteral("form"), QStringLiteral("login"));
    login.setQuery(lq);

    if (!s.sigN.isEmpty()) {
        // Firmware actuel : enveloppe AES+RSA (un POST JSON nu repond 403).
        s.aesKey = randomDigits16();
        s.aesIv = randomDigits16();
        s.credHash = QCryptographicHash::hash(QByteArrayLiteral("admin") + password.toUtf8(),
                                              QCryptographicHash::Md5)
                         .toHex();
        QString rsaErr;
        const QByteArray pwdEnc = RsaPkcs1::encrypt(pwdN, pwdE, password.toUtf8(), &rsaErr);
        if (pwdEnc.isEmpty()) {
            if (err)
                *err = rsaErr;
            return false;
        }
        s.enc = true;
        const QJsonObject payload{{QStringLiteral("params"),
                                   QJsonObject{{QStringLiteral("password"),
                                                QString::fromLatin1(pwdEnc)}}},
                                  {QStringLiteral("operation"), QStringLiteral("login")}};
        const HttpResult lr = decoEnvelope(s, login, payload, true);
        const QJsonObject lo = decoUnwrap(s, QJsonDocument::fromJson(lr.body).object());
        s.stok = lo.value(QStringLiteral("result")).toObject().value(QStringLiteral("stok")).toString();
        if (s.stok.isEmpty())
            s.stok = lo.value(QStringLiteral("stok")).toString();
        if (!s.stok.isEmpty())
            return true;
        s.enc = false;
        const int attempts = lo.value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("attemptsAllowed")).toInt(-1);
        if (err) {
            QString why = jsonErr(lo, lr);
            if (attempts >= 0)
                why += QStringLiteral(" tentatives restantes=%1").arg(attempts);
            if (lo.value(QStringLiteral("error_code")).toInt() == -5002)
                why += QStringLiteral(" (mot de passe refuse par luci, hash admin+mdp)");
            *err = QStringLiteral("%1 (%2) : login chiffre refuse (%3)")
                       .arg(s.host, s.scheme, why);
        }
        return false;
    }

    QString rsaErr;
    const QByteArray enc = RsaPkcs1::encrypt(pwdN, pwdE, password.toUtf8(), &rsaErr);
    if (enc.isEmpty()) {
        if (err)
            *err = rsaErr;
        return false;
    }
    const QJsonObject body{{QStringLiteral("operation"), QStringLiteral("login")},
                           {QStringLiteral("password"), QString::fromLatin1(enc)}};
    HttpResult lr = http.post(login, QJsonDocument(body).toJson(QJsonDocument::Compact),
                              "application/json", 10000);
    QJsonObject lo = QJsonDocument::fromJson(lr.body).object();
    s.stok = lo.value(QStringLiteral("result")).toObject().value(QStringLiteral("stok")).toString();
    if (s.stok.isEmpty()) {
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("operation"), QStringLiteral("login"));
        form.addQueryItem(QStringLiteral("password"), QString::fromLatin1(enc));
        lr = http.post(login, form.toString(QUrl::FullyEncoded).toUtf8(),
                       "application/x-www-form-urlencoded", 10000);
        lo = QJsonDocument::fromJson(lr.body).object();
        s.stok = lo.value(QStringLiteral("result")).toObject().value(QStringLiteral("stok")).toString();
    }
    if (s.stok.isEmpty()) {
        if (err)
            *err = QStringLiteral("%1 (%2) : login refuse (%3)")
                       .arg(s.host, s.scheme,
                            jsonErr(lo, lr).isEmpty()
                                ? (bodyClip(lr).isEmpty()
                                       ? QStringLiteral("mot de passe appli Deco / compte TP-Link")
                                       : bodyClip(lr))
                                : jsonErr(lo, lr));
        return false;
    }
    return true;
}

static Device decoNode(const QJsonObject& o)
{
    Device d;
    d.extra = o;
    d.mac = o.value(QStringLiteral("mac")).toString();
    d.deviceId = o.value(QStringLiteral("device_id")).toString();
    d.hostname = o.value(QStringLiteral("nickname")).toString();
    if (d.hostname.isEmpty())
        d.hostname = o.value(QStringLiteral("device_alias")).toString();
    if (d.hostname.isEmpty())
        d.hostname = o.value(QStringLiteral("set_nickname")).toString();
    d.ip = o.value(QStringLiteral("device_ip")).toString();
    if (d.ip.isEmpty())
        d.ip = o.value(QStringLiteral("inet_ip")).toString();
    d.model = o.value(QStringLiteral("device_model")).toString(QStringLiteral("Deco X50"));
    d.vendor = QStringLiteral("TP-Link");
    d.category = QStringLiteral("mesh_node");
    d.infrastructure = true;
    d.online = o.value(QStringLiteral("inet_status")).toString() != QLatin1String("offline");
    d.notes = o.value(QStringLiteral("role")).toString();
    d.connection = o.value(QStringLiteral("connection_type")).toString();
    d.sources << QStringLiteral("deco");
    return d;
}

static QString firstString(const QJsonObject& o, const QStringList& keys)
{
    for (const QString& k : keys) {
        const QJsonValue v = o.value(k);
        if (v.isString() && !v.toString().isEmpty())
            return v.toString();
        if (v.isDouble())
            return QString::number(v.toInt());
    }
    return {};
}

static QJsonObject firstObject(const QJsonObject& o, const QStringList& keys)
{
    for (const QString& k : keys) {
        if (o.value(k).isObject())
            return o.value(k).toObject();
    }
    return {};
}

// Le mesh Deco encode les noms clients en base64 (UTF-8). Un nom deja en clair
// (firmware plus ancien) n'est pas du base64 valide : on le laisse tel quel.
static QString decodeDecoName(const QString& raw)
{
    if (raw.isEmpty())
        return raw;
    const auto res = QByteArray::fromBase64Encoding(
        raw.toUtf8(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (res.decodingStatus != QByteArray::Base64DecodingStatus::Ok || res.decoded.isEmpty())
        return raw;
    const QString s = QString::fromUtf8(res.decoded);
    // N'accepter le decodage que s'il round-trip en UTF-8 (evite de "decoder"
    // par erreur un nom en clair qui ressemble a du base64).
    if (s.isEmpty() || s.toUtf8() != res.decoded)
        return raw;
    return s;
}

// connection_type Deco : "band5" / "band2_4" (Wi-Fi) ou filaire.
static QString humanizeDecoBand(const QString& connType, const QString& wireType)
{
    const QString c = connType.toLower();
    if (c.contains(QLatin1String("band5")) || c == QLatin1String("5g"))
        return QStringLiteral("Wi-Fi 5 GHz");
    if (c.contains(QLatin1String("band2")) || c.contains(QLatin1String("2_4"))
        || c.contains(QLatin1String("2.4")))
        return QStringLiteral("Wi-Fi 2.4 GHz");
    if (c.contains(QLatin1String("band6")))
        return QStringLiteral("Wi-Fi 6 GHz");
    const QString w = wireType.toLower();
    if (w == QLatin1String("wired") || w.contains(QLatin1String("eth"))
        || w.contains(QLatin1String("lan")) || w.contains(QLatin1String("plc")))
        return QStringLiteral("Filaire");
    return {};
}

// client_type Deco (majuscules) -> libelle lisible, utile surtout pour les
// mobiles a MAC aleatoire (fabricant inconnu) : au moins on sait "Telephone".
static QString humanizeDecoType(const QString& raw)
{
    const QString t = raw.toUpper();
    if (t.contains(QLatin1String("PHONE")) || t.contains(QLatin1String("MOBILE")))
        return QStringLiteral("Telephone");
    if (t.contains(QLatin1String("TABLET")) || t == QLatin1String("PAD"))
        return QStringLiteral("Tablette");
    if (t.contains(QLatin1String("LAPTOP")) || t.contains(QLatin1String("PC"))
        || t.contains(QLatin1String("COMPUTER")) || t.contains(QLatin1String("DESKTOP")))
        return QStringLiteral("Ordinateur");
    if (t.contains(QLatin1String("TV")))
        return QStringLiteral("TV");
    if (t.contains(QLatin1String("CAMERA")) || t == QLatin1String("IPC"))
        return QStringLiteral("Camera");
    if (t.contains(QLatin1String("GAME")) || t.contains(QLatin1String("CONSOLE")))
        return QStringLiteral("Console de jeu");
    if (t.contains(QLatin1String("SPEAKER")) || t.contains(QLatin1String("SOUND"))
        || t.contains(QLatin1String("AUDIO")))
        return QStringLiteral("Enceinte");
    if (t.contains(QLatin1String("PRINTER")))
        return QStringLiteral("Imprimante");
    if (t.contains(QLatin1String("WATCH")) || t.contains(QLatin1String("WEARABLE")))
        return QStringLiteral("Montre connectee");
    if (t.contains(QLatin1String("CLEANER")) || t.contains(QLatin1String("VACUUM")))
        return QStringLiteral("Aspirateur");
    if (t.contains(QLatin1String("NAS")) || t.contains(QLatin1String("STORAGE")))
        return QStringLiteral("NAS");
    if (t.contains(QLatin1String("ROUTER")) || t.contains(QLatin1String("REPEATER"))
        || t.contains(QLatin1String("EXTENDER")) || t.contains(QLatin1String("AP")))
        return QStringLiteral("Equipement reseau");
    if (t.contains(QLatin1String("IOT")) || t.contains(QLatin1String("SMART"))
        || t.contains(QLatin1String("PLUG")) || t.contains(QLatin1String("BULB"))
        || t.contains(QLatin1String("SENSOR")))
        return QStringLiteral("Objet connecte");
    if (t.isEmpty() || t == QLatin1String("UNKNOWN"))
        return {};
    // Inconnu : "GAME_CONSOLE" -> "Game console".
    QString pretty = raw.toLower().replace(QLatin1Char('_'), QLatin1Char(' ')).trimmed();
    if (!pretty.isEmpty())
        pretty[0] = pretty[0].toUpper();
    return pretty;
}

static Device decoClient(const QJsonObject& o)
{
    Device d;
    d.extra = o;
    d.mac = o.value(QStringLiteral("mac")).toString();
    d.hostname = decodeDecoName(firstString(o, {QStringLiteral("name"), QStringLiteral("client_name"),
                                 QStringLiteral("hostname"), QStringLiteral("device_name"),
                                 QStringLiteral("nickname")}));
    d.ip = firstString(o, {QStringLiteral("ip"), QStringLiteral("ip_addr"), QStringLiteral("ipv4")});
    const QString wireType = o.value(QStringLiteral("wire_type")).toString();
    const QString connType = firstString(o, {QStringLiteral("connection_type"),
                                             QStringLiteral("conn_type")});
    d.band = humanizeDecoBand(connType, wireType);
    d.connection = d.band.isEmpty() ? (connType.isEmpty() ? wireType : connType) : d.band;
    if (d.band.isEmpty())
        d.band = d.connection;
    d.ssid = firstString(o, {QStringLiteral("ssid"), QStringLiteral("SSID")});
    // Ne pas copier owner_id dans deviceId : ce serait l'id du repetiteur, pas du client.
    d.parentId = firstString(o, {QStringLiteral("owner_id"), QStringLiteral("parent_id"),
                                 QStringLiteral("ap_mac"), QStringLiteral("parent_mac"),
                                 QStringLiteral("connection_to"), QStringLiteral("belong")});
    d.parentName = firstString(o, {QStringLiteral("owner_name"), QStringLiteral("parent_name"),
                                   QStringLiteral("ap_name"), QStringLiteral("access_host")});
    const QJsonObject owner = firstObject(o, {QStringLiteral("owner"), QStringLiteral("parent"),
                                              QStringLiteral("access_point"), QStringLiteral("ap")});
    if (!owner.isEmpty()) {
        if (d.parentId.isEmpty())
            d.parentId = firstString(owner, {QStringLiteral("device_id"), QStringLiteral("id"),
                                             QStringLiteral("mac")});
        if (d.parentName.isEmpty())
            d.parentName = firstString(owner, {QStringLiteral("nickname"), QStringLiteral("name"),
                                               QStringLiteral("device_alias")});
    }
    if (o.contains(QStringLiteral("signal_strength")))
        d.rssi = o.value(QStringLiteral("signal_strength")).toInt();
    else if (o.contains(QStringLiteral("rssi")))
        d.rssi = o.value(QStringLiteral("rssi")).toInt();
    d.online = jsonFlag(o.value(QStringLiteral("online")), false);
    const QString inet = o.value(QStringLiteral("inet_status")).toString().toLower();
    if (inet == QLatin1String("offline") || inet == QLatin1String("off"))
        d.online = false;
    else if (inet == QLatin1String("online") || inet == QLatin1String("on"))
        d.online = true;
    d.vendor = o.value(QStringLiteral("vendor")).toString();
    d.model = humanizeDecoType(o.value(QStringLiteral("client_type")).toString());
    d.category = QStringLiteral("client");
    d.sources << QStringLiteral("deco");
    return d;
}

QVector<Device> queryDeco(const AppSettings& settings, Inventory& inventory,
                          const QStringList& candidateHosts)
{
    QVector<Device> out;
    if (settings.decoPassword.isEmpty()) {
        inventory.log(QStringLiteral(
            "Deco : pas de mot de passe. Dans Parametres, coller celui du compte "
            "TP-Link / appli Deco (c'est le meme, il n'y a pas d'admin local separe)."));
        return out;
    }

    QStringList hosts = candidateHosts;
    if (!settings.decoHost.isEmpty() && !hosts.contains(settings.decoHost))
        hosts.prepend(settings.decoHost);
    hosts << QStringLiteral("192.168.68.1") << QStringLiteral("tplinkdeco.net");
    hosts.removeDuplicates();

    HttpClient http;
    http.setTlsRelaxed(true);
    http.setHeader("User-Agent",
                   "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/126.0.0.0");
    http.setHeader("X-Requested-With", "XMLHttpRequest");
    DecoSess sess;
    sess.http = &http;
    QString err;
    const QStringList schemes = {QStringLiteral("https"), QStringLiteral("http")};
    bool ok = false;
    for (const QString& scheme : schemes) {
        for (const QString& host : hosts) {
            if (host.isEmpty())
                continue;
            sess.scheme = scheme;
            sess.host = host;
            sess.stok.clear();
            sess.enc = false;
            http.setHeader("Referer",
                           (scheme + QStringLiteral("://") + host
                            + QStringLiteral("/webpages/index.html"))
                               .toUtf8());
            http.setHeader("Origin", (scheme + QStringLiteral("://") + host).toUtf8());
            QString oneErr;
            if (decoLogin(sess, settings.decoPassword, &oneErr)) {
                err.clear();
                ok = true;
                break;
            }
            err = oneErr;
            inventory.log(QStringLiteral("Deco essai ") + host + QLatin1Char(' ') + scheme
                          + QStringLiteral(" : ") + oneErr);
        }
        if (ok)
            break;
    }
    if (sess.stok.isEmpty()) {
        inventory.log(QStringLiteral("Deco : aucun nœud n'a accepte le login. ")
                      + (err.isEmpty() ? QString() : err));
        return out;
    }
    inventory.log(QStringLiteral("Deco : session ouverte sur ") + sess.scheme
                  + QStringLiteral("://") + sess.host
                  + (sess.enc ? QStringLiteral(" (login chiffre web UI)") : QString()));

    QJsonArray nodes;
    for (const char* form : {"device_list", "get_device_list"}) {
        auto devicesJson = decoForm(sess, QStringLiteral("/admin/device"), QLatin1String(form),
                                    QJsonObject{{QStringLiteral("operation"),
                                                 QStringLiteral("read")}});
        nodes = firstArray(devicesJson);
        if (!nodes.isEmpty())
            break;
    }
    QVector<QJsonObject> nodeObjs;
    for (const auto& n : nodes) {
        const QJsonObject node = n.toObject();
        nodeObjs.append(node);
        out.append(decoNode(node));
    }

    // Clients : interroger PAR nœud. La liste globale (device_mac="default")
    // renvoie un owner_id VIDE -> impossible de savoir sur quel Deco est chaque
    // client. En ciblant le mac de chaque nœud, le mesh renvoie exactement les
    // clients de ce nœud, et le rattachement vient de la requete elle-meme.
    int clientCount = 0;
    QSet<QString> seenClients;
    for (const QJsonObject& node : nodeObjs) {
        const QString nodeMac = node.value(QStringLiteral("mac")).toString();
        if (nodeMac.isEmpty())
            continue;
        QString nodeName = node.value(QStringLiteral("nickname")).toString();
        if (nodeName.isEmpty())
            nodeName = node.value(QStringLiteral("device_alias")).toString();
        QJsonArray clients;
        const QJsonObject payload{
            {QStringLiteral("operation"), QStringLiteral("read")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("device_mac"), nodeMac}}}};
        for (const char* form : {"client_list", "get_client_list"}) {
            auto clientsJson = decoForm(sess, QStringLiteral("/admin/client"), QLatin1String(form),
                                        payload);
            clients = firstArray(clientsJson);
            if (!clients.isEmpty())
                break;
        }
        for (const auto& c : clients) {
            Device d = decoClient(c.toObject());
            const QString key = normalizedMac(d.mac);
            if (!key.isEmpty() && seenClients.contains(key))
                continue;
            if (!key.isEmpty())
                seenClients.insert(key);
            // Rattachement au nœud interroge : le mac est l'identifiant fiable,
            // meme pour le nœud principal qui n'expose pas de device_id.
            d.parentId = nodeMac;
            d.parentName = nodeName;
            out.append(d);
            ++clientCount;
        }
    }

    // Repli : certains firmwares n'acceptent que la liste globale.
    if (clientCount == 0) {
        QJsonArray clients;
        const QJsonObject clientPayload{
            {QStringLiteral("operation"), QStringLiteral("read")},
            {QStringLiteral("params"),
             QJsonObject{{QStringLiteral("device_mac"), QStringLiteral("default")}}}};
        for (const char* form : {"client_list", "get_client_list"}) {
            auto clientsJson = decoForm(sess, QStringLiteral("/admin/client"), QLatin1String(form),
                                        clientPayload);
            clients = firstArray(clientsJson);
            if (!clients.isEmpty())
                break;
        }
        for (const auto& c : clients)
            out.append(decoClient(c.toObject()));
        clientCount = clients.size();
    }

    inventory.decoSummary = QStringLiteral("%1 nœud(s), %2 client(s) declares par le mesh")
                                .arg(nodes.size())
                                .arg(clientCount);
    inventory.log(inventory.decoSummary);
    if (nodes.isEmpty())
        inventory.log(QStringLiteral(
            "Deco : liste des nœuds vide. Le login a marche mais l'API n'a pas renvoye les repetiteurs."));
    return out;
}
