#include "wiseaiconfigclient.h"

#include "ivaareaoptionsparser.h"
#include "ivaareacapabilityparser.h"
#include "ivaareaparser.h"
#include "ivaareaupdatebuilder.h"

#include <QAuthenticator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace {
bool isPinnableCertificateError(QSslError::SslError error)
{
    switch (error) {
    case QSslError::SelfSignedCertificate:
    case QSslError::SelfSignedCertificateInChain:
    case QSslError::CertificateUntrusted:
    case QSslError::HostNameMismatch:
    case QSslError::UnableToGetLocalIssuerCertificate:
    case QSslError::UnableToVerifyFirstCertificate:
        return true;
    default:
        return false;
    }
}

QByteArray peerCertificateDigest(QNetworkReply *reply,
                                 const QList<QSslError> &errors)
{
    QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
    if (certificate.isNull()) {
        for (const QSslError &error : errors) {
            if (!error.certificate().isNull()) {
                certificate = error.certificate();
                break;
            }
        }
    }
    return certificate.isNull()
        ? QByteArray()
        : certificate.digest(QCryptographicHash::Sha256);
}

QJsonObject channelObject(const IvaAreaConfiguration &configuration, int channel)
{
    for (const IvaChannelDefinition &item : configuration.channels) {
        if (item.channel == channel) {
            return item.rawContainer;
        }
    }
    return {};
}

