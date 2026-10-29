#ifndef PARKINGCONTROLLER_H
#define PARKINGCONTROLLER_H

#include "diagnostics/diagnostictypes.h"
#include "api/parkingroi.h"
#include "models/monitoringevent.h"
#include "models/parkingstate.h"
#include "models/slotidmapper.h"

#include <QByteArray>
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QUrl>

class ApiClient;
class QDateTime;
class ImageLoader;
class QJsonDocument;
class QJsonObject;
class MqttServiceClient;
class QTimer;
struct ServerParkingEvent;
struct ServerFireEvent;

class ParkingController : public QObject
{
    Q_OBJECT

public:
    explicit ParkingController(const QString &sharedConfigPath,
                               const QString &localConfigPath,
                               QObject *parent = nullptr);

    void start();
    const ParkingViewState &state() const { return m_state; }
    ImageLoader *imageLoader() const { return m_imageLoader; }
    SlotState slotState(const QString &slotId) const;
    QString plateNumber(const QString &slotId) const;
    QList<ParkingImageResource> images(const QString &slotId) const;
    bool hasEventEvidence(const QString &eventId) const;
    QString eventEvidenceSlotId(const QString &eventId) const;
    QString resolveParkingZoneId(const QString &sourceId) const;
    void replaceViewState(const ParkingViewState &state);
    void applyEvSlotUpdate(const QString &slotId, SlotState state,
                           const QString &plateNumber, bool isEv,
                           const QString &occupiedTime, const QString &alarmText);
    void applyParkingSlotUpdate(const QString &slotId, SlotState state);

public slots:
    void requestSlotDetail(const QString &slotId);
    void requestEventEvidence(const QString &eventId);
    void setBearerAuthentication(const QUrl &fixedLoginOrigin,
                                 const QByteArray &token);
    void updateServerBaseUrl(const QString &baseUrl);
    void reconnectNow();
    void requestOverstayThreshold();
    void updateOverstayThreshold(int seconds);
    void requestParkingRois(quint64 generation);
    void requestParkingRoi(const QString &slotId, quint64 generation);
    void updateParkingRoi(const QString &slotId, const ParkingRoi &roi,
                          quint64 generation);
    void clearAlarms();
    void acknowledgeFireAlarm(const QString &channelId);
    void applyManualJsonMessage(const QJsonObject &json);
    void recordEvent(const QString &zone, const QString &eventType,
                     const QString &message, const QString &status);

signals:
    void authenticationExpired();
    void stateChanged();
    void bannerChanged(const QString &message, bool hasAlert);
    void statusMessageChanged(const QString &message);
    void eventLogged(const MonitoringEvent &event);
    void slotDetailReady(const QString &slotId);
    void slotDetailFailed(const QString &slotId, const QString &message);
    void eventEvidenceReady(const QString &eventId, const QString &slotId,
                            qint64 sessionId, SlotState state,
                            const QString &plateNumber,
                            const QList<ParkingImageResource> &images);
    void eventEvidenceFailed(const QString &eventId, const QString &slotId,
                             const QString &message);
    void detailError(const QString &message);
    void imageLoaderChanged(ImageLoader *imageLoader);
    void serverBaseUrlChanged(const QString &baseUrl);
    void serverConnectionChanged(const QString &status, bool connected);
    void apiDiagnosticChanged(const ApiDiagnosticState &state);
    void serverConfigurationError(const QString &message);
    void overstayThresholdRequestStarted(const QString &status);
    void overstayThresholdReceived(int seconds, const QString &applyPolicy,
                                   bool afterUpdate);
    void overstayThresholdRequestFailed(const QString &message,
                                        bool updateRequest);
    void parkingRoiListReceived(const ParkingRoiMap &rois,
                                quint64 generation);
    void parkingRoiReceived(const QString &slotId, const ParkingRoi &roi,
                            quint64 generation, bool afterSave,
                            bool appliedImmediately);
    void parkingRoiRequestFailed(const QString &slotId,
                                 const QString &message,
                                 quint64 generation,
                                 bool saveRequest);
    void fireAcknowledgementCommandPrepared(const QString &topic,
                                             const QByteArray &payload);
    void fireConfirmationRequested(const QString &channelId,
                                   const QString &alarmId);
    void fireConfirmationRetryRequested(const QString &channelId,
                                        const QString &alarmId);
    void fireConfirmationClosed(const QString &channelId,
                                const QString &alarmId);

private slots:
    void handleMqttMessage(const QString &topic, const QByteArray &payload);
    void handleMqttMessageWithMetadata(const QString &topic,
                                       const QByteArray &payload,
                                       bool retained);
    void applyParkingSnapshot(const QJsonDocument &document);

private:
    struct FirePreflightResult {
        bool accepted = false;
        bool applyState = false;
        bool recordHistory = false;
        bool revisioned = false;
        QString errorMessage;
    };

    struct FireRevisionLedger {
        bool versionedSeen = false;
        quint64 lastAppliedRevision = 0;
        QByteArray stateFingerprint;
        QHash<QString, QByteArray> eventFingerprintById;
        QHash<QString, QString> deliveryIdBySink;
        bool lifecycleHistoryRecorded = false;
    };

