#include "imageloader.h"

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
    request.setRawHeader("Accept", "image/*");
    QNetworkReply *reply = m_networkManager->get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, requestId, url]() {
        timeout->stop();
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