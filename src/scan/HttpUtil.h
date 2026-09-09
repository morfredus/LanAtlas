#pragma once

#include <QByteArray>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

struct HttpResult {
    int status = 0;
    QByteArray body;
    QList<QNetworkReply::RawHeaderPair> headers;
    QString error;
    QUrl url;
    bool ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
    HttpClient()
    {
        nam_.setCookieJar(new QNetworkCookieJar(&nam_));
        // Les redirections HTTP→HTTPS recreent un QNetworkReply : ignorer le
        // certificat sur le manager, pas seulement sur la premiere requete.
        QObject::connect(&nam_, &QNetworkAccessManager::sslErrors,
                         [this](QNetworkReply* reply, const QList<QSslError>&) {
                             if (tlsRelaxed_ && reply)
                                 reply->ignoreSslErrors();
                         });
    }

    void setTlsRelaxed(bool on) { tlsRelaxed_ = on; }
    // false : suivre le 307 HTTP→HTTPS (firmware Deco actuel).

    void setHeader(const QByteArray& name, const QByteArray& value)
    {
        extra_.insert(name, value);
    }

    void clearHeader(const QByteArray& name) { extra_.remove(name); }

    HttpResult get(const QUrl& url, int timeoutMs = 8000)
    {
        lastIsPost_ = false;
        lastBody_.clear();
        lastType_.clear();
        QNetworkRequest req(url);
        apply(req);
        return exec(nam_.get(req), timeoutMs, 0);
    }

    HttpResult post(const QUrl& url, const QByteArray& body, const QByteArray& contentType,
                    int timeoutMs = 8000)
    {
        lastIsPost_ = true;
        lastBody_ = body;
        lastType_ = contentType;
        QNetworkRequest req(url);
        apply(req);
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        return exec(nam_.post(req, body), timeoutMs, 0);
    }

    QJsonObject json(const HttpResult& r) const
    {
        return QJsonDocument::fromJson(r.body).object();
    }

private:
    void apply(QNetworkRequest& req) const
    {
        req.setRawHeader("User-Agent", "LanAtlas/0.1 (morfsystem)");
        for (auto it = extra_.begin(); it != extra_.end(); ++it)
            req.setRawHeader(it.key(), it.value());
        if (tlsRelaxed_) {
            QSslConfiguration ssl = req.sslConfiguration();
            ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
            req.setSslConfiguration(ssl);
            // On suit a la main : sinon Qt part vers tplinkdeco.net et le
            // certificat (signature interne Deco) casse la poignee TLS.
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        }
    }

    static bool isRedirect(int status)
    {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    QUrl rewriteRedirect(const QUrl& from, const QByteArray& locationRaw) const
    {
        QUrl loc = QUrl::fromEncoded(locationRaw);
        if (loc.isRelative() || loc.host().isEmpty())
            loc = from.resolved(loc);
        // Garder l'IP du nœud : le hostname public n'a pas de DNS utile
        // derriere une Livebox, et le cert n'est pas non plus pour ce nom.
        const QString h = loc.host().toLower();
        if (h.contains(QLatin1String("tplinkdeco")) || h != from.host().toLower())
            loc.setHost(from.host());
        return loc;
    }

    HttpResult exec(QNetworkReply* reply, int timeoutMs, int hop)
    {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        if (tlsRelaxed_) {
            QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                             [reply](const QList<QSslError>&) { reply->ignoreSslErrors(); });
        }
        timer.start(timeoutMs);
        loop.exec();
        HttpResult r;
        r.url = reply->url();
        if (!reply->isFinished()) {
            reply->abort();
            r.error = QStringLiteral("timeout");
            reply->deleteLater();
            return r;
        }
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.headers = reply->rawHeaderPairs();
        r.error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const QByteArray loc = reply->rawHeader("Location");
        const QUrl from = reply->url();
        reply->deleteLater();

        if (tlsRelaxed_ && hop < 5 && isRedirect(r.status) && !loc.isEmpty()) {
            const QUrl next = rewriteRedirect(from, loc);
            QNetworkRequest req(next);
            apply(req);
            const bool keepPost = lastIsPost_ && (r.status == 307 || r.status == 308);
            if (keepPost) {
                req.setHeader(QNetworkRequest::ContentTypeHeader, lastType_);
                return exec(nam_.post(req, lastBody_), timeoutMs, hop + 1);
            }
            return exec(nam_.get(req), timeoutMs, hop + 1);
        }
        return r;
    }

    QNetworkAccessManager nam_;
    QHash<QByteArray, QByteArray> extra_;
    bool tlsRelaxed_ = false;
    bool lastIsPost_ = false;
    QByteArray lastBody_;
    QByteArray lastType_;
};
