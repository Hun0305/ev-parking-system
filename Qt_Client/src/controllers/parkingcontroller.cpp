#include "parkingcontroller.h"


#include "adapters/serverfireeventadapter.h"
#include "adapters/serverparkingeventadapter.h"
#include "api/apiclient.h"
#include "api/imageloader.h"
#include "api/parkingroi.h"
#include "api/parkingresponseparser.h"
#include "api/urlorigin.h"
#include "services/mqttserviceclient.h"

#include <QDateTime>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace {
constexpr int kSeenServerEventLimit = 256;
constexpr int kEventEvidenceReferenceLimit = 512;
constexpr int kPortableMqttClientIdMaxLength = 23;
constexpr int kGeneratedMqttClientIdSuffixLength = 12;
constexpr int kMinimumOverstayThresholdSeconds = 60;
constexpr int kMaximumOverstayThresholdSeconds = 86400;

bool isParkingRoiZoneId(const QString &slotId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:EV|P)-(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(slotId);
    return match.hasMatch() && match.captured(1).toInt() > 0;
}

QString parkingRoiTag(const QString &kind, quint64 generation,
                      const QString &slotId = QString(),
                      bool appliedImmediately = false)
{
    return QStringLiteral("parking-roi|%1|%2|%3|%4")
        .arg(kind).arg(generation).arg(slotId)
        .arg(appliedImmediately ? 1 : 0);
}

QString generatedMqttClientId(QString prefix)
{
    prefix = prefix.trimmed();
    prefix.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                   QStringLiteral("-"));
    if (prefix.isEmpty()) prefix = QStringLiteral("qt-client");

    const int maxPrefixLength = kPortableMqttClientIdMaxLength
        - 1 - kGeneratedMqttClientIdSuffixLength;
    prefix = prefix.left(maxPrefixLength);
    QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    suffix.remove(QLatin1Char('-'));
    return prefix + QLatin1Char('-')
        + suffix.left(kGeneratedMqttClientIdSuffixLength);
}

bool isEvSlotId(const QString &slotId)
{
    return slotId.startsWith(QStringLiteral("EV-"));
}

bool isGeneralSlotId(const QString &slotId)
{
    return slotId.startsWith(QStringLiteral("P-"));
}

bool isEvidenceSlotId(const QString &slotId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:EV|P)-[0-9]+$"));
    return pattern.match(slotId).hasMatch();
}

QString eventEvidenceRequestTag(quint64 sequence)
{
    return QStringLiteral("event-evidence|%1").arg(sequence);
}

QString normalizeCameraChannelId(const QString &rawChannelId)
{
    QString normalized = rawChannelId.trimmed().toUpper();
    if (!normalized.startsWith(QStringLiteral("CH"))) {
        return QString();
    }
    normalized.remove(0, 2);
    bool ok = false;
    const int channelNumber = normalized.toInt(&ok);
    return ok && channelNumber >= 1 && channelNumber <= 4
        ? QStringLiteral("CH%1").arg(channelNumber) : QString();
}

QString fireAckCommandKey(const QString &channelId, const QString &alarmId)
{
    return channelId + QLatin1Char('|') + alarmId;
}

QString fireDeliverySinkKey(const QString &topic, const bool retained)
{
    if (topic.startsWith(QStringLiteral("parking/fire/"))) {
        return QStringLiteral("RETAINED_STATE");
    }
    if (topic.startsWith(QStringLiteral("parking/v1/events/"))) {
        return QStringLiteral("LIFECYCLE_EVENT");
    }
    return retained ? QStringLiteral("RETAINED_STATE")
                    : QStringLiteral("MANUAL_OR_EVENT");
}

bool isFireStateOnlyDelivery(const QString &topic, const bool retained)
{
    return retained || topic.startsWith(QStringLiteral("parking/fire/"));
}

QByteArray fireStateFingerprint(const ServerFireEvent &event)
{
    QJsonObject identity;
    identity.insert(QStringLiteral("event_id"), event.eventId);
    identity.insert(QStringLiteral("alarm_id"), event.alarmId);
    identity.insert(QStringLiteral("action"), static_cast<int>(event.action));
    identity.insert(QStringLiteral("event_type"), event.eventType);
    identity.insert(QStringLiteral("channel_id"), event.channelId);
    identity.insert(QStringLiteral("source_id"), event.sourceId);
    identity.insert(QStringLiteral("alarm_kind"), event.alarmKind);
    identity.insert(QStringLiteral("alarm_state"), event.alarmState);
    identity.insert(QStringLiteral("ack_state"), event.ackState);
    identity.insert(QStringLiteral("active_present"), event.activePresent);
    identity.insert(QStringLiteral("active"), event.active);
    return QJsonDocument(identity).toJson(QJsonDocument::Compact);
}

bool isFireLifecycleEventType(const QString &rawEventType)
{
    const QString eventType = rawEventType.trimmed().toUpper();
    return eventType == QStringLiteral("FIRE_SUSPECTED")
        || eventType == QStringLiteral("FIRE_CLEARED")
        || eventType == QStringLiteral("FIRE_ALARM")
        || eventType == QStringLiteral("FIRE_EVENT")
        || eventType == QStringLiteral("FLAME_DETECTED")
        || eventType == QStringLiteral("FIRE_ACKNOWLEDGED")
        || eventType == QStringLiteral("FIRE_ALARM_ACK");
}

bool vehicleClassFromText(const QString &vehicleType,
                          VehicleClass &vehicleClass)
{
    const QString normalized = vehicleType.trimmed().toUpper();
    if (normalized == QStringLiteral("EV")
        || normalized == QStringLiteral("ELECTRIC")
        || normalized == QStringLiteral("PHEV")) {
        vehicleClass = VehicleClass::Electric;
        return true;
    }
    if (normalized == QStringLiteral("NON_EV")
        || normalized == QStringLiteral("GENERAL")
        || normalized == QStringLiteral("ICE")
        || normalized == QStringLiteral("GASOLINE")
        || normalized == QStringLiteral("DIESEL")) {
        vehicleClass = VehicleClass::General;
        return true;
    }
    return false;
}

SlotState stateFromVisual(const SlotVisualState &visual)
{
    switch (visual.alarm) {
    case SlotAlarmKind::NonEvViolation: return SlotState::NonEvAlert;
    case SlotAlarmKind::Overstay: return SlotState::OvertimeAlert;
    case SlotAlarmKind::SensorError: return SlotState::SensorError;
    case SlotAlarmKind::None: break;
    }
    return visual.occupancy == SlotOccupancy::Occupied
        ? SlotState::Occupied : SlotState::Vacant;
}

QString alarmTextFromKind(SlotAlarmKind alarm)
{
    switch (alarm) {
    case SlotAlarmKind::NonEvViolation: return QStringLiteral("NON_EV_ALERT");
    case SlotAlarmKind::Overstay: return QStringLiteral("OVERTIME_ALERT");
    case SlotAlarmKind::SensorError: return QStringLiteral("HALL_SENSOR_ERROR");
    case SlotAlarmKind::None: return QStringLiteral("NORMAL");
    }
    return QStringLiteral("NORMAL");
}

QString durationText(int seconds)
{
    const int totalMinutes = qMax(0, seconds) / 60;
    return QStringLiteral("%1:%2")
        .arg(totalMinutes / 60, 2, 10, QLatin1Char('0'))
        .arg(totalMinutes % 60, 2, 10, QLatin1Char('0'));
}

EventSeverity severityFromText(const QString &severity)
{
    const QString normalized = severity.trimmed().toUpper();
    if (normalized == QStringLiteral("CRITICAL")) return EventSeverity::Critical;
    if (normalized == QStringLiteral("WARNING")
        || normalized == QStringLiteral("WARN")) {
        return EventSeverity::Warning;
    }
    if (normalized == QStringLiteral("INFO")) return EventSeverity::Info;
    return EventSeverity::Unknown;
}

bool parseOverstayThreshold(const QJsonDocument &document,
                            bool requireSuccess,
                            int &seconds,
                            QString &applyPolicy,
                            QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("The server returned an invalid JSON object.");
        return false;
    }

    const QJsonObject object = document.object();
    const QJsonValue successValue = object.value(QStringLiteral("success"));
    if (requireSuccess
        && (!successValue.isBool() || !successValue.toBool())) {
        errorMessage = object.value(QStringLiteral("error")).toString().trimmed();
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("The server did not confirm the update.");
        }
        return false;
    }
    if (successValue.isBool() && !successValue.toBool()) {
        errorMessage = object.value(QStringLiteral("error")).toString().trimmed();
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("The server rejected the request.");
        }
        return false;
    }

    const QJsonValue thresholdValue = object.value(
        QStringLiteral("thresholdSeconds"));
    if (!thresholdValue.isDouble()) {
        errorMessage = QStringLiteral("The response is missing thresholdSeconds.");
        return false;
    }
    const double rawSeconds = thresholdValue.toDouble();
    seconds = thresholdValue.toInt(-1);
    if (rawSeconds != static_cast<double>(seconds)
        || seconds < kMinimumOverstayThresholdSeconds
        || seconds > kMaximumOverstayThresholdSeconds) {
        errorMessage = QStringLiteral(
            "The server returned an invalid thresholdSeconds value.");
        return false;
    }

    applyPolicy = object.value(QStringLiteral("applyPolicy"))
                      .toString().trimmed();
    errorMessage.clear();
    return true;
}

QByteArray apiSyncFingerprint(const ParkingViewState &state,
                              int receivedCount,
                              int appliedCount,
                              int rejectedCount,
                              int duplicateCount)
{
    QByteArray fingerprint;
    QDataStream stream(&fingerprint, QIODevice::WriteOnly);
    stream << receivedCount << appliedCount << rejectedCount << duplicateCount
           << state.serverSlotCount;

    QStringList slotIds = state.evSlots.keys();
    slotIds.append(state.parkingSlots.keys());
    std::sort(slotIds.begin(), slotIds.end());
    for (const QString &slotId : slotIds) {
        const bool evSlot = state.evSlots.contains(slotId);
        SlotState slotState = SlotState::Vacant;
        SlotVisualState visual;
        QDateTime occupiedSince;
        QString alarmText;
        bool isEv = false;
        if (evSlot) {
            const EvSlotInfo &slot = state.evSlots.value(slotId);
            slotState = slot.state;
            visual = slot.visual;
            occupiedSince = slot.occupiedSince;
            alarmText = slot.alarmText;
            isEv = slot.isEv;
        } else {
            const ParkingSlotInfo &slot = state.parkingSlots.value(slotId);
            slotState = slot.state;
            visual = slot.visual;
            occupiedSince = slot.occupiedSince;
        }

        stream << slotId << evSlot << static_cast<int>(slotState)
               << static_cast<int>(visual.occupancy)
               << static_cast<int>(visual.vehicleClass)
               << static_cast<int>(visual.alarm)
               << visual.alarmAcknowledged
               << static_cast<int>(visual.ocrStatus)
               << visual.correlationId
               << occupiedSince.toMSecsSinceEpoch()
               << alarmText << isEv
               << state.slotPlateNumbers.value(slotId)
               << state.slotSessionIds.value(slotId, -1);

        QStringList imageFingerprints;
        for (const ParkingImageResource &image :
             state.slotImages.value(slotId)) {
            imageFingerprints.append(QStringLiteral(
                "%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6\x1f%7\x1f%8\x1f%9")
                .arg(image.url.toString(QUrl::FullyEncoded),
                     image.timestamp.toString(Qt::ISODateWithMs),
                     image.role,
                     image.processing)
                .arg(image.imageId)
                .arg(image.sessionId)
                .arg(image.enhancementType,
                     image.ocrResult,
                     image.evidenceReason));
        }
        std::sort(imageFingerprints.begin(), imageFingerprints.end());
        stream << imageFingerprints;
    }
    return fingerprint;
}

}

ParkingController::ParkingController(const QString &sharedConfigPath,
                                     const QString &localConfigPath,
                                     QObject *parent)
    : QObject(parent)
    , m_sharedConfigPath(sharedConfigPath)
    , m_localConfigPath(localConfigPath)
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &ParkingController::reconnectNow);

    QString mapperError;
    const QDir configDir = QFileInfo(sharedConfigPath).absoluteDir();
    const QString localMappingPath =
        configDir.filePath(QStringLiteral("slot_mapping.local.json"));
    if (QFileInfo::exists(localMappingPath)) {
        if (!m_slotIdMapper.loadFromFile(localMappingPath, mapperError)) {
            qWarning().noquote()
                << QStringLiteral("Slot mapping disabled: %1").arg(mapperError);
        }
    }
}

void ParkingController::start()
{
    initializeApiClient();
    initializeMqttClient();
}