    struct EventEvidenceReference {
        QString eventId;
        QString slotId;
        qint64 sessionId = -1;
        SlotState state = SlotState::Vacant;
        QString plateNumber;
    };

    enum class EventEvidenceRequestKind { SlotDetail, SessionImages };

    struct PendingEventEvidenceRequest {
        EventEvidenceReference reference;
        EventEvidenceRequestKind kind = EventEvidenceRequestKind::SlotDetail;
    };

    void initializeApiClient();
    void initializeMqttClient();
    FirePreflightResult preflightFireEvent(const ServerFireEvent &event,
                                           const QString &topic,
                                           bool retained);
    void applyChannelFireEvent(const ServerFireEvent &event,
                               const QString &topic,
                               bool retained);
    void recordChannelFireEvent(const ServerFireEvent &event);
    bool applyServerParkingEvent(const QJsonObject &event,
                                 const QString &topic);
    void recordServerEvent(const ServerParkingEvent &event,
                           const QString &slotId);
    void rememberEventEvidence(const MonitoringEvent &event,
                               const QString &plateNumber = QString());
    EventEvidenceReference eventEvidenceReference(
        const QString &eventId) const;
    void requestEventEvidenceSlotDetail(
        const EventEvidenceReference &reference);
    void requestEventEvidenceSession(
        const EventEvidenceReference &reference);
    void applyEventEvidenceResponse(const QString &requestTag,
                                    const QJsonDocument &document);
    void applyEventEvidenceError(const QString &requestTag,
                                 const QString &message);
    void emitEventEvidenceReady(const EventEvidenceReference &reference,
                                QList<ParkingImageResource> images);
    bool rememberServerEventId(const QString &eventId);
    void rebuildApiClient();
    void scheduleReconnect(const QString &reason);
    void applyParkingSlotDetail(const QString &requestedSlotId,
                                const QJsonDocument &document);
    void applyParkingSessionImages(const QString &slotId,
                                   const QJsonDocument &document);
    void applyOverstayThresholdResponse(const QJsonDocument &document);
    void applyParkingRoiResponse(const QString &requestTag,
                                 const QJsonDocument &document);
    void applyParkingRoiError(const QString &requestTag,
                              const QString &message);
    QString parkingRoiPath(const QString &slotId) const;
    QString parkingRoiZoneId(const QString &slotId) const;
    QString parkingRoiServerSlotId(const QString &zoneId) const;
    void resetSlotsForSnapshot();
    void notifyStateChanged();
    void refreshAlert();
    void publishApiDiagnostic();
    QUrl resolveApiUrl(const QUrl &url) const;
    QString occupiedDurationText(const QDateTime &occupiedSince, int elapsedSeconds) const;

    QString m_sharedConfigPath;
    QString m_localConfigPath;
    ParkingViewState m_state;
    ApiClient *m_apiClient = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    MqttServiceClient *m_mqttClient = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QSet<QString> m_seenServerEventIds;
    QQueue<QString> m_seenServerEventOrder;
    QHash<QString, EventEvidenceReference> m_eventEvidenceReferences;
    QQueue<QString> m_eventEvidenceOrder;
    QSet<QString> m_fireAckCommandKeys;
    QHash<QString, FireRevisionLedger> m_fireRevisionLedgers;
    QHash<QString, QByteArray> m_fireDeliveryFingerprints;
    QUrl m_apiBaseUrl;
    QUrl m_authenticatedServerOrigin;
    QByteArray m_bearerToken;
    bool m_hasAuthenticatedServerOverride = false;
    bool m_authenticationExpired = false;
    QString m_slotsPath;
    QString m_slotDetailPath;
    QString m_sessionImagesPath;
    QString m_overstayThresholdPath;
    QString m_parkingRoiListPath;
    QString m_parkingRoiPathTemplate;
    QHash<QString, QString> m_pendingDetailRequests;
    QHash<QString, QString> m_pendingImageRequests;
    QHash<QString, PendingEventEvidenceRequest> m_pendingEventEvidenceRequests;
    QSet<QString> m_pendingParkingRoiTags;
    QHash<QString, ParkingRoi> m_pendingParkingRoiExpectedValues;
    int m_apiTimeoutMs = 5000;
    int m_reconnectIntervalMs = 5000;
    int m_maxReconnectIntervalMs = 60000;
    int m_currentReconnectDelayMs = 5000;
    bool m_allowInsecureHttp = false;
    bool m_snapshotRequestInFlight = false;
    enum class OverstayRequest { None, Fetch, Update };
    OverstayRequest m_overstayRequest = OverstayRequest::None;
    int m_pendingOverstaySeconds = -1;
    quint64 m_nextEventSequence = 1;
    quint64 m_nextEventEvidenceRequestSequence = 1;
    ApiDiagnosticState m_apiDiagnostic;
    SlotIdMapper m_slotIdMapper;
};

#endif
