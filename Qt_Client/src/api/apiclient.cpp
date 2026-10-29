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
    sendJsonRequest(path, QByteArrayLiteral("GET"));
}

void ApiClient::putJson(const QString &path, const QJsonObject &body)
{
    sendJsonRequest(path, QByteArrayLiteral("PUT"),
                    QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void ApiClient::getJsonTagged(const QString &path, const QString &requestTag)
{
    sendJsonRequest(path, QByteArrayLiteral("GET"), QByteArray(), requestTag);
}

void ApiClient::putJsonTagged(const QString &path, const QJsonObject &body,
                              const QString &requestTag)
{
    sendJsonRequest(path, QByteArrayLiteral("PUT"),
                    QJsonDocument(body).toJson(QJsonDocument::Compact),
                    requestTag);
}

void ApiClient::sendJsonRequest(const QString &path, const QByteArray &method,
                                const QByteArray &body,
                                const QString &requestTag)
{
    const QUrl url = requestUrl(path);
    const bool allowedScheme = url.scheme() == QStringLiteral("https")
        || (m_allowInsecureHttp && url.scheme() == QStringLiteral("http"));
    if (!url.isValid() || !allowedScheme || url.host().isEmpty()) {
        const QString safeUrl = url.toString(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
        const QString message = QStringLiteral(
            "Invalid or disallowed API URL: %1").arg(safeUrl);
        if (requestTag.isEmpty()) emit requestFailed(path, message, 0, 0);
        else emit taggedRequestFailed(requestTag, path, message, 0, 0);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    }

    QNetworkReply *reply = method == QByteArrayLiteral("GET")
        ? m_networkManager->get(request)
        : m_networkManager->sendCustomRequest(request, method, body);
    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timeout, path, requestTag, startedAtMs]() {
        timeout->stop();
        const int latencyMs = static_cast<int>(
            qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAtMs));
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseBody = reply->readAll();

        if (reply->property("timedOut").toBool()) {
            const QString message = QStringLiteral("API request timed out");
            if (requestTag.isEmpty()) {
                emit requestFailed(path, message, latencyMs, statusCode);
            } else {
                emit taggedRequestFailed(requestTag, path, message,
                                         latencyMs, statusCode);
            }
            reply->deleteLater();
            return;
        }
        if (statusCode == 0 && reply->error() != QNetworkReply::NoError) {
            if (requestTag.isEmpty()) {
                emit requestFailed(path, reply->errorString(), latencyMs, statusCode);
            } else {
                emit taggedRequestFailed(requestTag, path, reply->errorString(),
                                         latencyMs, statusCode);
            }
            reply->deleteLater();
            return;
        }
        if (statusCode < 200 || statusCode >= 300) {
            QString message = QStringLiteral("HTTP %1").arg(statusCode);
            QJsonParseError errorParseResult;
            const QJsonDocument errorDocument = QJsonDocument::fromJson(
                responseBody, &errorParseResult);
            if (errorParseResult.error == QJsonParseError::NoError
                && errorDocument.isObject()) {
                const QString serverError = errorDocument.object()
                    .value(QStringLiteral("error")).toString().trimmed();
                if (!serverError.isEmpty()) {
                    message += QStringLiteral(": ") + serverError;
                }
            }
            if (requestTag.isEmpty()) {
                emit requestFailed(path, message, latencyMs, statusCode);
            } else {
                emit taggedRequestFailed(requestTag, path, message,
                                         latencyMs, statusCode);
            }
            reply->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (requestTag.isEmpty()) {
                emit requestFailed(path, reply->errorString(), latencyMs, statusCode);
            } else {
                emit taggedRequestFailed(requestTag, path, reply->errorString(),
                                         latencyMs, statusCode);
            }
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            const QString message = QStringLiteral(
                "Invalid JSON response: %1").arg(parseError.errorString());
            if (requestTag.isEmpty()) {
                emit requestFailed(path, message, latencyMs, statusCode);
            } else {
                emit taggedRequestFailed(requestTag, path, message,
                                         latencyMs, statusCode);
            }
            reply->deleteLater();
            return;
        }

        if (requestTag.isEmpty()) {
            emit jsonReceived(path, document, latencyMs, statusCode);
        } else {
            emit taggedJsonReceived(requestTag, path, document,
                                    latencyMs, statusCode);
        }
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