void ParkingController::initializeMqttClient()
{
    QSettings sharedSettings(m_sharedConfigPath, QSettings::IniFormat);
    QSettings localSettings(m_localConfigPath, QSettings::IniFormat);
    auto setting = [&](const QString &key, const QVariant &defaultValue) {
        return localSettings.contains(key) ? localSettings.value(key, defaultValue)
                                           : sharedSettings.value(key, defaultValue);
    };

    // Settings can be reloaded while the application is running. Tear down the
    // client first so a disabled or changed broker cannot keep retrying with a
    // stale host captured by the previous MqttSettings instance.
    if (m_mqttClient) {
        m_mqttClient->stop();
        disconnect(m_mqttClient, nullptr, this, nullptr);
        m_mqttClient->deleteLater();
        m_mqttClient = nullptr;
    }

    MqttSettings mqttSettings;
    mqttSettings.enabled = setting(QStringLiteral("mqtt/enabled"), false).toBool();
    if (!mqttSettings.enabled) {
        emit statusMessageChanged(QStringLiteral("MQTT disabled"));
        return;
    }

    mqttSettings.host = setting(QStringLiteral("mqtt/host"), QString()).toString().trimmed();
    const bool followApiHost =
        setting(QStringLiteral("mqtt/follow_api_host"), true).toBool();
    if (followApiHost) {
        const QUrl apiUrl = m_hasAuthenticatedServerOverride
            ? m_authenticatedServerOrigin
            : QUrl(setting(QStringLiteral("api/base_url"), QString())
                       .toString().trimmed());
        if (apiUrl.isValid() && !apiUrl.host().isEmpty()) {
            mqttSettings.host = apiUrl.host();
        }
    }
    mqttSettings.port =
        static_cast<quint16>(setting(QStringLiteral("mqtt/port"), 1883).toInt());
    const QString persistedClientId =
        localSettings.value(QStringLiteral("mqtt/client_id")).toString().trimmed();
    const bool needsGeneratedClientId = persistedClientId.isEmpty()
        || persistedClientId.compare(QStringLiteral("qt-client"),
                                     Qt::CaseInsensitive) == 0;
    if (needsGeneratedClientId) {
        const QString sharedPrefix =
            sharedSettings.value(QStringLiteral("mqtt/client_id"),
                                 QStringLiteral("qt-client")).toString();
        mqttSettings.clientId = generatedMqttClientId(sharedPrefix);
        localSettings.setValue(QStringLiteral("mqtt/client_id"),
                               mqttSettings.clientId);
        localSettings.sync();
        if (localSettings.status() == QSettings::NoError) {
            recordEvent(QStringLiteral("SYSTEM"),
                        QStringLiteral("MQTT_CLIENT_ID_INITIALIZED"),
                        QStringLiteral("Generated workstation MQTT client ID: ")
                            + mqttSettings.clientId,
                        QStringLiteral("DONE"));
        } else {
            recordEvent(QStringLiteral("SYSTEM"),
                        QStringLiteral("MQTT_CLIENT_ID_PERSIST_ERROR"),
                        QStringLiteral("Could not persist generated MQTT client ID"),
                        QStringLiteral("FAILED"));
        }
    } else {
        mqttSettings.clientId = persistedClientId;
    }
    mqttSettings.reconnectIntervalMs =
        setting(QStringLiteral("mqtt/reconnect_interval_ms"), 5000).toInt();
    const QStringList defaultTopics{
        QStringLiteral("parking/fire/+"),
        QStringLiteral("parking/v1/events/+"),
        QStringLiteral("parking/v1/state/+")};
    const QString topicsKey = QStringLiteral("mqtt/topics");
    const bool hasLocalTopics = localSettings.contains(topicsKey);
    mqttSettings.topics = MqttSettings::normalizedTopics(
        hasLocalTopics ? localSettings.value(topicsKey)
                       : sharedSettings.value(topicsKey));
    if (mqttSettings.topics.isEmpty() && hasLocalTopics) {
        mqttSettings.topics = MqttSettings::normalizedTopics(
            sharedSettings.value(topicsKey));
    }
    if (mqttSettings.topics.isEmpty()) {
        mqttSettings.topics = defaultTopics;
    }

    m_mqttClient = new MqttServiceClient(mqttSettings, this);
    connect(m_mqttClient, &MqttServiceClient::messageReceived,
            this, &ParkingController::handleMqttMessageWithMetadata);
    connect(m_mqttClient, &MqttServiceClient::connectionChanged, this,
            [this](const QString &status, bool connected) {
                emit statusMessageChanged(status);
                recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("MQTT"),
                            status,
                            connected ? QStringLiteral("DONE")
                                      : QStringLiteral("FAILED"));
                if (connected && m_state.apiEnabled
                    && !m_snapshotRequestInFlight) {
                    reconnectNow();
                }
            });
    connect(m_mqttClient, &MqttServiceClient::publishFailed, this,
            [this](const QString &topic, const QString &reason) {
                if (!topic.startsWith(
                        QStringLiteral("parking/v1/commands/fire/"))) return;
                const QString channelId = normalizeCameraChannelId(
                    topic.section(QLatin1Char('/'), -1));
                const ChannelFireAlarmState alarm =
                    m_state.fireAlarms.value(channelId);
                const QString commandKey = fireAckCommandKey(
                    channelId, alarm.alarmId);
                const bool retryable = alarm.active && !alarm.acknowledged
                    && !alarm.alarmId.isEmpty()
                    && m_fireAckCommandKeys.remove(commandKey) > 0;
                recordEvent(channelId.isEmpty()
                                ? QStringLiteral("SYSTEM") : channelId,
                            QStringLiteral("FIRE_ACK_PUBLISH_FAILED"),
                            reason, QStringLiteral("FAILED"));
                if (retryable) {
                    emit fireConfirmationRetryRequested(
                        channelId, alarm.alarmId);
                }
            });
    connect(m_mqttClient, &MqttServiceClient::publishConfirmed, this,
            [this](const QString &topic) {
                if (!topic.startsWith(
                        QStringLiteral("parking/v1/commands/fire/"))) return;
                recordEvent(topic.section(QLatin1Char('/'), -1),
                            QStringLiteral("FIRE_ACK_COMMAND_DELIVERED"),
                            QStringLiteral("Broker confirmed ALARM_ACK command; waiting for Pi response"),
                            QStringLiteral("DONE"));
            });
    m_mqttClient->start();
}

void ParkingController::handleMqttMessage(const QString &topic,
                                          const QByteArray &payload)
{
    handleMqttMessageWithMetadata(topic, payload, false);
}

void ParkingController::handleMqttMessageWithMetadata(
    const QString &topic, const QByteArray &payload, const bool retained)
{
    emit statusMessageChanged(QStringLiteral("MQTT RX: ") + topic);

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("MQTT_PARSE_ERROR"),
                    topic + QStringLiteral(": ") + parseError.errorString(),
                    QStringLiteral("REJECTED"));
        return;
    }

    const QJsonObject event = document.object();
    const QString eventType = event.value(QStringLiteral("event_type")).toString();
    const bool fireStateTopic =
        topic.startsWith(QStringLiteral("parking/fire/"));
    const bool integratedEventTopic =
        topic.startsWith(QStringLiteral("parking/v1/events/"));
    const bool parkingStateTopic =
        topic.startsWith(QStringLiteral("parking/v1/state/"));

    const ServerFireEvent fireEvent = ServerFireEventAdapter::parse(event);
    if (fireEvent.action == ServerFireEventAction::Invalid) {
        recordEvent(fireEvent.channelId.isEmpty()
                        ? QStringLiteral("SYSTEM") : fireEvent.channelId,
                    QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                    topic + QStringLiteral(": ") + fireEvent.errorMessage,
                    QStringLiteral("REJECTED"));
        return;
    }
    if (fireEvent.action == ServerFireEventAction::Activate
        || fireEvent.action == ServerFireEventAction::Acknowledge
        || fireEvent.action == ServerFireEventAction::Clear) {
        if (fireStateTopic || integratedEventTopic) {
            const QString topicChannelId = normalizeCameraChannelId(
                topic.section(QLatin1Char('/'), -1));
            if (topicChannelId.isEmpty()
                || topicChannelId != fireEvent.channelId) {
                recordEvent(
                    fireEvent.channelId,
                    QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                    QStringLiteral("Fire topic channel does not match channel_id"),
                    QStringLiteral("REJECTED"));
                return;
            }
        }
        if (parkingStateTopic) {
            recordEvent(fireEvent.channelId,
                        QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                        QStringLiteral("Fire event is not allowed on parking/v1/state"),
                        QStringLiteral("REJECTED"));
            return;
        }
        if (!fireStateTopic && !integratedEventTopic) {
            recordEvent(fireEvent.channelId,
                        QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                        QStringLiteral("Fire event received on an unsupported topic"),
                        QStringLiteral("REJECTED"));
            return;
        }
        applyChannelFireEvent(fireEvent, topic, retained);
        return;
    }

    if (fireStateTopic) {
        recordEvent(QStringLiteral("SYSTEM"),
                    QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                    topic + QStringLiteral(": payload is not a fire event"),
                    QStringLiteral("REJECTED"));
        return;
    }

    if (applyServerParkingEvent(event, topic)) {
        return;
    }

    recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("MQTT_UNSUPPORTED"),
                topic + QStringLiteral(": ") + eventType,
                QStringLiteral("REJECTED"));
}

ParkingController::FirePreflightResult ParkingController::preflightFireEvent(
    const ServerFireEvent &event, const QString &topic, const bool retained)
{
    FirePreflightResult result;
    const bool stateOnly = isFireStateOnlyDelivery(topic, retained);
    auto existingLedger = m_fireRevisionLedgers.constFind(event.channelId);

    if (!event.fireRevisionPresent) {
        if (existingLedger != m_fireRevisionLedgers.cend()
            && existingLedger->versionedSeen) {
            result.errorMessage = QStringLiteral(
                "revisionless fire payload rejected after v2 activation");
            return result;
        }

        result.accepted = true;
        result.applyState = true;
        result.recordHistory = !stateOnly;
        return result;
    }

    result.revisioned = true;
    const QByteArray stateIdentity = fireStateFingerprint(event);
    const QByteArray eventIdentity = QByteArray::number(event.fireRevision)
        + QByteArray(1, '\x1f') + stateIdentity;
    const QString sinkKey = fireDeliverySinkKey(topic, retained);
    const QString deliveryKey = sinkKey + QChar(0x1f) + event.deliveryId
        + QChar(0x1f) + QString::number(event.fireRevision);
    const QByteArray deliveryIdentity = event.channelId.toUtf8()
        + QByteArray(1, '\x1f') + QByteArray::number(event.fireRevision)
        + QByteArray(1, '\x1f') + event.canonicalPayload;
    const QByteArray registeredGlobalDelivery =
        m_fireDeliveryFingerprints.value(deliveryKey);
    if (!registeredGlobalDelivery.isEmpty()
        && registeredGlobalDelivery != deliveryIdentity) {
        result.errorMessage = QStringLiteral(
            "same fire sink/delivery_id/revision carries a different payload");
        return result;
    }

    if (existingLedger != m_fireRevisionLedgers.cend()
        && existingLedger->versionedSeen) {
        const QByteArray registeredEvent =
            existingLedger->eventFingerprintById.value(event.eventId);
        if (!registeredEvent.isEmpty() && registeredEvent != eventIdentity) {
            result.errorMessage = QStringLiteral(
                "same fire event_id carries a different revision or payload");
            return result;
        }

        if (event.fireRevision < existingLedger->lastAppliedRevision) {
            result.accepted = true;
            return result;
        }

        if (event.fireRevision == existingLedger->lastAppliedRevision) {
            if (existingLedger->stateFingerprint != stateIdentity) {
                result.errorMessage = QStringLiteral(
                    "same fire_revision carries a different lifecycle identity");
                return result;
            }

            const QString registeredDeliveryId =
                existingLedger->deliveryIdBySink.value(sinkKey);
            if (!registeredDeliveryId.isEmpty()
                && registeredDeliveryId != event.deliveryId) {
                result.errorMessage = QStringLiteral(
                    "same fire sink/revision carries a different delivery_id");
                return result;
            }

            FireRevisionLedger &ledger = m_fireRevisionLedgers[event.channelId];
            if (registeredDeliveryId.isEmpty()) {
                ledger.deliveryIdBySink.insert(sinkKey, event.deliveryId);
            }
            if (registeredGlobalDelivery.isEmpty()) {
                m_fireDeliveryFingerprints.insert(
                    deliveryKey, deliveryIdentity);
            }
            result.accepted = true;
            result.recordHistory = !stateOnly
                && !ledger.lifecycleHistoryRecorded;
            if (result.recordHistory) {
                ledger.lifecycleHistoryRecorded = true;
            }
            return result;
        }
    }

    FireRevisionLedger nextLedger;
    if (existingLedger != m_fireRevisionLedgers.cend()) {
        nextLedger.eventFingerprintById = existingLedger->eventFingerprintById;
    }
    nextLedger.versionedSeen = true;
    nextLedger.lastAppliedRevision = event.fireRevision;
    nextLedger.stateFingerprint = stateIdentity;
    nextLedger.eventFingerprintById.insert(event.eventId, eventIdentity);
    nextLedger.deliveryIdBySink.insert(sinkKey, event.deliveryId);
    nextLedger.lifecycleHistoryRecorded = !stateOnly;
    m_fireRevisionLedgers.insert(event.channelId, nextLedger);
    if (registeredGlobalDelivery.isEmpty()) {
        m_fireDeliveryFingerprints.insert(deliveryKey, deliveryIdentity);
    }

    result.accepted = true;
    result.applyState = true;
    result.recordHistory = !stateOnly;
    return result;
}

