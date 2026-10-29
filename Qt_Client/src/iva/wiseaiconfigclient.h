#pragma once

#include "ivaareamodels.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QAuthenticator;
class QNetworkReply;
class QSslError;
class QTimer;

struct WiseAiConnectionOptions
{
    QUrl baseUrl;
    QString username;
    QString password;
    QString pinnedCertificateSha256;
    int timeoutMs = 15000;
};

class WiseAiConfigClient : public QObject
{
    Q_OBJECT

public:
    explicit WiseAiConfigClient(WiseAiConnectionOptions options,
                                QObject *parent = nullptr);

    void setConnectionOptions(WiseAiConnectionOptions options);
    bool isBusy() const;
    void fetchConfiguration();
    void applyChannelConfiguration(int channel,
                                   bool enabled,
                                   const QList<IvaAreaDefinition> &areas);
    void deleteArea(int channel, int areaIndex);
    void cancel();

    static QByteArray normalizeCertificateSha256(const QString &fingerprint);
    static QString formatCertificateSha256(const QByteArray &digest);

signals:
    void certificatePinned(const QString &sha256);
    void optionsReceived(const IvaAreaOptions &options);
    void capabilitiesReceived(const WiseAiCapabilities &capabilities);
    void configurationReceived(const IvaAreaConfiguration &configuration);
    void requestFailed(const QString &message);
    void applyStarted(int channel);
    void applySucceeded(int channel,
                        const IvaAreaConfiguration &verifiedConfiguration);
    void applyFailed(int channel,
                     const QString &message,
                     bool rollbackSucceeded);

private:
    enum class RequestKind {
        None,
        FetchOptions,
        FetchCapabilities,
        FetchConfiguration,
        PreflightConfiguration,
        PutUpdate,
        DeleteUpdate,
        VerifyUpdate,
        PutRollback,
        VerifyRollback
    };

    struct PendingUpdate {
        int channel = -1;
        QJsonObject baselineRawChannel;
        QJsonObject intendedPayload;
        QJsonObject rollbackPayload;
        int deletedAreaIndex = -1;
        int verificationAttempt = 0;
        QString lastVerificationError;
    };

    bool validateConnection(QString &errorMessage) const;
    void startGet(RequestKind kind, const QUrl &url);
    void startPut(RequestKind kind, const QJsonObject &payload);
    void startDelete(RequestKind kind, const QUrl &url);
    void scheduleVerification(RequestKind kind);
    void startRequest(RequestKind kind,
                      const QNetworkRequest &request,
                      const QByteArray &body = {});
    QNetworkRequest createRequest(const QUrl &url, bool jsonBody) const;
    void handleAuthentication(QNetworkReply *reply, QAuthenticator *authenticator);
    void handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);
    void finishRequest();
    void processSuccess(RequestKind kind, const QByteArray &body);
    void processFailure(RequestKind kind, const QString &message);
    void finishApplyFailure(const QString &message, bool rollbackSucceeded);
    bool parseConfiguration(const QByteArray &body,
                            IvaAreaConfiguration &configuration,
                            QString &errorMessage) const;
    bool parseOptions(const QByteArray &body,
                      IvaAreaOptions &options,
                      QString &errorMessage) const;
    bool parseCapabilities(const QByteArray &body,
                           WiseAiCapabilities &capabilities,
                           QString &errorMessage) const;
    QUrl configurationUrl(bool addSequenceId = true) const;
    QUrl deleteAreaUrl(int channel, int areaIndex) const;
    QUrl optionsUrl() const;
    QUrl capabilitiesUrl() const;

    WiseAiConnectionOptions m_options;
    QNetworkAccessManager m_network;
    QNetworkReply *m_pendingReply = nullptr;
    QTimer *m_verificationTimer = nullptr;
    RequestKind m_requestKind = RequestKind::None;
    QString m_tlsFailure;
    IvaAreaOptions m_lastOptions;
    WiseAiCapabilities m_lastCapabilities;
    IvaAreaConfiguration m_lastConfiguration;
    bool m_hasOptions = false;
    bool m_hasCapabilities = false;
    bool m_hasConfiguration = false;
    PendingUpdate m_pendingUpdate;
};
