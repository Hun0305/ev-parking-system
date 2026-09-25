#include "parkingcontroller.h"

#include "adapters/incomingmessageadapter.h"
#include "adapters/serverfireeventadapter.h"
#include "adapters/serverparkingeventadapter.h"
#include "api/apiclient.h"
#include "api/imageloader.h"
#include "api/parkingresponseparser.h"
#include "services/mqttserviceclient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int kSeenServerEventLimit = 256;
constexpr int kPortableMqttClientIdMaxLength = 23;
constexpr int kGeneratedMqttClientIdSuffixLength = 12;

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
        const QUrl apiUrl(
            setting(QStringLiteral("api/base_url"), QString()).toString().trimmed());
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

void ParkingController::applyChannelFireEvent(const ServerFireEvent &event,
                                              const QString &topic,
                                              const bool retained)
{
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
            || previous.alarmId != event.alarmId;
        ChannelFireAlarmState updated;
        updated.alarmId = event.alarmId;
        updated.alarmState = event.alarmState.isEmpty()
            ? QStringLiteral("OPEN") : event.alarmState;
        updated.ackState = event.ackState.isEmpty()
            ? QStringLiteral("unacked") : event.ackState;
        updated.active = true;

        // A repeated DETECTED for the same active alarm must never undo ACK.
        if (previous.active && previous.acknowledged
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
        if ((!retained && !previous.active)
            || (previous.active && previous.alarmId != event.alarmId)) {
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
        if (previous.active && previous.alarmId != event.alarmId) {
            recordEvent(
                event.channelId,
                QStringLiteral("MQTT_FIRE_CONTRACT_ERROR"),
                QStringLiteral("Rejected clear for mismatched alarm_id"),
                QStringLiteral("REJECTED"));
            return;
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

    const bool stateOnly = retained
        || topic.startsWith(QStringLiteral("parking/fire/"));
    if (!stateOnly || event.action != ServerFireEventAction::Activate) {
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

    const QString slotId = normalizeParkingSlotId(event.slotId);
    if (slotId.isEmpty()) {
        if (event.eventType == QStringLiteral("SENSOR_ERROR")
            || event.eventType == QStringLiteral("SENSOR_RECOVERED")) {
            recordServerEvent(event, QStringLiteral("SYSTEM"));
            return true;
        }
        return false;
    }
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
    const bool hasStateMeaning = occupied || vacant || alarmOpen
        || monitoringStatus == QStringLiteral("CLEARED");
    if (!hasStateMeaning) {
        return false;
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
            }
        } else {
            removeUnknownState = !m_state.parkingSlots.contains(slotId)
                || m_state.parkingSlots.value(slotId).visual.occupancy
                    == SlotOccupancy::Unknown;
            if (removeUnknownState) {
                m_state.parkingSlots.remove(slotId);
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
        } else if (vacant) {
            visual.occupancy = SlotOccupancy::Vacant;
            visual.vehicleClass = VehicleClass::Unknown;
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
            slot.plateNumber = QStringLiteral("-");
            slot.occupiedTime = QStringLiteral("-");
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
        slot.visual = visual;
        m_state.evSlots.insert(slotId, slot);
        m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
    } else {
        ParkingSlotInfo slot = m_state.parkingSlots.value(slotId);
        slot.slotId = slotId;
        SlotVisualState visual = slot.visual;

        if (occupied) {
            visual.occupancy = SlotOccupancy::Occupied;
            visual.vehicleClass = VehicleClass::General;
        } else if (vacant) {
            visual.occupancy = SlotOccupancy::Vacant;
            visual.vehicleClass = VehicleClass::Unknown;
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
        }
        if (alarmOpen) {
            visual.alarm = incomingAlarm;
            visual.alarmAcknowledged = false;
        } else if (alarmCleared) {
            visual.alarm = SlotAlarmKind::None;
            visual.alarmAcknowledged = false;
        }

        slot.state = stateFromVisual(visual);
        slot.visual = visual;
        m_state.parkingSlots.insert(slotId, slot);
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

void ParkingController::initializeApiClient()
{
    QSettings sharedSettings(m_sharedConfigPath, QSettings::IniFormat);
    QSettings localSettings(m_localConfigPath, QSettings::IniFormat);
    auto setting = [&](const QString &key, const QVariant &defaultValue) {
        return localSettings.contains(key) ? localSettings.value(key, defaultValue)
                                           : sharedSettings.value(key, defaultValue);
    };

    m_state.apiEnabled = setting(QStringLiteral("api/enabled"), false).toBool();
    m_apiDiagnostic = ApiDiagnosticState{};
    m_apiDiagnostic.enabled = m_state.apiEnabled;
    if (!m_state.apiEnabled) {
        if (m_reconnectTimer) m_reconnectTimer->stop();
        emit serverConnectionChanged(QStringLiteral("API disabled"), false);
        publishApiDiagnostic();
        notifyStateChanged();
        return;
    }

    m_apiBaseUrl = QUrl(setting(QStringLiteral("api/base_url"), QString()).toString().trimmed());
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

    connect(m_apiClient, &ApiClient::jsonReceived, this,
            [this](const QString &path, const QJsonDocument &document,
                   int latencyMs, int httpStatus) {
                if (path == m_slotsPath) {
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
                if (path == m_slotsPath) {
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

    emit serverBaseUrlChanged(m_apiBaseUrl.toString());
    publishApiDiagnostic();
    notifyStateChanged();
}
void ParkingController::reconnectNow()
{
    if (!m_state.apiEnabled || !m_apiClient) {
        emit serverConnectionChanged(QStringLiteral("API disabled"), false);
        return;
    }
    if (m_reconnectTimer) m_reconnectTimer->stop();
    if (m_snapshotRequestInFlight) {
        emit serverConnectionChanged(QStringLiteral("Connection already in progress"), false);
        return;
    }
    const QString message =
        QStringLiteral("Connecting to %1...").arg(m_apiBaseUrl.toString(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment));
    emit bannerChanged(message, false);
    emit serverConnectionChanged(QStringLiteral("Connecting..."), false);
    m_apiDiagnostic.connected = false;
    m_apiDiagnostic.status = QStringLiteral("CONNECTING");
    m_apiDiagnostic.lastAttemptAt = QDateTime::currentDateTime();
    m_apiDiagnostic.nextRetrySeconds = 0;
    publishApiDiagnostic();
    m_snapshotRequestInFlight = true;
    m_apiClient->getJson(m_slotsPath);
}

void ParkingController::scheduleReconnect(const QString &reason)
{
    if (!m_state.apiEnabled || !m_reconnectTimer) return;
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
    for (const ParkingSlotSnapshot &slot : snapshot.parkingSlots) {
        const QString slotId = normalizeParkingSlotId(slot.slotId);
        if (!isEvSlotId(slotId) && !isGeneralSlotId(slotId)) {
            recordEvent(slotId, QStringLiteral("API_SLOT_SKIPPED"), QStringLiteral("Unknown parking slot in response"), QStringLiteral("SKIPPED"));
            continue;
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
            m_state.evSlots[slotId] = info;
        } else {
            ParkingSlotInfo info{slotId, state};
            info.visual = visual;
            m_state.parkingSlots[slotId] = info;
        }
        m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
        QList<ParkingImageResource> images;
        for (ParkingImageResource image : slot.images) { image.url = resolveApiUrl(image.url); images.append(image); }
        if (!images.isEmpty()) m_state.slotImages.insert(slotId, images);
        ++appliedCount;
    }
    notifyStateChanged();
    const QString generatedAt = snapshot.generatedAt.isValid()
        ? snapshot.generatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    emit statusMessageChanged(QStringLiteral("API synchronized: %1 slots at %2").arg(appliedCount).arg(generatedAt));
    m_apiDiagnostic.connected = true;
    m_apiDiagnostic.status = QStringLiteral("CONNECTED");
    m_apiDiagnostic.lastError.clear();
    m_apiDiagnostic.lastSuccessAt = QDateTime::currentDateTime();
    m_apiDiagnostic.consecutiveFailures = 0;
    m_apiDiagnostic.nextRetrySeconds = 0;
    m_apiDiagnostic.appliedSlotCount = appliedCount;
    publishApiDiagnostic();
    recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("API_SYNC"), QStringLiteral("Applied %1 parking slots").arg(appliedCount), QStringLiteral("DONE"));
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
    const QString slotId = normalizeParkingSlotId(slot.slotId);
    m_state.slotPlateNumbers.insert(slotId, slot.plateNumber);
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
    m_state.evSlots.clear(); m_state.parkingSlots.clear(); m_state.slotImages.clear(); m_state.slotPlateNumbers.clear();
}

void ParkingController::requestSlotDetail(const QString &rawSlotId)
{
    const QString slotId = normalizeParkingSlotId(rawSlotId);
    if (!m_apiClient) { emit slotDetailReady(slotId); return; }
    QString compact = slotId; compact.remove(QLatin1Char('-'));
    QString path = m_slotDetailPath; path.replace(QStringLiteral("{slot_id}"), compact);
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
        event.eventType = eventType;
        event.message = message;
        event.status = status;
    }
    event.ackState = eventAckStateFromStatus(event.status);
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

void ParkingController::processIncomingMessage(const QString &message)
{
    const ParsedIncomingMessage parsed = IncomingMessageAdapter::parse(message);
    emit statusMessageChanged(QStringLiteral("Last RX: ") + parsed.raw);

    if (!parsed.isValid()) {
        recordEvent(QStringLiteral("SYSTEM"), QStringLiteral("RX_ERROR"),
                    parsed.errorMessage, QStringLiteral("REJECTED"));
        return;
    }

    const QString &slotId = parsed.sourceId;
    const QString &value = parsed.value;
    switch (parsed.kind) {
    case IncomingMessageKind::NormalizedEvent: {
        if (!isFireLifecycleEventType(parsed.eventType)) {
            recordEvent(slotId, parsed.eventType, parsed.message, parsed.status);
            return;
        }

        const QString channelId = normalizeCameraChannelId(slotId);
        if (channelId.isEmpty()) {
            recordEvent(slotId, parsed.eventType, parsed.message, parsed.status);
            return;
        }

        const QString eventType = parsed.eventType.trimmed().toUpper();
        if (eventType == QStringLiteral("FIRE_CLEARED")) {
            if (m_state.fireChannels.remove(channelId) > 0) {
                notifyStateChanged();
            }
        } else if (eventType != QStringLiteral("FIRE_ACKNOWLEDGED")
                   && eventType != QStringLiteral("FIRE_ALARM_ACK")) {
            if (!m_state.fireChannels.contains(channelId)) {
                m_state.fireChannels.insert(channelId);
                notifyStateChanged();
            }
        }
        recordEvent(channelId, eventType, parsed.message, parsed.status);
        return;
    }

    case IncomingMessageKind::ParkingSlot: {
        if (value == QStringLiteral("FIRE")
            || value == QStringLiteral("FIRE_SUSPECTED")) {
            recordEvent(QStringLiteral("SYSTEM"),
                        QStringLiteral("FIRE_CHANNEL_ERROR"),
                        QStringLiteral("Parking-slot messages cannot carry fire state: ")
                            + parsed.raw,
                        QStringLiteral("REJECTED"));
            return;
        }
        const SlotState state = slotStateFromText(value);
        applyParkingSlotUpdate(slotId, state);
        recordEvent(slotId, slotStateText(state),
                    QStringLiteral("Parking slot state updated from RX message"),
                    QStringLiteral("RECORDED"));
        return;
    }

    case IncomingMessageKind::HallSensor: {
        if (value == QStringLiteral("ERROR") || value == QStringLiteral("FAILED")) {
            applyParkingSlotUpdate(slotId, SlotState::SensorError);
            recordEvent(slotId, QStringLiteral("HALL_SENSOR_ERROR"),
                        QStringLiteral("Hall sensor error received"),
                        QStringLiteral("OPEN"));
            return;
        }
        if (value == QStringLiteral("ACKED")) {
            applyParkingSlotUpdate(slotId, SlotState::Acked);
            recordEvent(slotId, QStringLiteral("HALL_SENSOR_ACK"),
                        QStringLiteral("Hall sensor alarm acknowledged from RX message"),
                        QStringLiteral("ACKED"));
            return;
        }
        if (value == QStringLiteral("CLEAR")) {
            ParkingSlotInfo &slot = m_state.parkingSlots[slotId];
            slot.visual.alarm = SlotAlarmKind::None;
            slot.visual.alarmAcknowledged = false;
            slot.state = slot.visual.occupancy == SlotOccupancy::Occupied
                ? SlotState::Occupied : SlotState::Vacant;
            notifyStateChanged();
            recordEvent(slotId, QStringLiteral("HALL_SENSOR_CLEAR"),
                        QStringLiteral("Hall sensor alarm cleared from RX message"),
                        QStringLiteral("CLEARED"));
            return;
        }
        const SlotState state = slotStateFromText(value);
        applyParkingSlotUpdate(slotId, state);
        recordEvent(slotId, QStringLiteral("HALL_SENSOR_CHANGED"),
                    QStringLiteral("Hall sensor state updated from RX message"),
                    QStringLiteral("RECORDED"));
        return;
    }

    case IncomingMessageKind::EvAlert:
        if (value == QStringLiteral("NON_EV")) {
            applyEvSlotUpdate(slotId, SlotState::NonEvAlert,
                              QStringLiteral("UNKNOWN"), false,
                              QStringLiteral("00:00"), QStringLiteral("NON_EV_ALERT"));
            recordEvent(slotId, QStringLiteral("NON_EV_ALERT"),
                        QStringLiteral("Non-EV alert received"), QStringLiteral("OPEN"));
            return;
        }
        if (value == QStringLiteral("OVERTIME")) {
            applyEvSlotUpdate(slotId, SlotState::OvertimeAlert,
                              QStringLiteral("UNKNOWN"), true,
                              QStringLiteral("02:00+"), QStringLiteral("OVERTIME_ALERT"));
            recordEvent(slotId, QStringLiteral("OVERTIME_ALERT"),
                        QStringLiteral("Overtime alert received"), QStringLiteral("OPEN"));
            return;
        }
        if (value == QStringLiteral("ACKED")) {
            applyEvSlotUpdate(slotId, SlotState::Acked,
                              QStringLiteral("UNKNOWN"), true,
                              QStringLiteral("00:00"), QStringLiteral("ACKED"));
            recordEvent(slotId, QStringLiteral("ALARM_ACK"),
                        QStringLiteral("Alarm acknowledged from RX message"),
                        QStringLiteral("ACKED"));
            return;
        }
        if (value == QStringLiteral("CLEAR")) {
            EvSlotInfo &slot = m_state.evSlots[slotId];
            slot.visual.alarm = SlotAlarmKind::None;
            slot.visual.alarmAcknowledged = false;
            slot.state = slot.visual.occupancy == SlotOccupancy::Occupied
                ? SlotState::Occupied : SlotState::Vacant;
            slot.alarmText = QStringLiteral("NORMAL");
            notifyStateChanged();
            recordEvent(slotId, QStringLiteral("ALARM_CLEAR"), QStringLiteral("Alarm cleared from RX message"), QStringLiteral("CLEARED"));
            return;
        }
        break;

    case IncomingMessageKind::FireAlarm:
    {
        const QString channelId = normalizeCameraChannelId(slotId);
        if (channelId.isEmpty()) {
            recordEvent(QStringLiteral("SYSTEM"),
                        QStringLiteral("FIRE_CHANNEL_ERROR"),
                        QStringLiteral("Fire message requires CH1~CH4: ") + parsed.raw,
                        QStringLiteral("REJECTED"));
            return;
        }

        if (value == QStringLiteral("CLEAR")) {
            if (m_state.fireChannels.remove(channelId) > 0) {
                notifyStateChanged();
            }
            recordEvent(channelId, QStringLiteral("FIRE_CLEARED"),
                        QStringLiteral("Fire state cleared on ") + channelId,
                        QStringLiteral("CLEARED"));
            return;
        }
        if (value == QStringLiteral("ACKED")) {
            recordEvent(channelId, QStringLiteral("FIRE_ACKNOWLEDGED"),
                        QStringLiteral("Fire warning acknowledged on ") + channelId,
                        QStringLiteral("ACKED"));
            return;
        }

        if (!m_state.fireChannels.contains(channelId)) {
            m_state.fireChannels.insert(channelId);
            notifyStateChanged();
        }
        recordEvent(channelId, QStringLiteral("FIRE_SUSPECTED"),
                    QStringLiteral("Fire suspected on ") + channelId,
                    QStringLiteral("OPEN"));
        return;
    }

    case IncomingMessageKind::Unsupported:
        break;

    case IncomingMessageKind::Invalid:
        return;
    }

    recordEvent(slotId, QStringLiteral("RX_UNSUPPORTED"),
                QStringLiteral("Unsupported message: ") + parsed.raw,
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