void ParkingController::applyChannelFireEvent(const ServerFireEvent &event,
                                              const QString &topic,
                                              const bool retained)
{
    const FirePreflightResult preflight =
        preflightFireEvent(event, topic, retained);
    if (!preflight.accepted) {
        recordEvent(
            event.channelId.isEmpty() ? QStringLiteral("SYSTEM")
                                      : event.channelId,
            QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
            topic + QStringLiteral(": ") + preflight.errorMessage,
            QStringLiteral("REJECTED"));
        return;
    }
    if (!preflight.applyState) {
        if (preflight.recordHistory) {
            recordChannelFireEvent(event);
        }
        return;
    }

    bool stateChanged = false;
    bool requestConfirmation = false;
    if (event.action == ServerFireEventAction::Activate) {
        if (!m_state.fireChannels.contains(event.channelId)) {
            m_state.fireChannels.insert(event.channelId);
            stateChanged = true;
        }
        const ChannelFireAlarmState previous =
            m_state.fireAlarms.value(event.channelId);
        if (previous.active && previous.alarmId != event.alarmId) {
            m_fireAckCommandKeys.remove(
                fireAckCommandKey(event.channelId, previous.alarmId));
        }
        requestConfirmation = !previous.active
            || previous.alarmId != event.alarmId
            || (preflight.revisioned && previous.acknowledged);
        ChannelFireAlarmState updated;
        updated.alarmId = event.alarmId;
        updated.alarmState = event.alarmState.isEmpty()
            ? QStringLiteral("OPEN") : event.alarmState;
        updated.ackState = event.ackState.isEmpty()
            ? QStringLiteral("unacked") : event.ackState;
        updated.active = true;

        // A repeated DETECTED for the same active alarm must never undo ACK.
        if (!preflight.revisioned
            && previous.active && previous.acknowledged
            && (event.alarmId.isEmpty()
                || previous.alarmId == event.alarmId)) {
            updated = previous;
        }
        if (previous.alarmId != updated.alarmId
            || previous.alarmState != updated.alarmState
            || previous.ackState != updated.ackState
            || previous.active != updated.active
            || previous.acknowledged != updated.acknowledged) {
            m_state.fireAlarms.insert(event.channelId, updated);
            stateChanged = true;
        }
    } else if (event.action == ServerFireEventAction::Acknowledge) {
        const ChannelFireAlarmState previous =
            m_state.fireAlarms.value(event.channelId);
        if (!preflight.revisioned
            && ((!retained && !previous.active)
                || (previous.active
                    && previous.alarmId != event.alarmId))) {
            recordEvent(
                event.channelId,
                QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                QStringLiteral("Rejected ACK for inactive or mismatched alarm_id"),
                QStringLiteral("REJECTED"));
            return;
        }

        ChannelFireAlarmState updated = previous;
        updated.alarmId = event.alarmId;
        updated.alarmState = QStringLiteral("ACKNOWLEDGED");
        updated.ackState = QStringLiteral("acknowledged");
        updated.active = true;
        updated.acknowledged = true;
        if (!m_state.fireChannels.contains(event.channelId)) {
            m_state.fireChannels.insert(event.channelId);
            stateChanged = true;
        }
        if (previous.alarmId != updated.alarmId
            || previous.alarmState != updated.alarmState
            || previous.ackState != updated.ackState
            || previous.active != updated.active
            || previous.acknowledged != updated.acknowledged) {
            m_state.fireAlarms.insert(event.channelId, updated);
            stateChanged = true;
        }
        m_fireAckCommandKeys.remove(
            fireAckCommandKey(event.channelId, event.alarmId));
        emit fireConfirmationClosed(event.channelId, QString());
    } else if (event.action == ServerFireEventAction::Clear) {
        const ChannelFireAlarmState previous =
            m_state.fireAlarms.value(event.channelId);
        if (!preflight.revisioned
            && previous.active && previous.alarmId != event.alarmId) {
            recordEvent(
                event.channelId,
                QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                QStringLiteral("Rejected clear for mismatched alarm_id"),
                QStringLiteral("REJECTED"));
            return;
        }
        if (!previous.alarmId.isEmpty()) {
            m_fireAckCommandKeys.remove(
                fireAckCommandKey(event.channelId, previous.alarmId));
        }
        m_fireAckCommandKeys.remove(
            fireAckCommandKey(event.channelId, event.alarmId));
        if (m_state.fireChannels.remove(event.channelId) > 0) {
            stateChanged = true;
        }
        if (m_state.fireAlarms.remove(event.channelId) > 0) {
            stateChanged = true;
        }
        emit fireConfirmationClosed(event.channelId, QString());
    }

    if (stateChanged) {
        notifyStateChanged();
    }
    if (requestConfirmation) {
        emit fireConfirmationRequested(event.channelId, event.alarmId);
    }

    if (preflight.recordHistory) {
        recordChannelFireEvent(event);
    }
}

void ParkingController::recordChannelFireEvent(const ServerFireEvent &event)
{
    const QString effectiveEventId = event.eventId.isEmpty()
        ? QStringLiteral("%1|%2|%3|%4")
              .arg(event.channelId, event.alarmId,
                   event.eventType, event.alarmState)
        : event.eventId;
    if (!rememberServerEventId(effectiveEventId)) {
        return;
    }

    MonitoringEvent monitoringEvent;
    monitoringEvent.id = effectiveEventId;
    monitoringEvent.occurredAt = event.occurredAt.isValid()
        ? event.occurredAt : QDateTime::currentDateTime();
    monitoringEvent.sourceId = event.channelId;
    if (event.action == ServerFireEventAction::Activate) {
        monitoringEvent.eventType = QStringLiteral("FIRE_SUSPECTED");
        monitoringEvent.status = QStringLiteral("OPEN");
    } else if (event.action == ServerFireEventAction::Acknowledge) {
        monitoringEvent.eventType = QStringLiteral("FIRE_ACKNOWLEDGED");
        monitoringEvent.status = QStringLiteral("ACKNOWLEDGED");
    } else {
        monitoringEvent.eventType = QStringLiteral("FIRE_CLEARED");
        monitoringEvent.status = QStringLiteral("CLEARED");
    }
    monitoringEvent.ackState = eventAckStateFromStatus(monitoringEvent.status);
    monitoringEvent.severity = severityFromText(event.severity);
    monitoringEvent.message = event.message.isEmpty()
        ? (event.action == ServerFireEventAction::Activate
               ? QStringLiteral("Fire suspected on %1; operator verification required")
                     .arg(event.channelId)
               : event.action == ServerFireEventAction::Acknowledge
                   ? QStringLiteral("Fire alarm acknowledged on %1").arg(event.channelId)
                   : QStringLiteral("Fire state cleared on %1").arg(event.channelId))
        : event.message;
    emit eventLogged(monitoringEvent);
}

void ParkingController::acknowledgeFireAlarm(const QString &rawChannelId)
{
    const QString channelId = normalizeCameraChannelId(rawChannelId);
    const ChannelFireAlarmState alarm = m_state.fireAlarms.value(channelId);
    if (channelId.isEmpty() || !alarm.active || alarm.acknowledged
        || alarm.alarmId.isEmpty()) {
        recordEvent(channelId.isEmpty() ? QStringLiteral("SYSTEM") : channelId,
                    QStringLiteral("FIRE_ACK_REQUEST_REJECTED"),
                    QStringLiteral("No unacknowledged active fire alarm with alarm_id"),
                    QStringLiteral("REJECTED"));
        return;
    }

    const QString commandKey = fireAckCommandKey(channelId, alarm.alarmId);
    if (m_fireAckCommandKeys.contains(commandKey)) {
        recordEvent(channelId, QStringLiteral("FIRE_ACK_DUPLICATE_SUPPRESSED"),
                    QStringLiteral("ALARM_ACK was already published for this alarm_id"),
                    QStringLiteral("SKIPPED"));
        return;
    }
    const QString commandChannel = QStringLiteral("ch0")
        + channelId.mid(2);
    const QString topic = QStringLiteral("parking/v1/commands/fire/")
        + commandChannel;
    QJsonObject command;
    command.insert(QStringLiteral("command"), QStringLiteral("ALARM_ACK"));
    command.insert(QStringLiteral("channel_id"), commandChannel);
    command.insert(QStringLiteral("alarm_id"), alarm.alarmId);
    const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    emit fireAcknowledgementCommandPrepared(topic, payload);

    if (!m_mqttClient) {
        recordEvent(channelId, QStringLiteral("FIRE_ACK_PUBLISH_FAILED"),
                    QStringLiteral("MQTT is not ready; fire alarm remains unacknowledged"),
                    QStringLiteral("FAILED"));
        emit fireConfirmationRetryRequested(channelId, alarm.alarmId);
        return;
    }

    m_fireAckCommandKeys.insert(commandKey);
    if (!m_mqttClient->publish(topic, payload)) {
        m_fireAckCommandKeys.remove(commandKey);
        return;
    }
    recordEvent(channelId, QStringLiteral("FIRE_ACK_REQUESTED"),
                QStringLiteral("Waiting for Pi FIRE_ACKNOWLEDGED response"),
                QStringLiteral("PENDING"));
}

