#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QUrl>

struct AuthSession
{
    QByteArray accessToken;
    QUrl serverOrigin;
    qint64 userId = -1;
    QString accountId;
    QString displayName;
    QDateTime expiresAtUtc;

    bool isValid() const
    {
        return !accessToken.isEmpty()
            && serverOrigin.isValid()
            && !serverOrigin.host().isEmpty()
            && expiresAtUtc.isValid()
            && expiresAtUtc > QDateTime::currentDateTimeUtc();
    }
};

Q_DECLARE_METATYPE(AuthSession)