QByteArray compactObject(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

WiseAiConfigClient::WiseAiConfigClient(WiseAiConnectionOptions options,
                                       QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    connect(&m_network, &QNetworkAccessManager::authenticationRequired,
            this, &WiseAiConfigClient::handleAuthentication);
    connect(&m_network, &QNetworkAccessManager::sslErrors,
            this, &WiseAiConfigClient::handleSslErrors);
    m_verificationTimer = new QTimer(this);
    m_verificationTimer->setSingleShot(true);
    connect(m_verificationTimer, &QTimer::timeout, this, [this]() {
        const RequestKind kind = m_requestKind;
        m_requestKind = RequestKind::None;
        startGet(kind, configurationUrl());
    });
}

void WiseAiConfigClient::setConnectionOptions(WiseAiConnectionOptions options)
{
    if (isBusy()) {
        cancel();
    }
    m_options = std::move(options);
    m_hasOptions = false;
    m_hasCapabilities = false;
    m_hasConfiguration = false;
}

bool WiseAiConfigClient::isBusy() const
{
    return m_pendingReply != nullptr
        || (m_verificationTimer && m_verificationTimer->isActive());
}

void WiseAiConfigClient::fetchConfiguration()
{
    if (isBusy()) {
        emit requestFailed(QStringLiteral("A WiseAI request is already running"));
        return;
    }
    QString errorMessage;
    if (!validateConnection(errorMessage)) {
        emit requestFailed(errorMessage);
        return;
    }
    startGet(RequestKind::FetchOptions, optionsUrl());
}

void WiseAiConfigClient::applyChannelConfiguration(
    int channel,
    bool enabled,
    const QList<IvaAreaDefinition> &areas)
{
    if (isBusy()) {
        emit applyFailed(channel,
                         QStringLiteral("Another WiseAI request is already running"),
                         false);
        return;
    }
    QString errorMessage;
    if (!validateConnection(errorMessage)) {
        emit applyFailed(channel, errorMessage, false);
        return;
    }
    if (!m_hasOptions || !m_hasCapabilities || !m_hasConfiguration) {
        emit applyFailed(channel,
                         QStringLiteral("Refresh camera configuration before applying changes"),
                         false);
        return;
    }
    const IvaChannelOptions *channelOptions = m_lastOptions.forChannel(channel);
    const IvaChannelCapability *channelCapability = m_lastCapabilities.forChannel(
        channel);
    const QJsonObject baselineRaw = channelObject(m_lastConfiguration, channel);
    if (!channelOptions || !channelCapability
        || !channelCapability->ivaAreaSupported
        || !channelCapability->maxResolution.isValid()
        || baselineRaw.isEmpty()) {
        emit applyFailed(channel,
                         QStringLiteral("Camera options, capability, or baseline are missing for CH%1")
                             .arg(channel + 1),
                         false);
        return;
    }

    QJsonObject intendedPayload;
    if (!IvaAreaUpdateBuilder::buildChannelPayload(
            channel, enabled, areas, *channelOptions, intendedPayload,
            errorMessage, channelCapability->maxResolution)) {
        emit applyFailed(channel, errorMessage, false);
        return;
    }

    QList<IvaAreaDefinition> baselineAreas;
    for (const IvaAreaDefinition &area : m_lastConfiguration.areas) {
        if (area.channel == channel) {
            baselineAreas.append(area);
        }
    }
    const bool baselineEnabled = baselineRaw.value(QStringLiteral("enable")).toBool();
    QJsonObject rollbackPayload;
    if (!IvaAreaUpdateBuilder::buildChannelPayload(
            channel, baselineEnabled, baselineAreas, *channelOptions,
            rollbackPayload, errorMessage,
            channelCapability->maxResolution)) {
        emit applyFailed(channel,
                         QStringLiteral("The last-good camera state cannot be used for rollback: %1")
                             .arg(errorMessage),
                         false);
        return;
    }

    if (compactObject(intendedPayload) == compactObject(rollbackPayload)) {
        emit applyFailed(channel, QStringLiteral("No IVA values changed"), false);
        return;
    }

    m_pendingUpdate.channel = channel;
    m_pendingUpdate.baselineRawChannel = baselineRaw;
    m_pendingUpdate.intendedPayload = intendedPayload;
    m_pendingUpdate.rollbackPayload = rollbackPayload;
    emit applyStarted(channel);
    startGet(RequestKind::PreflightConfiguration, configurationUrl());
}

void WiseAiConfigClient::deleteArea(int channel, int areaIndex)
{
    if (isBusy()) {
        emit applyFailed(channel,
                         QStringLiteral("Another WiseAI request is already running"),
                         false);
        return;
    }
    QString errorMessage;
    if (!validateConnection(errorMessage)) {
        emit applyFailed(channel, errorMessage, false);
        return;
    }
    if (!m_hasOptions || !m_hasCapabilities || !m_hasConfiguration) {
        emit applyFailed(channel,
                         QStringLiteral("Refresh camera configuration before deleting an Area"),
                         false);
        return;
    }

    const IvaChannelOptions *channelOptions = m_lastOptions.forChannel(channel);
    const IvaChannelCapability *channelCapability = m_lastCapabilities.forChannel(
        channel);
    const QJsonObject baselineRaw = channelObject(m_lastConfiguration, channel);
    if (!channelOptions || !channelCapability
        || !channelCapability->ivaAreaSupported
        || !channelCapability->maxResolution.isValid()
        || baselineRaw.isEmpty()) {
        emit applyFailed(channel,
                         QStringLiteral("Camera options, capability, or baseline are missing for CH%1")
                             .arg(channel + 1),
                         false);
        return;
    }

    QList<IvaAreaDefinition> baselineAreas;
    QList<IvaAreaDefinition> remainingAreas;
    bool found = false;
    for (const IvaAreaDefinition &area : m_lastConfiguration.areas) {
        if (area.channel != channel) {
            continue;
        }
        baselineAreas.append(area);
        if (area.areaIndex == areaIndex) {
            found = true;
        } else {
            remainingAreas.append(area);
        }
    }
    if (!found) {
        emit applyFailed(channel,
                         QStringLiteral("IVA Area %1 was not found on CH%2")
                             .arg(areaIndex)
                             .arg(channel + 1),
                         false);
        return;
    }

    const bool enabled = baselineRaw.value(QStringLiteral("enable")).toBool();
    QJsonObject intendedPayload;
    if (!IvaAreaUpdateBuilder::buildChannelPayload(
            channel, enabled, remainingAreas, *channelOptions, intendedPayload,
            errorMessage, channelCapability->maxResolution)) {
        emit applyFailed(channel, errorMessage, false);
        return;
    }
    QJsonObject rollbackPayload;
    if (!IvaAreaUpdateBuilder::buildChannelPayload(
            channel, enabled, baselineAreas, *channelOptions, rollbackPayload,
            errorMessage, channelCapability->maxResolution)) {
        emit applyFailed(channel,
                         QStringLiteral("The last-good camera state cannot be used for rollback: %1")
                             .arg(errorMessage),
                         false);
        return;
    }

    m_pendingUpdate.channel = channel;
    m_pendingUpdate.baselineRawChannel = baselineRaw;
    m_pendingUpdate.intendedPayload = intendedPayload;
    m_pendingUpdate.rollbackPayload = rollbackPayload;
    m_pendingUpdate.deletedAreaIndex = areaIndex;
    emit applyStarted(channel);
    startGet(RequestKind::PreflightConfiguration, configurationUrl());
}

void WiseAiConfigClient::cancel()
{
    if (m_verificationTimer) {
        m_verificationTimer->stop();
    }
    if (m_pendingReply) {
        m_pendingReply->abort();
    }
}

QByteArray WiseAiConfigClient::normalizeCertificateSha256(
    const QString &fingerprint)
{
    QString normalized = fingerprint.trimmed();
    normalized.remove(QLatin1Char(':'));
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char(' '));
    if (normalized.size() != 64) {
        return {};
    }
    for (const QChar character : normalized) {
        if (!character.isDigit()
            && (character.toUpper() < QLatin1Char('A')
                || character.toUpper() > QLatin1Char('F'))) {
            return {};
        }
    }
    return QByteArray::fromHex(normalized.toLatin1());
}