bool ParkingController::applyServerParkingEvent(const QJsonObject &object,
                                                const QString &topic)
{
    ServerParkingEvent event;
    QString error;
    if (!ServerParkingEventAdapter::parse(object, event, error)) {
        recordEvent(QStringLiteral("SYSTEM"),
                    QStringLiteral("MQTT_CONTRACT_ERROR"),
                    topic + QStringLiteral(": ") + error,
                    QStringLiteral("REJECTED"));
        return true;
    }

    const QString mappedSlotId = m_slotIdMapper.toZoneId(event.slotId);
    if (mappedSlotId.isEmpty()) {
        if (event.eventType == QStringLiteral("SENSOR_ERROR")
            || event.eventType == QStringLiteral("SENSOR_RECOVERED")) {
            recordServerEvent(event, QStringLiteral("SYSTEM"));
            return true;
        }
        return false;
    }
    const QString slotId = normalizeParkingSlotId(mappedSlotId);
    if (!isEvSlotId(slotId) && !isGeneralSlotId(slotId)) {
        return false;
    }

    const QString effectiveAlarm =
        ServerParkingEventAdapter::effectiveAlarmKind(event);
    const QString monitoringStatus =
        ServerParkingEventAdapter::monitoringStatus(event);
    SlotAlarmKind incomingAlarm = slotAlarmKindFromText(effectiveAlarm);
    const bool alarmOpen = monitoringStatus == QStringLiteral("OPEN")
        && incomingAlarm != SlotAlarmKind::None;
    const bool occupied = event.parkingState == QStringLiteral("OCCUPIED");
    const bool vacant = event.parkingState == QStringLiteral("VACANT");
    const bool stateTopic =
        topic.startsWith(QStringLiteral("parking/v1/state/"));
    const bool alarmCleared = monitoringStatus == QStringLiteral("CLEARED")
        || vacant
        || (stateTopic && event.alarmState == QStringLiteral("NONE")
            && !alarmOpen);
    
    const bool ocrRequested =
        event.ocrStatus == QStringLiteral("PENDING")
        || event.eventType == QStringLiteral("OCR_REQUESTED");
    const bool ocrCompleted =
        event.ocrStatus == QStringLiteral("RECOGNIZED")
        || event.ocrStatus == QStringLiteral("COMPLETED")
        || event.eventType == QStringLiteral("OCR_COMPLETED")
        || event.eventType == QStringLiteral("VEHICLE_CLASSIFIED");
    const bool ocrUnrecognized =
        event.ocrStatus == QStringLiteral("FAILED")
        || event.ocrStatus == QStringLiteral("UNRECOGNIZED")
        || event.eventType == QStringLiteral("OCR_UNRECOGNIZED");

    const bool hasStateMeaning = occupied || vacant || alarmOpen
        || monitoringStatus == QStringLiteral("CLEARED")
        || ocrRequested || ocrCompleted || ocrUnrecognized;
    if (!hasStateMeaning) {
        return false;
    }

    if (vacant) {
        m_state.slotImages.remove(slotId);
        m_state.slotSessionIds.remove(slotId);
    } else if (event.sessionId > 0) {
        m_state.slotSessionIds.insert(slotId, event.sessionId);
    }

    VehicleClass incomingVehicleClass = VehicleClass::Unknown;
    const bool vehicleClassKnown =
        vehicleClassFromText(event.vehicleType, incomingVehicleClass);

    if (!occupied && !vacant
        && monitoringStatus == QStringLiteral("CLEARED")) {
        bool removeUnknownState = false;
        if (isEvSlotId(slotId)) {
            removeUnknownState = !m_state.evSlots.contains(slotId)
                || m_state.evSlots.value(slotId).visual.occupancy
                    == SlotOccupancy::Unknown;
            if (removeUnknownState) {
                m_state.evSlots.remove(slotId);
                m_state.slotPlateNumbers.remove(slotId);
                m_state.slotImages.remove(slotId);
                m_state.slotSessionIds.remove(slotId);
            }
        } else {
            removeUnknownState = !m_state.parkingSlots.contains(slotId)
                || m_state.parkingSlots.value(slotId).visual.occupancy
                    == SlotOccupancy::Unknown;
            if (removeUnknownState) {
                m_state.parkingSlots.remove(slotId);
                m_state.slotPlateNumbers.remove(slotId);
                m_state.slotImages.remove(slotId);
                m_state.slotSessionIds.remove(slotId);
            }
        }
        if (removeUnknownState) {
            notifyStateChanged();
            recordServerEvent(event, slotId);
            return true;
        }
    }

    if (isEvSlotId(slotId)) {
        EvSlotInfo slot = m_state.evSlots.value(slotId);
        slot.slotId = slotId;
        if (slot.plateNumber.isEmpty()) slot.plateNumber = QStringLiteral("-");
        if (slot.occupiedTime.isEmpty()) slot.occupiedTime = QStringLiteral("-");

        SlotVisualState visual = slot.visual;

        if (occupied) {
            visual.occupancy = SlotOccupancy::Occupied;
            slot.occupiedTime = durationText(event.occupiedSeconds);
            if (!slot.occupiedSince.isValid() && event.occurredAt.isValid()) {
                slot.occupiedSince = event.occurredAt;
            }
        } else if (vacant) {
            visual.occupancy = SlotOccupancy::Vacant;
            visual.vehicleClass = VehicleClass::Unknown;
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
            visual.ocrStatus = OcrStatus::None;
            slot.plateNumber = QStringLiteral("-");
            slot.occupiedTime = QStringLiteral("-");
            slot.occupiedSince = QDateTime();
            slot.correlationId.clear();
        }

        if (ocrRequested) {
            visual.ocrStatus = OcrStatus::Requested;
            if (!event.correlationId.isEmpty()) {
                slot.correlationId = event.correlationId;
            }
        } else if (ocrCompleted) {
            visual.ocrStatus = OcrStatus::Completed;
        } else if (ocrUnrecognized) {
            visual.ocrStatus = OcrStatus::Unrecognized;
        }

        if (!event.plateNumber.isEmpty()) {
            slot.plateNumber = event.plateNumber;
        }
        if (vehicleClassKnown) {
            visual.vehicleClass = incomingVehicleClass;
            slot.isEv = incomingVehicleClass == VehicleClass::Electric;
        } else if (vacant) {
            slot.isEv = false;
        }

        if (alarmOpen) {
            visual.alarm = incomingAlarm;
            visual.alarmAcknowledged = false;
        } else if (alarmCleared) {
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
        }

        slot.state = stateFromVisual(visual);
        slot.alarmText = alarmTextFromKind(visual.alarm);
        visual.correlationId = slot.correlationId;
        slot.visual = visual;
        slot.lastUpdatedAt = event.occurredAt.isValid()
            ? event.occurredAt : QDateTime::currentDateTime();
        slot.eventId = event.eventId;
        m_state.evSlots.insert(slotId, slot);
        m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
        if (vacant) m_state.slotPlateNumbers.remove(slotId);
    } else {
        ParkingSlotInfo slot = m_state.parkingSlots.value(slotId);
        slot.slotId = slotId;
        SlotVisualState visual = slot.visual;

        if (occupied) {
            visual.occupancy = SlotOccupancy::Occupied;
            visual.vehicleClass = VehicleClass::General;
            if (!slot.occupiedSince.isValid() && event.occurredAt.isValid()) {
                slot.occupiedSince = event.occurredAt;
            }
            slot.occupiedTime = durationText(event.occupiedSeconds);
        } else if (vacant) {
            visual.occupancy = SlotOccupancy::Vacant;
            visual.vehicleClass = VehicleClass::Unknown;
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
            visual.ocrStatus = OcrStatus::None;
            slot.correlationId.clear();
            slot.occupiedTime = QStringLiteral("-");
            slot.occupiedSince = QDateTime();
        }
        
        if (ocrRequested) {
            visual.ocrStatus = OcrStatus::Requested;
            if (!event.correlationId.isEmpty()) {
                slot.correlationId = event.correlationId;
            }
        } else if (ocrCompleted) {
            visual.ocrStatus = OcrStatus::Completed;
        } else if (ocrUnrecognized) {
            visual.ocrStatus = OcrStatus::Unrecognized;
        }
        if (alarmOpen) {
            visual.alarm = incomingAlarm;
            visual.alarmAcknowledged = false;
        } else if (alarmCleared) {
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
        }

        slot.state = stateFromVisual(visual);
        visual.correlationId = slot.correlationId;
        slot.visual = visual;
        slot.lastUpdatedAt = event.occurredAt.isValid()
            ? event.occurredAt : QDateTime::currentDateTime();
        slot.eventId = event.eventId;
        m_state.parkingSlots.insert(slotId, slot);
        if (vacant) {
            m_state.slotPlateNumbers.remove(slotId);
        } else if (!event.plateNumber.trimmed().isEmpty()) {
            m_state.slotPlateNumbers.insert(slotId,
                                            event.plateNumber.trimmed());
        }
    }

    notifyStateChanged();
    recordServerEvent(event, slotId);
    return true;
}

void ParkingController::recordServerEvent(const ServerParkingEvent &event,
                                          const QString &slotId)
{
    if (!rememberServerEventId(event.eventId)) {
        return;
    }

    MonitoringEvent monitoringEvent;
    monitoringEvent.id = event.eventId.isEmpty()
        ? QStringLiteral("evt-%1").arg(m_nextEventSequence++)
        : event.eventId;
    monitoringEvent.occurredAt = event.occurredAt.isValid()
        ? event.occurredAt : QDateTime::currentDateTime();
    monitoringEvent.sourceId = slotId;
    monitoringEvent.evidenceSlotId = isEvidenceSlotId(slotId)
        ? slotId : QString();
    monitoringEvent.parkingSessionId = event.sessionId;
    monitoringEvent.eventType =
        ServerParkingEventAdapter::monitoringEventType(event);
    monitoringEvent.status =
        ServerParkingEventAdapter::monitoringStatus(event);
    monitoringEvent.ackState = eventAckStateFromStatus(monitoringEvent.status);
    monitoringEvent.severity = severityFromText(event.severity);
    monitoringEvent.message = !event.message.isEmpty()
        ? event.message
        : (!event.evidencePath.isEmpty()
               ? event.evidencePath
               : QStringLiteral("Pi server event %1")
                     .arg(event.eventType));
    rememberEventEvidence(monitoringEvent, event.plateNumber);
    emit eventLogged(monitoringEvent);
}

bool ParkingController::rememberServerEventId(const QString &eventId)
{
    if (eventId.isEmpty()) {
        return true;
    }
    if (m_seenServerEventIds.contains(eventId)) {
        return false;
    }
    m_seenServerEventIds.insert(eventId);
    m_seenServerEventOrder.enqueue(eventId);
    while (m_seenServerEventOrder.size() > kSeenServerEventLimit) {
        m_seenServerEventIds.remove(m_seenServerEventOrder.dequeue());
    }
    return true;
}

void ParkingController::rememberEventEvidence(
    const MonitoringEvent &event,
    const QString &plateNumber)
{
    const QString eventId = event.id.trimmed();
    const QString slotId = resolveParkingZoneId(event.evidenceSlotId);
    if (eventId.isEmpty() || !isEvidenceSlotId(slotId)) {
        return;
    }

    EventEvidenceReference reference;
    reference.eventId = eventId;
    reference.slotId = slotId;
    reference.sessionId = event.parkingSessionId;
    reference.state = slotState(slotId);
    reference.plateNumber = plateNumber.trimmed();
    if (reference.plateNumber.isEmpty()) {
        reference.plateNumber = this->plateNumber(slotId);
    }

    if (!m_eventEvidenceReferences.contains(eventId)) {
        m_eventEvidenceOrder.enqueue(eventId);
    }
    m_eventEvidenceReferences.insert(eventId, reference);
    while (m_eventEvidenceOrder.size() > kEventEvidenceReferenceLimit) {
        m_eventEvidenceReferences.remove(m_eventEvidenceOrder.dequeue());
    }
}

ParkingController::EventEvidenceReference
ParkingController::eventEvidenceReference(const QString &eventId) const
{
    return m_eventEvidenceReferences.value(eventId.trimmed());
}

bool ParkingController::hasEventEvidence(const QString &eventId) const
{
    return m_eventEvidenceReferences.contains(eventId.trimmed());
}

QString ParkingController::eventEvidenceSlotId(const QString &eventId) const
{
    return eventEvidenceReference(eventId).slotId;
}

QString ParkingController::resolveParkingZoneId(const QString &sourceId) const
{
    const QString mapped = m_slotIdMapper.toZoneId(sourceId);
    const QString slotId = normalizeParkingSlotId(
        mapped.isEmpty() ? sourceId : mapped);
    return isEvidenceSlotId(slotId) ? slotId : QString();
}

void ParkingController::requestEventEvidence(const QString &rawEventId)
{
    const QString eventId = rawEventId.trimmed();
    const EventEvidenceReference reference = eventEvidenceReference(eventId);
    if (reference.eventId.isEmpty()) {
        emit eventEvidenceFailed(
            eventId, QString(),
            QStringLiteral("This event is not linked to a parking session."));
        return;
    }

    if (!m_apiClient) {
        QList<ParkingImageResource> cachedImages = m_state.slotImages.value(
            reference.slotId);
        if (reference.sessionId > 0) {
            cachedImages.erase(
                std::remove_if(
                    cachedImages.begin(), cachedImages.end(),
                    [&reference](const ParkingImageResource &image) {
                        return image.sessionId > 0
                            && image.sessionId != reference.sessionId;
                    }),
                cachedImages.end());
        }
        emitEventEvidenceReady(reference, cachedImages);
        return;
    }

    if (reference.sessionId > 0) {
        requestEventEvidenceSession(reference);
    } else {
        requestEventEvidenceSlotDetail(reference);
    }
}

void ParkingController::requestEventEvidenceSlotDetail(
    const EventEvidenceReference &reference)
{
    QString serverSlotId = m_slotIdMapper.toServerSlotId(reference.slotId);
    if (serverSlotId.isEmpty()) {
        serverSlotId = reference.slotId;
    }
    if (serverSlotId.startsWith(QStringLiteral("EV-"))
        || serverSlotId.startsWith(QStringLiteral("P-"))) {
        serverSlotId.remove(QLatin1Char('-'));
    }
    if (serverSlotId.isEmpty()) {
        emit eventEvidenceFailed(
            reference.eventId, reference.slotId,
            QStringLiteral("No server slot mapping is configured."));
        return;
    }

    QString path = m_slotDetailPath;
    path.replace(QStringLiteral("{slot_id}"), serverSlotId);
    const QString requestTag = eventEvidenceRequestTag(
        m_nextEventEvidenceRequestSequence++);
    m_pendingEventEvidenceRequests.insert(
        requestTag,
        {reference, EventEvidenceRequestKind::SlotDetail});
    emit bannerChanged(
        QStringLiteral("Resolving event %1 parking session...")
            .arg(reference.eventId),
        false);
    m_apiClient->getJsonTagged(path, requestTag);
}

