#pragma once

#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(const QUrl &baseUrl,
                       int timeoutMs,
                       bool allowInsecureHttp,
                       QObject *parent = nullptr);

    void getJson(const QString &path);

signals:
    void jsonReceived(const QString &path, const QJsonDocument &document,
                      int latencyMs, int httpStatus);
    void requestFailed(const QString &path, const QString &message,
                       int latencyMs, int httpStatus);

private:
    QUrl requestUrl(const QString &path) const;

    QNetworkAccessManager *m_networkManager = nullptr;
    QUrl m_baseUrl;
    int m_timeoutMs = 5000;
    bool m_allowInsecureHttp = false;
};
