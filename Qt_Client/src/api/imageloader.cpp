#include "imageloader.h"
#include "urlorigin.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

ImageLoader::ImageLoader(int timeoutMs, bool allowInsecureHttp, QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_timeoutMs(timeoutMs > 0 ? timeoutMs : 5000)
    , m_allowInsecureHttp(allowInsecureHttp)
{
}

void ImageLoader::setBearerAuthentication(const QUrl &fixedLoginOrigin,
                                          const QByteArray &token)
{
    m_authenticatedServerOrigin =
        UrlOrigin::normalizedHttpOrigin(fixedLoginOrigin);
    m_bearerToken = token;
    if (m_bearerToken.contains('\r') || m_bearerToken.contains('\n')) {
        m_bearerToken.clear();
    }
}

void ImageLoader::load(const QString &requestId, const QUrl &url)
{
    const bool allowedScheme = url.scheme() == QStringLiteral("https")
        || (m_allowInsecureHttp && url.scheme() == QStringLiteral("http"));
    if (!url.isValid() || !allowedScheme || url.host().isEmpty()) {
        emit imageFailed(requestId, QStringLiteral("Invalid or disallowed image URL"));
        return;
    }

    if (m_cache.contains(url)) {
        const QPixmap cached = m_cache.value(url);
        QTimer::singleShot(0, this, [this, requestId, cached]() {
            emit imageLoaded(requestId, cached);
        });
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Accept", "image/*");
    const bool bearerAuthenticationAttached = !m_bearerToken.isEmpty()
        && UrlOrigin::sameHttpOrigin(url, m_authenticatedServerOrigin);
    if (bearerAuthenticationAttached) {
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_bearerToken);
    }
    QNetworkReply *reply = m_networkManager->get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timeout, requestId, url,
             bearerAuthenticationAttached]() {
        timeout->stop();
        const int statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 401 && bearerAuthenticationAttached) {
            emit authenticationRequired();
            reply->deleteLater();
            return;
        }
        if (reply->property("timedOut").toBool()) {
            emit imageFailed(requestId, QStringLiteral("Image request timed out"));
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit imageFailed(requestId, reply->errorString());
            reply->deleteLater();
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll())) {
            emit imageFailed(requestId, QStringLiteral("Unsupported or invalid image data"));
            reply->deleteLater();
            return;
        }

        m_cache.insert(url, pixmap);
        emit imageLoaded(requestId, pixmap);
        reply->deleteLater();
    });
}

void ImageLoader::download(const QString &requestId, const QUrl &url)
{
    const bool allowedScheme = url.scheme() == QStringLiteral("https")
        || (m_allowInsecureHttp && url.scheme() == QStringLiteral("http"));
    if (!url.isValid() || !allowedScheme || url.host().isEmpty()) {
        emit imageDataFailed(
            requestId, QStringLiteral("Invalid or disallowed image URL"));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Accept", "image/*");
    const bool bearerAuthenticationAttached = !m_bearerToken.isEmpty()
        && UrlOrigin::sameHttpOrigin(url, m_authenticatedServerOrigin);
    if (bearerAuthenticationAttached) {
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_bearerToken);
    }

    QNetworkReply *reply = m_networkManager->get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timeout, requestId,
             bearerAuthenticationAttached]() {
        timeout->stop();
        const int statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 401 && bearerAuthenticationAttached) {
            emit authenticationRequired();
            emit imageDataFailed(
                requestId, QStringLiteral("Authentication required"));
            reply->deleteLater();
            return;
        }
        if (reply->property("timedOut").toBool()) {
            emit imageDataFailed(
                requestId, QStringLiteral("Image request timed out"));
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit imageDataFailed(requestId, reply->errorString());
            reply->deleteLater();
            return;
        }

        const QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            emit imageDataFailed(
                requestId, QStringLiteral("Downloaded image is empty"));
            reply->deleteLater();
            return;
        }
        const QString contentType = QString::fromLatin1(
            reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
        emit imageDataLoaded(requestId, data, contentType);
        reply->deleteLater();
    });
}