void ParkingController::requestEventEvidenceSession(
    const EventEvidenceReference &reference)
{
    if (reference.sessionId <= 0 || !m_apiClient) {
        emit eventEvidenceFailed(
            reference.eventId, reference.slotId,
            QStringLiteral("The event does not provide a parking session ID."));
        return;
    }

    QString path = m_sessionImagesPath;
    path.replace(QStringLiteral("{session_id}"),
                 QString::number(reference.sessionId));
    const QString requestTag = eventEvidenceRequestTag(
        m_nextEventEvidenceRequestSequence++);
    m_pendingEventEvidenceRequests.insert(
        requestTag,
        {reference, EventEvidenceRequestKind::SessionImages});
    emit bannerChanged(
        QStringLiteral("Loading event %1 evidence images...")
            .arg(reference.eventId),
        false);
    m_apiClient->getJsonTagged(path, requestTag);
}

void ParkingController::applyEventEvidenceResponse(
    const QString &requestTag,
    const QJsonDocument &document)
{
    if (!m_pendingEventEvidenceRequests.contains(requestTag)) {
        return;
    }
    PendingEventEvidenceRequest pending =
        m_pendingEventEvidenceRequests.take(requestTag);

    if (pending.kind == EventEvidenceRequestKind::SlotDetail) {
        ParkingSlotSnapshot slot;
        QString error;
        if (!ParkingResponseParser::parseSlotDetail(document, slot, error)) {
            emit eventEvidenceFailed(
                pending.reference.eventId, pending.reference.slotId, error);
            return;
        }

        const QString responseSlotId = resolveParkingZoneId(slot.slotId);
        if (responseSlotId != pending.reference.slotId) {
            emit eventEvidenceFailed(
                pending.reference.eventId, pending.reference.slotId,
                QStringLiteral("The server returned a different parking slot."));
            return;
        }

        pending.reference.sessionId = slot.sessionId;
        pending.reference.state = slotStateFromText(slot.state);
        if (!slot.plateNumber.trimmed().isEmpty()) {
            pending.reference.plateNumber = slot.plateNumber.trimmed();
        }
        m_eventEvidenceReferences.insert(
            pending.reference.eventId, pending.reference);

        if (pending.reference.sessionId > 0) {
            requestEventEvidenceSession(pending.reference);
            return;
        }

        emitEventEvidenceReady(pending.reference, slot.images);
        return;
    }

    QList<ParkingImageResource> images;
    QString error;
    if (!ParkingResponseParser::parseSessionImages(document, images, error)) {
        emit eventEvidenceFailed(
            pending.reference.eventId, pending.reference.slotId, error);
        return;
    }
    emitEventEvidenceReady(pending.reference, images);
}

void ParkingController::applyEventEvidenceError(
    const QString &requestTag,
    const QString &message)
{
    if (!m_pendingEventEvidenceRequests.contains(requestTag)) {
        return;
    }
    const PendingEventEvidenceRequest pending =
        m_pendingEventEvidenceRequests.take(requestTag);
    emit eventEvidenceFailed(
        pending.reference.eventId, pending.reference.slotId, message);
}

void ParkingController::emitEventEvidenceReady(
    const EventEvidenceReference &reference,
    QList<ParkingImageResource> images)
{
    for (ParkingImageResource &image : images) {
        image.url = resolveApiUrl(image.url);
    }
    emit eventEvidenceReady(
        reference.eventId, reference.slotId, reference.sessionId,
        reference.state,
        reference.plateNumber.isEmpty() ? QStringLiteral("-")
                                        : reference.plateNumber,
        images);
}

void ParkingController::initializeApiClient()
{
    m_lastApiSyncFingerprint.clear();
    QSettings sharedSettings(m_sharedConfigPath, QSettings::IniFormat);
    QSettings localSettings(m_localConfigPath, QSettings::IniFormat);
    auto setting = [&](const QString &key, const QVariant &defaultValue) {
        return localSettings.contains(key) ? localSettings.value(key, defaultValue)
                                           : sharedSettings.value(key, defaultValue);
    };

    m_state.apiEnabled = m_hasAuthenticatedServerOverride
        || setting(QStringLiteral("api/enabled"), false).toBool();
    m_apiDiagnostic = ApiDiagnosticState{};
    m_apiDiagnostic.enabled = m_state.apiEnabled;
    if (!m_state.apiEnabled) {
        if (m_reconnectTimer) m_reconnectTimer->stop();
        emit serverConnectionChanged(QStringLiteral("API disabled"), false);
        publishApiDiagnostic();
        notifyStateChanged();
        return;
    }

    m_apiBaseUrl = m_hasAuthenticatedServerOverride
        ? m_authenticatedServerOrigin
        : QUrl(setting(QStringLiteral("api/base_url"), QString())
                   .toString().trimmed());
    m_apiDiagnostic.endpoint = m_apiBaseUrl.toString(
        QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    m_apiDiagnostic.status = QStringLiteral("DISCONNECTED");
    m_slotsPath = setting(QStringLiteral("api/slots_path"),
                          QStringLiteral("/api/v1/parking-slots")).toString();
    m_slotDetailPath = setting(QStringLiteral("api/slot_detail_path"),
                               QStringLiteral("/api/v1/parking-slots/{slot_id}")).toString();
    m_sessionImagesPath = setting(
        QStringLiteral("api/session_images_path"),
        QStringLiteral("/api/v1/parking-sessions/{session_id}/images")).toString();
    m_overstayThresholdPath = setting(
        QStringLiteral("api/overstay_threshold_path"),
        QStringLiteral("/api/v1/settings/overstay-threshold")).toString();
    m_parkingRoiListPath = setting(
        QStringLiteral("api/parking_roi_list_path"),
        QStringLiteral("/api/v1/settings/parking-slots/roi")).toString();
    m_parkingRoiPathTemplate = setting(
        QStringLiteral("api/parking_roi_path"),
        QStringLiteral("/api/v1/settings/parking-slots/{slot_id}/roi")).toString();
    m_apiTimeoutMs = setting(QStringLiteral("api/timeout_ms"), 5000).toInt();
    m_allowInsecureHttp =
        setting(QStringLiteral("api/allow_insecure_http"), false).toBool();
    m_reconnectIntervalMs =
        qMax(1000, setting(QStringLiteral("api/reconnect_interval_ms"), 5000).toInt());
    m_maxReconnectIntervalMs =
        qMax(m_reconnectIntervalMs,
             setting(QStringLiteral("api/max_reconnect_interval_ms"), 60000).toInt());
    m_currentReconnectDelayMs = m_reconnectIntervalMs;
    rebuildApiClient();
    reconnectNow();
}

void ParkingController::rebuildApiClient()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
    m_snapshotRequestInFlight = false;
    if (m_overstayRequest != OverstayRequest::None) {
        const bool updateRequest = m_overstayRequest == OverstayRequest::Update;
        m_overstayRequest = OverstayRequest::None;
        m_pendingOverstaySeconds = -1;
        emit overstayThresholdRequestFailed(
            QStringLiteral("The server connection changed."), updateRequest);
    }
    const QStringList pendingRoiTags = m_pendingParkingRoiTags.values();
    for (const QString &tag : pendingRoiTags) {
        applyParkingRoiError(tag,
            QStringLiteral("The server connection changed."));
    }
    m_pendingParkingRoiTags.clear();
    m_pendingParkingRoiExpectedValues.clear();
    const QStringList pendingEventEvidenceTags =
        m_pendingEventEvidenceRequests.keys();
    for (const QString &tag : pendingEventEvidenceTags) {
        applyEventEvidenceError(
            tag, QStringLiteral("The server connection changed."));
    }
    if (m_apiClient) {
        disconnect(m_apiClient, nullptr, this, nullptr);
        m_apiClient->deleteLater();
    }
    if (m_imageLoader) m_imageLoader->deleteLater();
    m_pendingDetailRequests.clear();
    m_pendingImageRequests.clear();

    m_apiClient = new ApiClient(
        m_apiBaseUrl, m_apiTimeoutMs, m_allowInsecureHttp, this);
    m_imageLoader = new ImageLoader(
        m_apiTimeoutMs, m_allowInsecureHttp, this);
    m_apiClient->setBearerAuthentication(m_authenticatedServerOrigin,
                                         m_bearerToken);
    m_imageLoader->setBearerAuthentication(m_authenticatedServerOrigin,
                                            m_bearerToken);

    const auto handleAuthenticationExpired = [this]() {
        if (m_authenticationExpired) {
            return;
        }
        m_authenticationExpired = true;
        m_snapshotRequestInFlight = false;
        if (m_reconnectTimer) {
            m_reconnectTimer->stop();
        }
        emit authenticationExpired();
    };
    connect(m_apiClient, &ApiClient::authenticationRequired,
            this, handleAuthenticationExpired);
    connect(m_imageLoader, &ImageLoader::authenticationRequired,
            this, handleAuthenticationExpired);

    connect(m_apiClient, &ApiClient::jsonReceived, this,
            [this](const QString &path, const QJsonDocument &document,
                   int latencyMs, int httpStatus) {
                if (path == m_overstayThresholdPath
                    && m_overstayRequest != OverstayRequest::None) {
                    applyOverstayThresholdResponse(document);
                } else if (path == m_slotsPath) {
                    m_snapshotRequestInFlight = false;
                    m_apiDiagnostic.lastLatencyMs = latencyMs;
                    m_apiDiagnostic.lastHttpStatus = httpStatus;
                    applyParkingSnapshot(document);
                } else if (m_pendingImageRequests.contains(path)) {
                    applyParkingSessionImages(m_pendingImageRequests.take(path), document);
                } else {
                    const QString requestedSlotId = m_pendingDetailRequests.take(path);
                    applyParkingSlotDetail(requestedSlotId, document);
                }
            });
    connect(m_apiClient, &ApiClient::requestFailed, this,
            [this](const QString &path, const QString &message,
                   int latencyMs, int httpStatus) {
                recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("API_ERROR"),
                            path + QStringLiteral(": ") + message,
                            QStringLiteral("FAILED"));
                if (path == m_overstayThresholdPath
                    && m_overstayRequest != OverstayRequest::None) {
                    const bool updateRequest =
                        m_overstayRequest == OverstayRequest::Update;
                    m_overstayRequest = OverstayRequest::None;
                    m_pendingOverstaySeconds = -1;
                    emit overstayThresholdRequestFailed(message, updateRequest);
                } else if (path == m_slotsPath) {
                    m_snapshotRequestInFlight = false;
                    m_apiDiagnostic.connected = false;
                    m_apiDiagnostic.status = QStringLiteral("ERROR");
                    m_apiDiagnostic.lastError = message;
                    m_apiDiagnostic.lastLatencyMs = latencyMs;
                    m_apiDiagnostic.lastHttpStatus = httpStatus;
                    ++m_apiDiagnostic.consecutiveFailures;
                    scheduleReconnect(message);
                } else if (m_pendingImageRequests.contains(path)) {
                    const QString slotId = m_pendingImageRequests.take(path);
                    emit slotDetailFailed(slotId, message);
                } else if (m_pendingDetailRequests.contains(path)) {
                    const QString slotId = m_pendingDetailRequests.take(path);
                    emit slotDetailFailed(slotId, message);
                } else {
                    emit detailError(message);
                }
            });
    connect(m_apiClient, &ApiClient::taggedJsonReceived, this,
            [this](const QString &requestTag, const QString &,
                   const QJsonDocument &document, int, int) {
                if (requestTag.startsWith(QStringLiteral("parking-roi|"))) {
                    applyParkingRoiResponse(requestTag, document);
                } else if (requestTag.startsWith(
                               QStringLiteral("event-evidence|"))) {
                    applyEventEvidenceResponse(requestTag, document);
                }
            });
    connect(m_apiClient, &ApiClient::taggedRequestFailed, this,
            [this](const QString &requestTag, const QString &path,
                   const QString &message, int, int) {
                recordEvent(QStringLiteral("SYSTEM"),
                            QStringLiteral("PARKING_ROI_API_ERROR"),
                            path + QStringLiteral(": ") + message,
                            QStringLiteral("FAILED"));
                if (requestTag.startsWith(QStringLiteral("parking-roi|"))) {
                    applyParkingRoiError(requestTag, message);
                } else if (requestTag.startsWith(
                               QStringLiteral("event-evidence|"))) {
                    applyEventEvidenceError(requestTag, message);
                }
            });

    emit imageLoaderChanged(m_imageLoader);
    emit serverBaseUrlChanged(m_apiBaseUrl.toString());
    publishApiDiagnostic();
    notifyStateChanged();
}

