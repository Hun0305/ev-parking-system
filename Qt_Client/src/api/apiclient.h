#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    void putJson(const QString &path, const QJsonObject &body);
    void getJsonTagged(const QString &path, const QString &requestTag);
    void putJsonTagged(const QString &path, const QJsonObject &body,
                       const QString &requestTag);

signals:
    void jsonReceived(const QString &path, const QJsonDocument &document,
                      int latencyMs, int httpStatus);
    void requestFailed(const QString &path, const QString &message,
                       int latencyMs, int httpStatus);
    void taggedJsonReceived(const QString &requestTag, const QString &path,
                            const QJsonDocument &document,
                            int latencyMs, int httpStatus);
    void taggedRequestFailed(const QString &requestTag, const QString &path,
                             const QString &message,
                             int latencyMs, int httpStatus);

private:
    void sendJsonRequest(const QString &path, const QByteArray &method,
                         const QByteArray &body = QByteArray(),
                         const QString &requestTag = QString());
    QUrl requestUrl(const QString &path) const;

    QNetworkAccessManager *m_networkManager = nullptr;
    QUrl m_baseUrl;
    int m_timeoutMs = 5000;
    bool m_allowInsecureHttp = false;
};