QString WiseAiConfigClient::formatCertificateSha256(const QByteArray &digest)
{
    const QByteArray hexadecimal = digest.toHex().toUpper();
    QStringList pairs;
    for (int index = 0; index + 1 < hexadecimal.size(); index += 2) {
        pairs.append(QString::fromLatin1(hexadecimal.mid(index, 2)));
    }
    return pairs.join(QLatin1Char(':'));
}

bool WiseAiConfigClient::validateConnection(QString &errorMessage) const
{
    if (!m_options.baseUrl.isValid()
        || m_options.baseUrl.scheme().compare(QStringLiteral("https"),
                                              Qt::CaseInsensitive) != 0
        || m_options.baseUrl.host().isEmpty()) {
        errorMessage = QStringLiteral(
            "WiseAI configuration requires a valid HTTPS camera URL");
        return false;
    }
    if (m_options.username.trimmed().isEmpty()) {
        errorMessage = QStringLiteral(
            "WiseAI configuration requires a camera username");
        return false;
    }
    const QByteArray pin = normalizeCertificateSha256(
        m_options.pinnedCertificateSha256);
    if (!m_options.pinnedCertificateSha256.trimmed().isEmpty() && pin.isEmpty()) {
        errorMessage = QStringLiteral(
            "The camera certificate SHA-256 fingerprint is invalid");
        return false;
    }
    errorMessage.clear();
    return true;
}

void WiseAiConfigClient::startGet(RequestKind kind, const QUrl &url)
{
    startRequest(kind, createRequest(url, false));
}