void ParkingController::setBearerAuthentication(
    const QUrl &fixedLoginOrigin, const QByteArray &token)
{
    m_authenticatedServerOrigin =
        UrlOrigin::normalizedHttpOrigin(fixedLoginOrigin);
    m_bearerToken = token;
    if (m_bearerToken.contains('\r') || m_bearerToken.contains('\n')) {
        m_bearerToken.clear();
    }
    m_hasAuthenticatedServerOverride =
        !m_authenticatedServerOrigin.isEmpty() && !m_bearerToken.isEmpty();
    m_authenticationExpired = false;
    if (m_apiClient) {
        m_apiClient->setBearerAuthentication(m_authenticatedServerOrigin,
                                             m_bearerToken);
    }
    if (m_imageLoader) {
        m_imageLoader->setBearerAuthentication(m_authenticatedServerOrigin,
                                               m_bearerToken);
    }
}

void ParkingController::reconnectNow()
{
    if (m_authenticationExpired) {
        return;
    }
    if (!m_state.apiEnabled || !m_apiClient) {
        emit serverConnectionChanged(QStringLiteral("API disabled"), false);
        return;
    }
    if (m_reconnectTimer) m_reconnectTimer->stop();
    if (m_snapshotRequestInFlight) {
        emit serverConnectionChanged(QStringLiteral("Connection already in progress"), false);
        return;
    }
    emit serverConnectionChanged(QStringLiteral("Connecting..."), false);
    m_apiDiagnostic.connected = false;
    m_apiDiagnostic.status = QStringLiteral("CONNECTING");
    m_apiDiagnostic.lastAttemptAt = QDateTime::currentDateTime();
    m_apiDiagnostic.nextRetrySeconds = 0;
    publishApiDiagnostic();
    m_snapshotRequestInFlight = true;
    m_apiClient->getJson(m_slotsPath);
}

void ParkingController::requestOverstayThreshold()
{
    if (m_overstayRequest != OverstayRequest::None) return;
    if (!m_state.apiEnabled || !m_apiClient) {
        emit overstayThresholdRequestFailed(
            QStringLiteral("Server API is disabled or not configured."), false);
        return;
    }

    m_overstayRequest = OverstayRequest::Fetch;
    emit overstayThresholdRequestStarted(
        QStringLiteral("Loading the server setting..."));
    m_apiClient->getJson(m_overstayThresholdPath);
}

void ParkingController::updateOverstayThreshold(int seconds)
{
    if (m_overstayRequest != OverstayRequest::None) return;
    if (seconds < kMinimumOverstayThresholdSeconds
        || seconds > kMaximumOverstayThresholdSeconds) {
        emit overstayThresholdRequestFailed(
            QStringLiteral(
                "thresholdSeconds must be between 60 and 86400."), true);
        return;
    }
    if (!m_state.apiEnabled || !m_apiClient || !m_apiDiagnostic.connected) {
        emit overstayThresholdRequestFailed(
            QStringLiteral("Server is not connected."), true);
        return;
    }

    m_overstayRequest = OverstayRequest::Update;
    m_pendingOverstaySeconds = seconds;
    emit overstayThresholdRequestStarted(
        QStringLiteral("Applying the new threshold..."));
    QJsonObject request;
    request.insert(QStringLiteral("thresholdSeconds"), seconds);
    m_apiClient->putJson(m_overstayThresholdPath, request);
}

void ParkingController::requestParkingRois(quint64 generation)
{
    if (!m_state.apiEnabled || !m_apiClient) {
        emit parkingRoiRequestFailed(
            QString(), QStringLiteral("Server API is disabled or not configured."),
            generation, false);
        return;
    }
    const QString tag = parkingRoiTag(QStringLiteral("list"), generation);
    m_pendingParkingRoiTags.insert(tag);
    m_apiClient->getJsonTagged(m_parkingRoiListPath, tag);
}

void ParkingController::requestParkingRoi(const QString &slotId,
                                          quint64 generation)
{
    const QString zoneId = parkingRoiZoneId(slotId);
    if (!isParkingRoiZoneId(zoneId)) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("Parking slot was not found."),
            generation, false);
        return;
    }
    if (!m_state.apiEnabled || !m_apiClient) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("Server API is disabled or not configured."),
            generation, false);
        return;
    }
    const QString serverSlotId = parkingRoiServerSlotId(zoneId);
    if (serverSlotId.isEmpty()) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("No server slot mapping is configured."),
            generation, false);
        return;
    }
    const QString tag = parkingRoiTag(QStringLiteral("get"), generation,
                                      zoneId);
    m_pendingParkingRoiTags.insert(tag);
    m_apiClient->getJsonTagged(parkingRoiPath(serverSlotId), tag);
}

void ParkingController::updateParkingRoi(const QString &slotId,
                                         const ParkingRoi &roi,
                                         quint64 generation)
{
    const QString zoneId = parkingRoiZoneId(slotId);
    QString validationError;
    if (!isParkingRoiZoneId(zoneId)) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("Parking slot was not found."),
            generation, true);
        return;
    }
    if (!roi.isValid(&validationError)) {
        emit parkingRoiRequestFailed(zoneId, validationError,
                                     generation, true);
        return;
    }
    if (!m_state.apiEnabled || !m_apiClient) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("Server API is disabled or not configured."),
            generation, true);
        return;
    }
    const QString serverSlotId = parkingRoiServerSlotId(zoneId);
    if (serverSlotId.isEmpty()) {
        emit parkingRoiRequestFailed(
            zoneId, QStringLiteral("No server slot mapping is configured."),
            generation, true);
        return;
    }
    const QString tag = parkingRoiTag(QStringLiteral("put"), generation,
                                      zoneId);
    m_pendingParkingRoiTags.insert(tag);
    m_pendingParkingRoiExpectedValues.insert(tag, roi);
    m_apiClient->putJsonTagged(parkingRoiPath(serverSlotId),
                               ParkingRoiCodec::toJson(roi), tag);
}

void ParkingController::applyParkingRoiResponse(
    const QString &requestTag, const QJsonDocument &document)
{
    if (!m_pendingParkingRoiTags.remove(requestTag)) return;
    const bool hasExpectedRoi =
        m_pendingParkingRoiExpectedValues.contains(requestTag);
    const ParkingRoi expectedRoi =
        m_pendingParkingRoiExpectedValues.take(requestTag);
    const QStringList parts = requestTag.split(QLatin1Char('|'));
    if (parts.size() < 5) return;
    const QString kind = parts.at(1);
    bool generationOk = false;
    const quint64 generation = parts.at(2).toULongLong(&generationOk);
    const QString expectedSlot = parts.at(3);
    if (!generationOk) return;

    QString error;
    if (kind == QStringLiteral("list")) {
        ParkingRoiMap serverRois;
        if (!ParkingRoiCodec::parseList(document, serverRois, error)) {
            emit parkingRoiRequestFailed(QString(), error, generation, false);
            return;
        }
        ParkingRoiMap zoneRois;
        for (auto it = serverRois.constBegin(); it != serverRois.constEnd(); ++it) {
            const QString zoneId = parkingRoiZoneId(it.key());
            if (!isParkingRoiZoneId(zoneId) || zoneRois.contains(zoneId)) {
                emit parkingRoiRequestFailed(
                    QString(),
                    QStringLiteral("The server ROI list contains an unmapped or duplicate parking slot."),
                    generation, false);
                return;
            }
            zoneRois.insert(zoneId, it.value());
        }
        emit parkingRoiListReceived(zoneRois, generation);
        return;
    }

    QString responseServerSlot;
    ParkingRoi roi;
    bool appliedImmediately = false;
    if (!ParkingRoiCodec::parseSingle(document, responseServerSlot, roi,
                                      &appliedImmediately, error)) {
        emit parkingRoiRequestFailed(expectedSlot, error, generation,
                                     kind != QStringLiteral("get"));
        return;
    }
    const QString responseZoneId = parkingRoiZoneId(responseServerSlot);
    if (responseZoneId != expectedSlot) {
        emit parkingRoiRequestFailed(
            expectedSlot, QStringLiteral("The server returned a different parking slot."),
            generation, kind != QStringLiteral("get"));
        return;
    }

    if (kind == QStringLiteral("put")) {
        if (!hasExpectedRoi) {
            emit parkingRoiRequestFailed(
                expectedSlot, QStringLiteral("The ROI verification context was lost."),
                generation, true);
            return;
        }
        const QString serverSlotId = parkingRoiServerSlotId(expectedSlot);
        if (serverSlotId.isEmpty()) {
            emit parkingRoiRequestFailed(
                expectedSlot, QStringLiteral("No server slot mapping is configured."),
                generation, true);
            return;
        }
        const QString verifyTag = parkingRoiTag(
            QStringLiteral("verify"), generation, expectedSlot,
            appliedImmediately);
        m_pendingParkingRoiTags.insert(verifyTag);
        m_pendingParkingRoiExpectedValues.insert(verifyTag, expectedRoi);
        m_apiClient->getJsonTagged(parkingRoiPath(serverSlotId), verifyTag);
        return;
    }

    const bool afterSave = kind == QStringLiteral("verify");
    if (afterSave
        && (!hasExpectedRoi || !roi.nearlyEquals(expectedRoi))) {
        emit parkingRoiRequestFailed(
            expectedSlot,
            QStringLiteral("The saved ROI does not match the requested ROI."),
            generation, true);
        return;
    }
    const bool verifiedAppliedImmediately = afterSave
        && parts.at(4) == QStringLiteral("1");
    emit parkingRoiReceived(responseZoneId, roi, generation, afterSave,
                            verifiedAppliedImmediately);
    if (afterSave) {
        recordEvent(
            responseZoneId, QStringLiteral("PARKING_ROI_UPDATED"),
            QStringLiteral("Normalized ROI saved and verified"),
            QStringLiteral("DONE"));
    }
}

void ParkingController::applyParkingRoiError(
    const QString &requestTag, const QString &message)
{
    if (!m_pendingParkingRoiTags.remove(requestTag)) return;
    m_pendingParkingRoiExpectedValues.remove(requestTag);
    const QStringList parts = requestTag.split(QLatin1Char('|'));
    if (parts.size() < 5) return;
    bool generationOk = false;
    const quint64 generation = parts.at(2).toULongLong(&generationOk);
    if (!generationOk) return;
    const QString kind = parts.at(1);
    emit parkingRoiRequestFailed(parts.at(3), message, generation,
                                 kind == QStringLiteral("put")
                                     || kind == QStringLiteral("verify"));
}

QString ParkingController::parkingRoiPath(const QString &slotId) const
{
    QString path = m_parkingRoiPathTemplate;
    path.replace(QStringLiteral("{slot_id}"), slotId);
    return path;
}

QString ParkingController::parkingRoiZoneId(const QString &slotId) const
{
    const QString mapped = m_slotIdMapper.toZoneId(slotId);
    return normalizeParkingSlotId(mapped.isEmpty() ? slotId : mapped);
}

QString ParkingController::parkingRoiServerSlotId(const QString &zoneId) const
{
    QString serverSlotId = m_slotIdMapper.toServerSlotId(
        normalizeParkingSlotId(zoneId));
    if (serverSlotId.startsWith(QStringLiteral("EV-"))
        || serverSlotId.startsWith(QStringLiteral("P-"))) {
        serverSlotId.remove(QLatin1Char('-'));
    }
    return serverSlotId;
}

