#pragma once

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

    void load(const QString &requestId, const QUrl &url);

signals:
    void imageLoaded(const QString &requestId, const QPixmap &pixmap);
    void imageFailed(const QString &requestId, const QString &message);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QHash<QUrl, QPixmap> m_cache;
    int m_timeoutMs = 5000;
    bool m_allowInsecureHttp = false;
};