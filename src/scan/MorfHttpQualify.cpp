#include "scan/MorfHttpQualify.h"
#include "core/NetHints.h"
#include "scan/HttpUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtGlobal>
#include <QUrl>

namespace {

bool looksLikeHtml(const QByteArray& body)
{
    const QByteArray t = body.trimmed().left(64).toLower();
    return t.startsWith('<') || t.contains("<html") || t.contains("<!doctype");
}

// Contrat GET /status (StatusServer morfBeacon) : pas un simple {status:ok}.
bool looksLikeMorfStatus(const QJsonObject& o)
{
    const QString app = o.value(QStringLiteral("app")).toString().trimmed();
    const QString version = o.value(QStringLiteral("version")).toString();
    const QString state = o.value(QStringLiteral("state")).toString().trimmed();
    if (app.isEmpty() || version.isEmpty() || state.isEmpty())
        return false;
    if (!o.contains(QStringLiteral("host")) && !o.contains(QStringLiteral("role")))
        return false;
    const bool hasUptime = o.contains(QStringLiteral("uptime_s"));
    const bool hasMetrics = o.contains(QStringLiteral("metrics")) && o.value(QStringLiteral("metrics")).isObject();
    const bool hasApi = o.contains(QStringLiteral("api")) && o.value(QStringLiteral("api")).isObject();
    if (!hasUptime && !hasMetrics && !hasApi)
        return false;
    return true;
}

// Contrat GET /healthz : {"status":"ok"}. Jamais une preuve a lui seul.
bool looksLikeMorfHealthz(const QJsonObject& o)
{
    if (o.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
        return false;
    if (o.contains(QStringLiteral("app")))
        return false;
    return o.size() <= 3;
}

QJsonObject parseJsonObject(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};
    return doc.object();
}

} // namespace

void qualifyMorfHttp(QVector<Device>& devices, int timeoutMs,
                     const std::function<void(int done, int total)>& onProgress)
{
    int total = 0;
    for (const Device& d : devices) {
        if (NetHints::isTechnicalIp(d.ip))
            continue;
        for (int p : d.openPorts) {
            if (NetHints::isHttpQualifyPort(p))
                ++total;
        }
    }
    if (total == 0)
        return;

    HttpClient http;
    int done = 0;
    const int to = qBound(250, timeoutMs, 1500);

    for (Device& d : devices) {
        if (NetHints::isTechnicalIp(d.ip))
            continue;
        QJsonArray found = d.extra.value(QStringLiteral("morf_http")).toArray();
        for (int port : d.openPorts) {
            if (!NetHints::isHttpQualifyPort(port))
                continue;
            const QUrl statusUrl(QStringLiteral("http://%1:%2/status").arg(d.ip).arg(port));
            const HttpResult st = http.get(statusUrl, to);
            ++done;
            if (onProgress)
                onProgress(done, total);

            if (!st.ok() || looksLikeHtml(st.body))
                continue;
            const QJsonObject jo = parseJsonObject(st.body);
            if (!looksLikeMorfStatus(jo))
                continue;

            bool healthzOk = false;
            const QUrl hzUrl(QStringLiteral("http://%1:%2/healthz").arg(d.ip).arg(port));
            const HttpResult hz = http.get(hzUrl, to);
            if (hz.ok() && !looksLikeHtml(hz.body))
                healthzOk = looksLikeMorfHealthz(parseJsonObject(hz.body));

            const QString app = jo.value(QStringLiteral("app")).toString();
            if (!d.morfApps.contains(app))
                d.morfApps << app;
            const QJsonArray caps = jo.value(QStringLiteral("capabilities")).toArray();
            for (const QJsonValue& c : caps) {
                const QString cap = c.toString();
                if (!cap.isEmpty() && !d.capabilities.contains(cap))
                    d.capabilities << cap;
            }
            if (!d.sources.contains(QLatin1String("morfhttp")))
                d.sources << QStringLiteral("morfhttp");

            QJsonObject rec;
            rec[QStringLiteral("port")] = port;
            rec[QStringLiteral("app")] = app;
            rec[QStringLiteral("version")] = jo.value(QStringLiteral("version")).toString();
            rec[QStringLiteral("state")] = jo.value(QStringLiteral("state")).toString();
            rec[QStringLiteral("healthz")] = healthzOk;
            QJsonArray capOut;
            for (const QJsonValue& c : caps)
                capOut.append(c);
            rec[QStringLiteral("capabilities")] = capOut;
            rec[QStringLiteral("proof")] = healthzOk
                ? QStringLiteral("GET /status + GET /healthz (contrat JSON)")
                : QStringLiteral("GET /status (contrat JSON)");
            found.append(rec);
        }
        if (!found.isEmpty())
            d.extra.insert(QStringLiteral("morf_http"), found);
    }
}

void fingerprintHttp(QVector<Device>& devices, int timeoutMs)
{
    HttpClient http;
    http.setTlsRelaxed(true);
    const int to = qBound(300, timeoutMs, 1500);
    static const QRegularExpression titleRe(
        QStringLiteral("<title[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression genericTitle(
        QStringLiteral("^(index|home|login|log in|welcome|bienvenue|404|error|erreur|untitled|"
                       "document|page|dashboard)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    for (Device& d : devices) {
        if (d.ip.isEmpty() || NetHints::isTechnicalIp(d.ip))
            continue;
        if (!d.model.isEmpty() && !d.notes.isEmpty())
            continue; // deja identifie
        int webPort = -1;
        bool tls = false;
        for (int p : d.openPorts) {
            if (p == 80 || p == 8080 || p == 8000) {
                webPort = p;
                tls = false;
                break;
            }
            if ((p == 443 || p == 8443) && webPort < 0) {
                webPort = p;
                tls = true;
            }
        }
        if (webPort < 0)
            continue;

        const QString url = QStringLiteral("%1://%2:%3/")
                                .arg(tls ? QStringLiteral("https") : QStringLiteral("http"))
                                .arg(d.ip)
                                .arg(webPort);
        const HttpResult r = http.get(QUrl(url), to);

        QString server;
        for (const auto& h : r.headers) {
            if (QString::fromLatin1(h.first).compare(QLatin1String("Server"), Qt::CaseInsensitive) == 0)
                server = QString::fromLatin1(h.second).trimmed();
        }
        QString title;
        const auto m = titleRe.match(QString::fromUtf8(r.body.left(8192)));
        if (m.hasMatch())
            title = m.captured(1).simplified();

        if (!server.isEmpty()) {
            d.extra.insert(QStringLiteral("http_server"), server);
            if (d.notes.isEmpty())
                d.notes = server;
        }
        if (!title.isEmpty()) {
            d.extra.insert(QStringLiteral("http_title"), title);
            if (d.model.isEmpty() && title.size() <= 60 && !genericTitle.match(title).hasMatch())
                d.model = title;
        }
        if ((!server.isEmpty() || !title.isEmpty()) && !d.sources.contains(QLatin1String("http")))
            d.sources << QStringLiteral("http");
    }
}
