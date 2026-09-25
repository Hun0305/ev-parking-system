#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

class ImageLoader : public QObject
{
    Q_OBJECT

public:
    explicit ImageLoader(int timeoutMs,
                         bool allowInsecureHttp,
                         QObject *parent = nullptr);

    void setBearerAuthentication(const QUrl &fixedLoginOrigin,
                                 const QByteArray &token);
    void load(const QString &requestId, const QUrl &url);
    void download(const QString &requestId, const QUrl &url);

signals:
    void authenticationRequired();
    void imageLoaded(const QString &requestId, const QPixmap &pixmap);
    void imageFailed(const QString &requestId, const QString &message);
    void imageDataLoaded(const QString &requestId,
                         const QByteArray &data,
                         const QString &contentType);
    void imageDataFailed(const QString &requestId, const QString &message);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QHash<QUrl, QPixmap> m_cache;
    QUrl m_authenticatedServerOrigin;
    QByteArray m_bearerToken;
    int m_timeoutMs = 5000;
    bool m_allowInsecureHttp = false;
};
