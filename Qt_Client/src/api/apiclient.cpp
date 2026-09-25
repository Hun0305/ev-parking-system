#include "apiclient.h"

#include <QJsonParseError>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

ApiClient::ApiClient(const QUrl &baseUrl,
                     int timeoutMs,
                     bool allowInsecureHttp,
                     QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
    , m_timeoutMs(timeoutMs > 0 ? timeoutMs : 5000)
    , m_allowInsecureHttp(allowInsecureHttp)
{
}

void ApiClient::getJson(const QString &path)
{
    const QUrl url = requestUrl(path);
    const bool allowedScheme = url.scheme() == QStringLiteral("https")
        || (m_allowInsecureHttp && url.scheme() == QStringLiteral("http"));
    if (!url.isValid() || !allowedScheme || url.host().isEmpty()) {
        const QString safeUrl = url.toString(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
        emit requestFailed(path,
                           QStringLiteral("Invalid or disallowed API URL: %1").arg(safeUrl),
                           0, 0);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_networkManager->get(request);
    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timeout, path, startedAtMs]() {
        timeout->stop();
        const int latencyMs = static_cast<int>(
            qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAtMs));
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->property("timedOut").toBool()) {
            emit requestFailed(path, QStringLiteral("API request timed out"),
                               latencyMs, statusCode);
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(path, reply->errorString(), latencyMs, statusCode);
            reply->deleteLater();
            return;
        }

        if (statusCode < 200 || statusCode >= 300) {
            emit requestFailed(path, QStringLiteral("HTTP %1").arg(statusCode),
                               latencyMs, statusCode);
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit requestFailed(path,
                               QStringLiteral("Invalid JSON response: %1").arg(parseError.errorString()),
                               latencyMs, statusCode);
            reply->deleteLater();
            return;
        }

        emit jsonReceived(path, document, latencyMs, statusCode);
        reply->deleteLater();
    });
}

QUrl ApiClient::requestUrl(const QString &path) const
{
    if (QUrl(path).isRelative()) {
        QString base = m_baseUrl.toString();
        QString relativePath = path;
        while (base.endsWith('/')) {
            base.chop(1);
        }
        if (!relativePath.startsWith('/')) {
            relativePath.prepend('/');
        }
        return QUrl(base + relativePath);
    }
    return QUrl(path);
}