void WiseAiConfigClient::startPut(RequestKind kind, const QJsonObject &payload)
{
    startRequest(kind, createRequest(configurationUrl(false), true),
                 QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void WiseAiConfigClient::startDelete(RequestKind kind, const QUrl &url)
{
    m_tlsFailure.clear();
    m_requestKind = kind;
    m_pendingReply = m_network.deleteResource(createRequest(url, false));
    connect(m_pendingReply, &QNetworkReply::finished,
            this, &WiseAiConfigClient::finishRequest);
}

void WiseAiConfigClient::scheduleVerification(RequestKind kind)
{
    static constexpr int delaysMs[] = {250, 750, 1500};
    static constexpr int delayCount = 3;
    const int attempt = qBound(0, m_pendingUpdate.verificationAttempt,
                               delayCount - 1);
    ++m_pendingUpdate.verificationAttempt;
    m_requestKind = kind;
    m_verificationTimer->start(delaysMs[attempt]);
}

void WiseAiConfigClient::startRequest(RequestKind kind,
                                      const QNetworkRequest &request,
                                      const QByteArray &body)
{
    m_tlsFailure.clear();
    m_requestKind = kind;
    m_pendingReply = body.isEmpty()
        ? m_network.get(request)
        : m_network.put(request, body);
    connect(m_pendingReply, &QNetworkReply::finished,
            this, &WiseAiConfigClient::finishRequest);
}

QNetworkRequest WiseAiConfigClient::createRequest(const QUrl &url,
                                                  bool jsonBody) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("VEDA-Qt-IVA-Configurator/1.0"));
    request.setRawHeader("Accept", "application/json, application/octet-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    if (jsonBody) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    request.setTransferTimeout(qMax(1000, m_options.timeoutMs));
#endif
    return request;
}

void WiseAiConfigClient::handleAuthentication(QNetworkReply *reply,
                                              QAuthenticator *authenticator)
{
    if (reply != m_pendingReply) {
        return;
    }
    authenticator->setUser(m_options.username);
    authenticator->setPassword(m_options.password);
}

void WiseAiConfigClient::handleSslErrors(QNetworkReply *reply,
                                         const QList<QSslError> &errors)
{
    if (reply != m_pendingReply) {
        return;
    }

    const QByteArray receivedDigest = peerCertificateDigest(reply, errors);
    const QByteArray configuredDigest = normalizeCertificateSha256(
        m_options.pinnedCertificateSha256);
    const QString receivedFingerprint = formatCertificateSha256(receivedDigest);
    if (configuredDigest.isEmpty()) {
        if (receivedFingerprint.isEmpty()) {
            m_tlsFailure = QStringLiteral("The camera TLS certificate is not trusted");
            reply->abort();
            return;
        }
        m_options.pinnedCertificateSha256 = receivedFingerprint;
        emit certificatePinned(receivedFingerprint);
    } else if (receivedDigest != configuredDigest) {
        m_tlsFailure = QStringLiteral(
            "The camera TLS certificate does not match the configured SHA-256 pin. Received %1")
                           .arg(receivedFingerprint.isEmpty()
                                    ? QStringLiteral("no certificate")
                                    : receivedFingerprint);
        reply->abort();
        return;
    }
    for (const QSslError &error : errors) {
        if (!isPinnableCertificateError(error.error())) {
            m_tlsFailure = QStringLiteral(
                "The pinned camera certificate has an unsafe TLS error: %1")
                               .arg(error.errorString());
            reply->abort();
            return;
        }
    }
    reply->ignoreSslErrors(errors);
}

void WiseAiConfigClient::finishRequest()
{
    QNetworkReply *reply = m_pendingReply;
    const RequestKind kind = m_requestKind;
    m_pendingReply = nullptr;
    m_requestKind = RequestKind::None;
    if (!reply) {
        return;
    }

    const QString tlsFailure = m_tlsFailure;
    m_tlsFailure.clear();
    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (!tlsFailure.isEmpty()) {
        processFailure(kind, tlsFailure);
        return;
    }
    if (networkError != QNetworkReply::NoError) {
        const QString message = statusCode == 401
            ? QStringLiteral("Camera authentication failed")
            : QStringLiteral("WiseAI request failed: %1").arg(networkMessage);
        processFailure(kind, message);
        return;
    }
    if (statusCode < 200 || statusCode >= 300) {
        processFailure(kind,
                       QStringLiteral("WiseAI request returned HTTP %1")
                           .arg(statusCode));
        return;
    }
    processSuccess(kind, body);
}

void WiseAiConfigClient::processSuccess(RequestKind kind,
                                        const QByteArray &body)
{
    if (kind == RequestKind::FetchOptions) {
        IvaAreaOptions options;
        QString errorMessage;
        if (!parseOptions(body, options, errorMessage)) {
            emit requestFailed(errorMessage);
            return;
        }
        m_lastOptions = options;
        m_hasOptions = true;
        emit optionsReceived(options);
        startGet(RequestKind::FetchCapabilities, capabilitiesUrl());
        return;
    }

    if (kind == RequestKind::FetchCapabilities) {
        WiseAiCapabilities capabilities;
        QString errorMessage;
        if (!parseCapabilities(body, capabilities, errorMessage)) {
            emit requestFailed(errorMessage);
            return;
        }
        m_lastCapabilities = capabilities;
        m_hasCapabilities = true;
        emit capabilitiesReceived(capabilities);
        startGet(RequestKind::FetchConfiguration, configurationUrl());
        return;
    }

    if (kind == RequestKind::PutUpdate) {
        m_pendingUpdate.verificationAttempt = 0;
        scheduleVerification(RequestKind::VerifyUpdate);
        return;
    }
    if (kind == RequestKind::DeleteUpdate) {
        m_pendingUpdate.verificationAttempt = 0;
        scheduleVerification(RequestKind::VerifyUpdate);
        return;
    }
    if (kind == RequestKind::PutRollback) {
        m_pendingUpdate.verificationAttempt = 0;
        scheduleVerification(RequestKind::VerifyRollback);
        return;
    }

    IvaAreaConfiguration configuration;
    QString errorMessage;
    if (!parseConfiguration(body, configuration, errorMessage)) {
        processFailure(kind, errorMessage);
        return;
    }

    if (kind == RequestKind::FetchConfiguration) {
        m_lastConfiguration = configuration;
        m_hasConfiguration = true;
        emit configurationReceived(configuration);
        return;
    }

    const IvaChannelOptions *channelOptions = m_lastOptions.forChannel(
        m_pendingUpdate.channel);
    const IvaChannelCapability *channelCapability = m_lastCapabilities.forChannel(
        m_pendingUpdate.channel);
    if (!channelOptions || !channelCapability
        || !channelCapability->ivaAreaSupported
        || !channelCapability->maxResolution.isValid()) {
        finishApplyFailure(QStringLiteral("Camera options or capability disappeared during apply"),
                           false);
        return;
    }

    if (kind == RequestKind::PreflightConfiguration) {
        const QJsonObject current = channelObject(configuration,
                                                  m_pendingUpdate.channel);
        if (current.isEmpty()
            || compactObject(current)
                   != compactObject(m_pendingUpdate.baselineRawChannel)) {
            m_lastConfiguration = configuration;
            m_hasConfiguration = true;
            emit configurationReceived(configuration);
            finishApplyFailure(
                QStringLiteral("Camera IVA changed after it was loaded. Review the refreshed values and apply again."),
                false);
            return;
        }
        if (m_pendingUpdate.deletedAreaIndex >= 0) {
            startDelete(RequestKind::DeleteUpdate,
                        deleteAreaUrl(m_pendingUpdate.channel,
                                      m_pendingUpdate.deletedAreaIndex));
        } else {
            startPut(RequestKind::PutUpdate, m_pendingUpdate.intendedPayload);
        }
        return;
    }

    if (kind == RequestKind::VerifyUpdate) {
        if (IvaAreaUpdateBuilder::payloadMatchesChannel(
                m_pendingUpdate.intendedPayload, configuration, *channelOptions,
                errorMessage, channelCapability->maxResolution)) {
            const int channel = m_pendingUpdate.channel;
            m_lastConfiguration = configuration;
            m_hasConfiguration = true;
            m_pendingUpdate = {};
            emit configurationReceived(configuration);
            emit applySucceeded(channel, configuration);
            return;
        }
        m_pendingUpdate.lastVerificationError = errorMessage;
        if (m_pendingUpdate.verificationAttempt < 3) {
            scheduleVerification(RequestKind::VerifyUpdate);
            return;
        }
        startPut(RequestKind::PutRollback, m_pendingUpdate.rollbackPayload);
        return;
    }

    if (kind == RequestKind::VerifyRollback) {
        if (IvaAreaUpdateBuilder::payloadMatchesChannel(
                m_pendingUpdate.rollbackPayload, configuration, *channelOptions,
                errorMessage, channelCapability->maxResolution)) {
            m_lastConfiguration = configuration;
            m_hasConfiguration = true;
            emit configurationReceived(configuration);
            finishApplyFailure(
                QStringLiteral(
                    "Camera verification differed from the requested values; "
                    "last-good IVA was restored. %1")
                    .arg(m_pendingUpdate.lastVerificationError),
                true);
            return;
        }
        if (m_pendingUpdate.verificationAttempt < 3) {
            scheduleVerification(RequestKind::VerifyRollback);
            return;
        }
        finishApplyFailure(
            QStringLiteral("Camera verification failed and rollback could not be verified: %1")
                .arg(errorMessage),
            false);
    }
}

void WiseAiConfigClient::processFailure(RequestKind kind,
                                        const QString &message)
{
    if (kind == RequestKind::FetchOptions
        || kind == RequestKind::FetchCapabilities
        || kind == RequestKind::FetchConfiguration) {
        emit requestFailed(message);
        return;
    }
    if (kind == RequestKind::PutUpdate
        || kind == RequestKind::DeleteUpdate) {
        m_pendingUpdate.verificationAttempt = 0;
        scheduleVerification(RequestKind::VerifyUpdate);
        return;
    }
    if (kind == RequestKind::PutRollback) {
        finishApplyFailure(
            QStringLiteral("Rollback request failed; camera state requires a refresh: %1")
                .arg(message),
            false);
        return;
    }
    if (kind == RequestKind::VerifyUpdate) {
        finishApplyFailure(
            QStringLiteral("Apply outcome could not be verified; refresh the camera before further edits: %1")
                .arg(message),
            false);
        return;
    }
    if (kind == RequestKind::VerifyRollback) {
        finishApplyFailure(
            QStringLiteral("Rollback outcome could not be verified; refresh the camera: %1")
                .arg(message),
            false);
        return;
    }
    finishApplyFailure(message, false);
}

void WiseAiConfigClient::finishApplyFailure(const QString &message,
                                            bool rollbackSucceeded)
{
    const int channel = m_pendingUpdate.channel;
    m_pendingUpdate = {};
    emit applyFailed(channel, message, rollbackSucceeded);
}

bool WiseAiConfigClient::parseConfiguration(
    const QByteArray &body,
    IvaAreaConfiguration &configuration,
    QString &errorMessage) const
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        errorMessage = QStringLiteral("WiseAI configuration is not valid JSON: %1")
                           .arg(jsonError.errorString());
        return false;
    }
    QString structureError;
    if (!IvaAreaParser::parse(document, configuration, structureError)) {
        errorMessage = QStringLiteral("WiseAI configuration structure is invalid: %1")
                           .arg(structureError);
        return false;
    }
    return true;
}

