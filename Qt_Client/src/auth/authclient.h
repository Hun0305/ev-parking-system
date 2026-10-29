#pragma once

#include "authsession.h"

#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class AuthClient final : public QObject
{
    Q_OBJECT

public:
    explicit AuthClient(const QUrl &baseUrl,
                        int timeoutMs,
                        bool allowInsecureHttp,
                        QObject *parent = nullptr);

    bool setBaseUrl(const QUrl &baseUrl, QString *errorMessage = nullptr);
    QUrl baseUrl() const;
    bool isRequestInFlight() const;
    void login(const QString &accountId, const QString &password);

signals:
    void loginSucceeded(const AuthSession &session);
    void loginFailed(const QString &message, int httpStatus);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QPointer<QNetworkReply> m_reply;
    QUrl m_baseUrl;
    int m_timeoutMs = 5000;
    bool m_allowInsecureHttp = false;
};