void ParkingController::applyOverstayThresholdResponse(
    const QJsonDocument &document)
{
    const OverstayRequest completedRequest = m_overstayRequest;
    const bool updateResponse = completedRequest == OverstayRequest::Update;
    int seconds = -1;
    QString applyPolicy;
    QString errorMessage;
    if (!parseOverstayThreshold(document, updateResponse, seconds,
                                applyPolicy, errorMessage)) {
        m_overstayRequest = OverstayRequest::None;
        m_pendingOverstaySeconds = -1;
        emit overstayThresholdRequestFailed(
            errorMessage, updateResponse);
        return;
    }

    const QJsonObject object = document.object();
    const QJsonValue effectiveValue = object.value(
        QStringLiteral("effectiveSeconds"));
    const QJsonValue revisionValue = object.value(
        QStringLiteral("appliedRevision"));
    const QJsonValue runtimeAppliedValue = object.value(
        QStringLiteral("runtimeApplied"));
    const QJsonValue runtimeHealthyValue = object.value(
        QStringLiteral("runtimeHealthy"));
    const auto exactSeconds = [](const QJsonValue &value, int expected) {
        return value.isDouble()
            && value.toDouble() == static_cast<double>(expected);
    };
    const qint64 revision = revisionValue.toInteger(-1);
    const bool runtimeContractValid =
        exactSeconds(effectiveValue, seconds)
        && revisionValue.isDouble() && revision > 0
        && revisionValue.toDouble() == static_cast<double>(revision)
        && runtimeAppliedValue.isBool() && runtimeAppliedValue.toBool()
        && runtimeHealthyValue.isBool() && runtimeHealthyValue.toBool();
    if (!runtimeContractValid) {
        m_overstayRequest = OverstayRequest::None;
        m_pendingOverstaySeconds = -1;
        emit overstayThresholdRequestFailed(
            QStringLiteral(
                "The server runtime policy is unhealthy or incomplete."),
            updateResponse);
        return;
    }

    if (updateResponse) {
        const QJsonValue requestedValue = object.value(
            QStringLiteral("requestedSeconds"));
        if (m_pendingOverstaySeconds < 0
            || seconds != m_pendingOverstaySeconds
            || !exactSeconds(requestedValue, m_pendingOverstaySeconds)) {
            m_overstayRequest = OverstayRequest::None;
            m_pendingOverstaySeconds = -1;
            emit overstayThresholdRequestFailed(
                QStringLiteral(
                    "The server did not confirm the effective runtime policy."),
                true);
            return;
        }
    }

    m_overstayRequest = OverstayRequest::None;
    m_pendingOverstaySeconds = -1;
    emit overstayThresholdReceived(seconds, applyPolicy,
                                   updateResponse);
    if (updateResponse) {
        recordEvent(
            QStringLiteral("SYSTEM"),
            QStringLiteral("OVERSTAY_THRESHOLD_UPDATED"),
            QStringLiteral("Applied %1 seconds | policy=%2")
                .arg(seconds)
                .arg(applyPolicy.isEmpty()
                         ? QStringLiteral("NOT_PROVIDED") : applyPolicy),
            QStringLiteral("DONE"));
    }
}

void ParkingController::scheduleReconnect(const QString &reason)
{
    if (m_authenticationExpired
        || !m_state.apiEnabled || !m_reconnectTimer) return;
    const int delayMs = m_currentReconnectDelayMs;
    emit bannerChanged(
        QStringLiteral("Parking API unavailable | Retrying in %1 seconds")
            .arg(delayMs / 1000),
        true);
    emit serverConnectionChanged(
        QStringLiteral("Disconnected: %1 | retry in %2 seconds")
            .arg(reason).arg(delayMs / 1000),
        false);
    m_apiDiagnostic.connected = false;
    m_apiDiagnostic.status = QStringLiteral("RETRYING");
    m_apiDiagnostic.lastError = reason;
    m_apiDiagnostic.nextRetrySeconds = delayMs / 1000;
    publishApiDiagnostic();
    if (!m_reconnectTimer->isActive()) m_reconnectTimer->start(delayMs);
    m_currentReconnectDelayMs =
        qMin(m_currentReconnectDelayMs * 2, m_maxReconnectIntervalMs);
}

void ParkingController::updateServerBaseUrl(const QString &baseUrl)
{
    QString normalized = baseUrl.trimmed();
    while (normalized.endsWith(QLatin1Char('/'))) normalized.chop(1);
    const QUrl url(normalized);
    const bool allowedScheme = url.scheme() == QStringLiteral("https")
        || (m_allowInsecureHttp && url.scheme() == QStringLiteral("http"));
    if (!url.isValid() || url.host().isEmpty() || !allowedScheme) {
        emit serverConfigurationError(
            QStringLiteral("Enter a valid %1 server URL including port.")
                .arg(m_allowInsecureHttp ? QStringLiteral("HTTP or HTTPS")
                                        : QStringLiteral("HTTPS")));
        return;
    }
    if (m_hasAuthenticatedServerOverride
        && !UrlOrigin::sameHttpOrigin(url, m_authenticatedServerOrigin)) {
        emit serverConfigurationError(QStringLiteral(
            "Sign in again before changing to a different server."));
        return;
    }

    QSettings localSettings(m_localConfigPath, QSettings::IniFormat);
    localSettings.setValue(QStringLiteral("api/enabled"), true);
    localSettings.setValue(QStringLiteral("api/base_url"), normalized);
    // The deployed Mosquitto broker runs on the same Pi as the HTTP API. Keep
    // the effective broker address in sync with the full URL entered by the
    // user instead of preserving a host from a previous subnet.
    localSettings.setValue(QStringLiteral("mqtt/follow_api_host"), true);
    localSettings.setValue(QStringLiteral("mqtt/host"), url.host());
    localSettings.sync();
    if (localSettings.status() != QSettings::NoError) {
        emit serverConfigurationError(
            QStringLiteral("Failed to save client_config.local.ini."));
        return;
    }

    recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("API_ADDRESS_UPDATED"),
                QStringLiteral("Server API changed to ")
                    + url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery
                                   | QUrl::RemoveFragment),
                QStringLiteral("DONE"));
    initializeApiClient();
    initializeMqttClient();
}
void ParkingController::applyParkingSnapshot(const QJsonDocument &document)
{
    ParkingSnapshot snapshot;
    QString error;
    if (!ParkingResponseParser::parseSnapshot(document, snapshot, error)) {
        recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("API_PARSE_ERROR"), error, QStringLiteral("FAILED"));
        ++m_apiDiagnostic.consecutiveFailures;
        // 파싱 실패도 네트워크 실패와 동일하게 재시도 루프를 이어간다. 여기서
        // 타이머를 멈춘 채로 두면 다음 응답이 잘못됐다는 이유만으로 폴링 자체가
        // 영구히 멈춘다.
        scheduleReconnect(error);
        return;
    }
    m_snapshotRequestInFlight = false;
    m_currentReconnectDelayMs = m_reconnectIntervalMs;
    // 성공해도 타이머를 멈추지 않고 base interval 로 재시작한다. 이 타이머가
    // 실패 시 백오프 재시도와, 정상 상태에서의 주기적 재폴링을 겸한다.
    // 그래야 Pi 쪽 데이터가 바뀌었을 때 사용자가 Reconnect now 를 눌러야만
    // 화면이 갱신되는 문제가 없다.
    if (m_reconnectTimer) m_reconnectTimer->start(m_currentReconnectDelayMs);
    emit serverConnectionChanged(QStringLiteral("Connected"), true);
    const ParkingViewState previousState = m_state;
    resetSlotsForSnapshot();
    int appliedCount = 0;
    int rejectedCount = 0;
    int duplicateCount = 0;
    for (const ParkingSlotSnapshot &slot : snapshot.parkingSlots) {
        const QString mappedSlotId = m_slotIdMapper.toZoneId(slot.slotId);
        if (mappedSlotId.isEmpty()) {
            ++rejectedCount;
            recordEvent(slot.slotId, QStringLiteral("API_SLOT_SKIPPED"), QStringLiteral("No zone mapping for server slot"), QStringLiteral("SKIPPED"));
            continue;
        }
        // Even if mapped, we must still normalize it to standard EV-01 / P-01 format
        // in case the mapper's output wasn't perfect, or for passthrough mode.
        const QString slotId = normalizeParkingSlotId(mappedSlotId);
        if (!isEvSlotId(slotId) && !isGeneralSlotId(slotId)) {
            ++rejectedCount;
            recordEvent(slotId, QStringLiteral("API_SLOT_SKIPPED"), QStringLiteral("Unknown parking slot in response"), QStringLiteral("SKIPPED"));
            continue;
        }
        if (m_state.evSlots.contains(slotId) || m_state.parkingSlots.contains(slotId)) {
            ++duplicateCount;
        }
        SlotState state = slotStateFromText(slot.state);
        const SlotAlarmKind alarmKind = slotAlarmKindFromText(slot.alarm, state);
        if (alarmKind == SlotAlarmKind::NonEvViolation) state = SlotState::NonEvAlert;
        else if (alarmKind == SlotAlarmKind::Overstay) state = SlotState::OvertimeAlert;
        else if (alarmKind == SlotAlarmKind::SensorError) state = SlotState::SensorError;
        SlotVisualState visual = deriveSlotVisualState(
            state, slot.vehicleTypeKnown, slot.isEv, slot.alarm);
        SlotVisualState previousVisual;
        bool hasPreviousVisual = false;
        if (previousState.evSlots.contains(slotId)) {
            previousVisual = previousState.evSlots.value(slotId).visual;
            hasPreviousVisual = true;
        } else if (previousState.parkingSlots.contains(slotId)) {
            previousVisual = previousState.parkingSlots.value(slotId).visual;
            hasPreviousVisual = true;
        }
        if (hasPreviousVisual) {
            const bool persistentAlarm =
                previousVisual.alarm == SlotAlarmKind::SensorError
                || (visual.occupancy == SlotOccupancy::Occupied
                    && previousVisual.alarm != SlotAlarmKind::None);
            if (persistentAlarm) {
                visual.alarm = previousVisual.alarm;
                visual.alarmAcknowledged = previousVisual.alarmAcknowledged;
                state = stateFromVisual(visual);
            }
        }
        if (slotStateFromText(slot.state) == SlotState::Acked) {
            visual.alarmAcknowledged = true;
        }
        if (slotId.startsWith(QStringLiteral("EV-"))) {
            EvSlotInfo info{slotId, slot.plateNumber.isEmpty() ? QStringLiteral("-") : slot.plateNumber,
                            slot.isEv, occupiedDurationText(slot.occupiedSince, slot.elapsedSeconds), state,
                            visual.alarm == SlotAlarmKind::None
                                ? (slot.alarm.isEmpty()
                                       ? QStringLiteral("NORMAL") : slot.alarm)
                                : alarmTextFromKind(visual.alarm)};
            info.visual = visual;
            info.occupiedSince = slot.occupiedSince;
            info.lastUpdatedAt = snapshot.generatedAt;
            m_state.evSlots[slotId] = info;
        } else {
            ParkingSlotInfo info{slotId, state};
            info.visual = visual;
            info.occupiedTime = occupiedDurationText(
                slot.occupiedSince, slot.elapsedSeconds);
            info.occupiedSince = slot.occupiedSince;
            info.lastUpdatedAt = snapshot.generatedAt;
            m_state.parkingSlots[slotId] = info;
        }
        m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
        m_state.slotSessionIds.insert(slotId, slot.sessionId);
        QList<ParkingImageResource> images;
        for (ParkingImageResource image : slot.images) { image.url = resolveApiUrl(image.url); images.append(image); }
        if (images.isEmpty() && slot.sessionId > 0
            && previousState.slotSessionIds.value(slotId, -1)
                == slot.sessionId) {
            images = previousState.slotImages.value(slotId);
        }
        if (!images.isEmpty()) m_state.slotImages.insert(slotId, images);
        ++appliedCount;
    }
    // Keep Dashboard capacity tied to the latest accepted server snapshot.
    // MQTT/manual updates can add or remove entries in m_state afterwards;
    // they are runtime state changes, not a new capacity declaration.
    m_state.serverSlotCount = m_state.evSlots.size() + m_state.parkingSlots.size();
    m_state.hasServerSnapshot = true;
    m_state.generatedAt = snapshot.generatedAt;
    notifyStateChanged();
    const QString generatedAt = snapshot.generatedAt.isValid()
        ? snapshot.generatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const int uniqueCount = m_state.serverSlotCount;
    const int evCount = m_state.evSlots.size();
    const int parkingCount = m_state.parkingSlots.size();
    const int receivedCount = snapshot.parkingSlots.size();
    const QString snapshotDetail = QStringLiteral(
        "Server snapshot: received=%1, applied=%2, unique=%3 (EV=%4, P=%5), "
        "rejected=%6, duplicates=%7, generated=%8")
        .arg(receivedCount)
        .arg(appliedCount)
        .arg(uniqueCount)
        .arg(evCount)
        .arg(parkingCount)
        .arg(rejectedCount)
        .arg(duplicateCount)
        .arg(generatedAt);
    emit statusMessageChanged(snapshotDetail);
    m_apiDiagnostic.connected = true;
    m_apiDiagnostic.status = QStringLiteral("CONNECTED");
    m_apiDiagnostic.lastError.clear();
    m_apiDiagnostic.lastSuccessAt = QDateTime::currentDateTime();
    m_apiDiagnostic.consecutiveFailures = 0;
    m_apiDiagnostic.nextRetrySeconds = 0;
    m_apiDiagnostic.appliedSlotCount = uniqueCount;
    publishApiDiagnostic();
    const QByteArray fingerprint = apiSyncFingerprint(
        m_state, receivedCount, appliedCount, rejectedCount, duplicateCount);
    if (fingerprint != m_lastApiSyncFingerprint) {
        m_lastApiSyncFingerprint = fingerprint;
        recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("API_SYNC"),
                    snapshotDetail, QStringLiteral("DONE"));
    }
}