bool WiseAiConfigClient::parseOptions(const QByteArray &body,
                                      IvaAreaOptions &options,
                                      QString &errorMessage) const
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        errorMessage = QStringLiteral("WiseAI IVA options are not valid JSON: %1")
                           .arg(jsonError.errorString());
        return false;
    }
    QString structureError;
    if (!IvaAreaOptionsParser::parse(document, options, structureError)) {
        errorMessage = QStringLiteral("WiseAI IVA options are invalid: %1")
                           .arg(structureError);
        return false;
    }
    return true;
}

bool WiseAiConfigClient::parseCapabilities(
    const QByteArray &body,
    WiseAiCapabilities &capabilities,
    QString &errorMessage) const
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        errorMessage = QStringLiteral("WiseAI capability is not valid JSON: %1")
                           .arg(jsonError.errorString());
        return false;
    }
    QString structureError;
    if (!IvaAreaCapabilityParser::parse(document, capabilities, structureError)) {
        errorMessage = QStringLiteral("WiseAI capability is invalid: %1")
                           .arg(structureError);
        return false;
    }
    return true;
}

QUrl WiseAiConfigClient::configurationUrl(bool addSequenceId) const
{
    QUrl url = m_options.baseUrl;
    url.setPath(QStringLiteral("/opensdk/WiseAI/configuration/ivaarea"));
    if (addSequenceId) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("SunapiSeqId"),
                           QString::number(QDateTime::currentMSecsSinceEpoch()));
        url.setQuery(query);
    } else {
        url.setQuery(QString());
    }
    return url;
}

QUrl WiseAiConfigClient::deleteAreaUrl(int channel, int areaIndex) const
{
    QUrl url = m_options.baseUrl;
    url.setPath(QStringLiteral(
        "/opensdk/WiseAI/configuration/ivaarea/definedarea"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("channel"), QString::number(channel));
    query.addQueryItem(QStringLiteral("index"), QString::number(areaIndex));
    url.setQuery(query);
    return url;
}

QUrl WiseAiConfigClient::optionsUrl() const
{
    QUrl url = m_options.baseUrl;
    url.setPath(QStringLiteral("/opensdk/WiseAI/configuration/ivaarea/options"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("SunapiSeqId"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    return url;
}

QUrl WiseAiConfigClient::capabilitiesUrl() const
{
    QUrl url = m_options.baseUrl;
    url.setPath(QStringLiteral("/opensdk/WiseAI/configuration/capability"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("SunapiSeqId"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.setQuery(query);
    return url;
}
