#include "authclient.h"

#include "api/urlorigin.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {
qint64 firstInteger(const QJsonObject &object,
                    std::initializer_list<const char *> keys,
                    qint64 fallback = -1)
{
    for (const char *key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isDouble()) {
            return static_cast<qint64>(value.toDouble());
        }
    }
    return fallback;
}

QDateTime parseExpiry(const QJsonObject &object)
{
    const QString expiresAt = object.value(
        QStringLiteral("expiresAt")).toString();
    QDateTime parsed = QDateTime::fromString(expiresAt, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(expiresAt, Qt::ISODate);
    }
    return parsed.isValid() ? parsed.toUTC() : QDateTime();
}

bool isLoopbackHost(const QString &host)
{
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}
}

AuthClient::AuthClient(const QUrl &baseUrl,
                       int timeoutMs,
                       bool allowInsecureHttp,
                       QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_timeoutMs(qBound(1000, timeoutMs, 60000))
    , m_allowInsecureHttp(allowInsecureHttp)
{
    setBaseUrl(baseUrl);
}

bool AuthClient::setBaseUrl(const QUrl &baseUrl, QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    if (isRequestInFlight()) {
        return fail(QStringLiteral("Wait for the current sign-in attempt to finish."));
    }
    if (!baseUrl.isValid() || baseUrl.host().isEmpty()
        || !baseUrl.userInfo().isEmpty()) {
        return fail(QStringLiteral("Enter a valid server address."));
    }

    const QString scheme = baseUrl.scheme().toLower();
    const bool developmentLoopback = m_allowInsecureHttp
        && scheme == QStringLiteral("http")
        && isLoopbackHost(baseUrl.host());
    if (scheme != QStringLiteral("https") && !developmentLoopback) {
        return fail(QStringLiteral(
            "Use an https:// server address. Plain HTTP is limited to local tests."));
    }

    const QUrl origin = UrlOrigin::normalizedHttpOrigin(baseUrl);
    if (origin.isEmpty()) {
        return fail(QStringLiteral("Enter a valid server address."));
    }

    m_baseUrl = origin;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QUrl AuthClient::baseUrl() const
{
    return m_baseUrl;
}

bool AuthClient::isRequestInFlight() const
{
    return !m_reply.isNull();
}

void AuthClient::login(const QString &accountId, const QString &password)
{
    if (isRequestInFlight()) {
        return;
    }

    const QString normalizedAccountId = accountId.trimmed();
    if (m_baseUrl.isEmpty()) {
        emit loginFailed(QStringLiteral("Enter a valid server address."), 0);
        return;
    }
    if (normalizedAccountId.isEmpty() || password.isEmpty()) {
        emit loginFailed(QStringLiteral("Enter both account ID and password."), 0);
        return;
    }

    QUrl loginUrl = m_baseUrl;
    loginUrl.setPath(QStringLiteral("/api/v1/auth/login"));

    QNetworkRequest request(loginUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    const QJsonObject body{
        {QStringLiteral("accountId"), normalizedAccountId},
        {QStringLiteral("password"), password}
    };
    m_reply = m_networkManager->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;

    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("authTimedOut", true);
        reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, normalizedAccountId]() {
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseBody = reply->readAll();
        const bool timedOut = reply->property("authTimedOut").toBool();
        const QNetworkReply::NetworkError networkError = reply->error();
        const QByteArray retryAfter = reply->rawHeader("Retry-After").trimmed();
        m_reply.clear();
        reply->deleteLater();

        if (timedOut) {
            emit loginFailed(
                QStringLiteral("The server did not respond. Check the address and try again."),
                0);
            return;
        }

        if (status != 200 || networkError != QNetworkReply::NoError) {
            if (status == 401) {
                emit loginFailed(
                    QStringLiteral("Check the account ID and password."), 401);
                return;
            }
            if (status == 429) {
                const QString suffix = retryAfter.isEmpty()
                    ? QString()
                    : QStringLiteral(" Try again in %1 seconds.")
                          .arg(QString::fromLatin1(retryAfter));
                emit loginFailed(
                    QStringLiteral("Sign-in is temporarily limited.") + suffix,
                    429);
                return;
            }
            if (status >= 500) {
                emit loginFailed(
                    QStringLiteral("The server cannot process sign-in right now. Try again shortly."),
                    status);
                return;
            }
            if (status >= 400) {
                emit loginFailed(
                    QStringLiteral("The server rejected the sign-in request."),
                    status);
                return;
            }

            emit loginFailed(
                QStringLiteral("Cannot reach the server. Check the address and network."),
                status);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            emit loginFailed(
                QStringLiteral("The server returned an invalid sign-in response."),
                status);
            return;
        }

        const QJsonObject object = document.object();
        const bool success = object.value(QStringLiteral("success")).toBool();
        const QString token = object.value(
            QStringLiteral("accessToken")).toString();
        const QString tokenType = object.value(
            QStringLiteral("tokenType")).toString();
        const QByteArray tokenBytes = token.toUtf8();
        if (!success
            || tokenType.compare(QStringLiteral("Bearer"),
                                 Qt::CaseInsensitive) != 0
            || tokenBytes.isEmpty() || tokenBytes.size() > 4096
            || tokenBytes.contains('\r') || tokenBytes.contains('\n')) {
            emit loginFailed(
                QStringLiteral("The server returned an invalid sign-in response."),
                status);
            return;
        }

        AuthSession session;
        session.accessToken = tokenBytes;
        session.serverOrigin = m_baseUrl;
        session.expiresAtUtc = parseExpiry(object);

        const QJsonObject user = object.value(QStringLiteral("user")).toObject();
        session.userId = firstInteger(user, {"id"});
        session.accountId = user.value(
            QStringLiteral("accountId")).toString();
        if (session.accountId.isEmpty()) {
            session.accountId = normalizedAccountId;
        }
        session.displayName = user.value(
            QStringLiteral("displayName")).toString();

        if (!session.isValid()) {
            emit loginFailed(
                QStringLiteral("The server returned an invalid sign-in response."),
                status);
            return;
        }
        emit loginSucceeded(session);
    });
}