void ParkingController::publishApiDiagnostic()
{
    emit apiDiagnosticChanged(m_apiDiagnostic);
}

void ParkingController::applyParkingSlotDetail(
    const QString &requestedSlotId,
    const QJsonDocument &document)
{
    ParkingSlotSnapshot slot;
    QString error;
    if (!ParkingResponseParser::parseSlotDetail(document, slot, error)) {
        emit slotDetailFailed(requestedSlotId, error);
        refreshAlert();
        return;
    }
    const QString mappedSlotId = m_slotIdMapper.toZoneId(slot.slotId);
    if (mappedSlotId.isEmpty()) {
        emit slotDetailFailed(requestedSlotId, QStringLiteral("Server returned unmapped slot ID"));
        refreshAlert();
        return;
    }
    const QString slotId = normalizeParkingSlotId(mappedSlotId);
    m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
    m_state.slotSessionIds.insert(slotId, slot.sessionId);
    QList<ParkingImageResource> images;
    for (ParkingImageResource image : slot.images) { image.url = resolveApiUrl(image.url); images.append(image); }
    if (images.isEmpty()) m_state.slotImages.remove(slotId); else m_state.slotImages.insert(slotId, images);
    notifyStateChanged();

    if (slot.sessionId <= 0 || !m_apiClient) {
        if (images.isEmpty()) {
            m_state.slotImages.remove(slotId);
        }
        refreshAlert();
        emit slotDetailReady(slotId);
        return;
    }

    QString path = m_sessionImagesPath;
    path.replace(QStringLiteral("{session_id}"), QString::number(slot.sessionId));
    if (m_pendingImageRequests.contains(path)) {
        return;
    }
    m_pendingImageRequests.insert(path, slotId);
    emit bannerChanged(QStringLiteral("Loading %1 evidence images...").arg(slotId), false);
    m_apiClient->getJson(path);
}

void ParkingController::applyParkingSessionImages(
    const QString &slotId,
    const QJsonDocument &document)
{
    QList<ParkingImageResource> images;
    QString error;
    if (!ParkingResponseParser::parseSessionImages(document, images, error)) {
        emit slotDetailFailed(slotId, error);
        refreshAlert();
        return;
    }

    for (ParkingImageResource &image : images) {
        image.url = resolveApiUrl(image.url);
    }
    if (images.isEmpty()) {
        m_state.slotImages.remove(slotId);
    } else {
        m_state.slotImages.insert(slotId, images);
    }
    notifyStateChanged();
    refreshAlert();
    emit slotDetailReady(slotId);
}

void ParkingController::resetSlotsForSnapshot()
{
    m_state.evSlots.clear();
    m_state.parkingSlots.clear();
    m_state.slotImages.clear();
    m_state.slotPlateNumbers.clear();
    m_state.slotSessionIds.clear();
}

void ParkingController::requestSlotDetail(const QString &rawSlotId)
{
    const QString slotId = normalizeParkingSlotId(rawSlotId);
    if (!m_apiClient) { emit slotDetailReady(slotId); return; }
    
    QString serverSlotId = m_slotIdMapper.toServerSlotId(slotId);
    if (serverSlotId.isEmpty()) {
        serverSlotId = slotId; // fallback
    }
    // Backward compatibility for passthrough mode (e.g. EV-01 -> EV01)
    if (serverSlotId.startsWith(QStringLiteral("EV-")) || serverSlotId.startsWith(QStringLiteral("P-"))) {
        serverSlotId.remove(QLatin1Char('-'));
    }

    QString path = m_slotDetailPath; path.replace(QStringLiteral("{slot_id}"), serverSlotId);
    if (m_pendingDetailRequests.contains(path)) {
        emit statusMessageChanged(QStringLiteral("%1 evidence request is already in progress").arg(slotId));
        return;
    }
    m_pendingDetailRequests.insert(path, slotId);
    emit bannerChanged(QStringLiteral("Loading %1 details...").arg(slotId), false);
    m_apiClient->getJson(path);
}

void ParkingController::replaceViewState(const ParkingViewState &state)
{
    m_state = state;
    // replaceViewState() is used by the local simulation/test path. It must
    // not be mistaken for a server-provided capacity snapshot.
    m_state.serverSlotCount = 0;
    m_state.hasServerSnapshot = false;
    notifyStateChanged();
}

void ParkingController::applyEvSlotUpdate(const QString &slotId, SlotState state,
                                          const QString &plateNumber, bool isEv,
                                          const QString &occupiedTime, const QString &alarmText)
{
    EvSlotInfo updated{slotId, plateNumber, isEv, occupiedTime, state, alarmText};
    if (state == SlotState::Acked && m_state.evSlots.contains(slotId)) {
        updated.visual = m_state.evSlots.value(slotId).visual;
        updated.visual.alarmAcknowledged = true;
    } else {
        updated.visual = deriveSlotVisualState(state, state != SlotState::Vacant,
                                               isEv, alarmText);
    }
    m_state.evSlots[slotId] = updated;
    notifyStateChanged();
}

void ParkingController::applyParkingSlotUpdate(const QString &slotId, SlotState state)
{
    ParkingSlotInfo updated{slotId, state};
    const ParkingSlotInfo previous = m_state.parkingSlots.value(slotId);
    if (state == SlotState::SensorError) {
        updated.visual = previous.visual;
        updated.visual.alarm = SlotAlarmKind::SensorError;
        updated.visual.alarmAcknowledged = false;
    } else if (state == SlotState::Acked) {
        updated.visual = previous.visual;
        updated.visual.alarmAcknowledged = true;
    } else {
        updated.visual = deriveSlotVisualState(state, state == SlotState::Occupied, false);
    }
    m_state.parkingSlots[slotId] = updated;
    if (state == SlotState::Vacant) {
        m_state.slotPlateNumbers.remove(slotId);
        m_state.slotImages.remove(slotId);
        m_state.slotSessionIds.remove(slotId);
    }
    notifyStateChanged();
}

void ParkingController::notifyStateChanged()
{
    emit stateChanged();
    refreshAlert();
}

void ParkingController::refreshAlert()
{
    if (!m_state.fireChannels.isEmpty()) {
        QStringList unacknowledgedChannels;
        QStringList acknowledgedChannels;
        for (const QString &channelId : m_state.fireChannels) {
            const ChannelFireAlarmState alarm =
                m_state.fireAlarms.value(channelId);
            if (alarm.acknowledged) acknowledgedChannels.append(channelId);
            else unacknowledgedChannels.append(channelId);
        }
        unacknowledgedChannels.sort();
        acknowledgedChannels.sort();
        if (!unacknowledgedChannels.isEmpty()) {
            emit bannerChanged(
                QStringLiteral("FIRE SUSPECTED: %1 | operator must verify")
                    .arg(unacknowledgedChannels.join(QStringLiteral(" / "))),
                true);
        } else {
            emit bannerChanged(
                QStringLiteral("FIRE ACKNOWLEDGED: %1 | waiting for sensor clear")
                    .arg(acknowledgedChannels.join(QStringLiteral(" / "))),
                true);
        }
        return;
    }

    QStringList alerts;
    for (const EvSlotInfo &slot : m_state.evSlots) {
        if (slot.visual.alarm != SlotAlarmKind::None
            && !slot.visual.alarmAcknowledged) {
            alerts << slot.slotId + QLatin1Char(':') + slotAlarmText(slot.visual.alarm);
        }
    }
    for (const ParkingSlotInfo &slot : m_state.parkingSlots) {
        if (slot.visual.alarm != SlotAlarmKind::None
            && !slot.visual.alarmAcknowledged) {
            alerts << slot.slotId + QLatin1Char(':') + slotAlarmText(slot.visual.alarm);
        }
    }

    emit bannerChanged(alerts.isEmpty() ? QStringLiteral("Monitoring normal | No active alerts")
                                        : QStringLiteral("Active alerts: %1 | %2").arg(alerts.size()).arg(alerts.join(QStringLiteral(" / "))),
                       !alerts.isEmpty());
}

void ParkingController::recordEvent(const QString &zone, const QString &eventType,
                                    const QString &message, const QString &status)
{
    MonitoringEvent event;
    event.occurredAt = QDateTime::currentDateTime();
    event.id = QStringLiteral("client-%1-%2")
                   .arg(event.occurredAt.toMSecsSinceEpoch())
                   .arg(m_nextEventSequence++);
    const QString channelId = normalizeCameraChannelId(zone);
    if (isFireLifecycleEventType(eventType) && channelId.isEmpty()) {
        event.sourceId = QStringLiteral("SYSTEM");
        event.eventType = QStringLiteral("FIRE_CHANNEL_ERROR");
        event.message = QStringLiteral("Rejected non-channel fire source %1: %2")
                            .arg(zone, message);
        event.status = QStringLiteral("REJECTED");
    } else {
        event.sourceId = channelId.isEmpty() ? zone : channelId;
        if (channelId.isEmpty()) {
            event.evidenceSlotId = resolveParkingZoneId(zone);
        }
        event.eventType = eventType;
        event.message = message;
        event.status = status;
    }
    event.ackState = eventAckStateFromStatus(event.status);
    rememberEventEvidence(event);
    emit eventLogged(event);
}

void ParkingController::clearAlarms()
{
    bool cleared = false;
    for (auto it = m_state.evSlots.begin(); it != m_state.evSlots.end(); ++it) {
        if (it->visual.alarm != SlotAlarmKind::None
            && !it->visual.alarmAcknowledged) {
            it->state = SlotState::Acked;
            it->visual.alarmAcknowledged = true;
            it->alarmText = QStringLiteral("ACKED");
            cleared = true;
        }
    }
    for (auto it = m_state.parkingSlots.begin(); it != m_state.parkingSlots.end(); ++it) {
        if (it->visual.alarm != SlotAlarmKind::None
            && !it->visual.alarmAcknowledged) {
            it->state = SlotState::Acked;
            it->visual.alarmAcknowledged = true;
            cleared = true;
        }
    }
    notifyStateChanged();
    recordEvent(QStringLiteral("ALL"), QStringLiteral("ALARM_ACK"), cleared ? QStringLiteral("Active alarms acknowledged") : QStringLiteral("No alarms to clear"), QStringLiteral("ACKED"));
}

void ParkingController::applyManualJsonMessage(const QJsonObject &json)
{
    const QString eventType = json.value(QStringLiteral("event_type")).toString().trimmed().toUpper();
    emit statusMessageChanged(QStringLiteral("Manual RX JSON: ") + eventType);

    const ServerFireEvent fireEvent = ServerFireEventAdapter::parse(json);
    if (fireEvent.action == ServerFireEventAction::Invalid) {
        recordEvent(fireEvent.channelId.isEmpty() ? QStringLiteral("SYSTEM") : fireEvent.channelId,
                    QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                    QStringLiteral("manual: ") + fireEvent.errorMessage,
                    QStringLiteral("REJECTED"));
        return;
    }
    if (fireEvent.action != ServerFireEventAction::NotFire) {
        applyChannelFireEvent(fireEvent, QStringLiteral("manual"), false);
        return;
    }

    if (applyServerParkingEvent(json, QStringLiteral("manual"))) {
        return;
    }

    recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("RX_UNSUPPORTED"),
                QStringLiteral("Unsupported manual JSON message: ") + eventType,
                QStringLiteral("REJECTED"));
}

SlotState ParkingController::slotState(const QString &slotId) const
{
    if (m_state.evSlots.contains(slotId)) return m_state.evSlots.value(slotId).state;
    return m_state.parkingSlots.value(slotId).state;
}

QString ParkingController::plateNumber(const QString &slotId) const
{
    if (m_state.evSlots.contains(slotId)) return m_state.evSlots.value(slotId).plateNumber;
    return m_state.slotPlateNumbers.value(slotId, QStringLiteral("-"));
}

QList<ParkingImageResource> ParkingController::images(const QString &slotId) const
{
    return m_state.slotImages.value(slotId);
}

QUrl ParkingController::resolveApiUrl(const QUrl &url) const
{
    if (url.isEmpty() || !url.isRelative()) return url;
    QString base = m_apiBaseUrl.toString(); while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QString path = url.toString(); if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));
    return QUrl(base + path);
}

QString ParkingController::occupiedDurationText(const QDateTime &occupiedSince, int elapsedSeconds) const
{
    qint64 seconds = elapsedSeconds;
    if (seconds <= 0 && occupiedSince.isValid()) seconds = qMax<qint64>(0, occupiedSince.secsTo(QDateTime::currentDateTime()));
    if (seconds <= 0) return QStringLiteral("-");
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0')).arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
