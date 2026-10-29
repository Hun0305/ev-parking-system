#include "controllers/parkingcontroller.h"
#include "services/notificationcenter.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMetaObject>
#include <QTemporaryDir>

#include <algorithm>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingController controller(
        directory.filePath(QStringLiteral("client_config.ini")),
        directory.filePath(QStringLiteral("client_config.local.ini")));
    NotificationCenter notifications;
    QList<MonitoringEvent> events;
    QString preparedFireAckTopic;
    QByteArray preparedFireAckPayload;
    int preparedFireAckCount = 0;
    QStringList fireConfirmationRequests;
    QStringList fireConfirmationRetries;
    QStringList fireConfirmationCloses;
    int stateChangeCount = 0;
    int notificationChangeCount = 0;

    QObject::connect(
        &controller, &ParkingController::eventLogged, &app,
        [&](const MonitoringEvent &event) {
            events.append(event);
            notifications.ingestEvent(event);
        });
    QObject::connect(
        &controller, &ParkingController::fireAcknowledgementCommandPrepared,
        &app, [&](const QString &topic, const QByteArray &payload) {
            preparedFireAckTopic = topic;
            preparedFireAckPayload = payload;
            ++preparedFireAckCount;
        });
    QObject::connect(
        &controller, &ParkingController::fireConfirmationRequested,
        &app, [&](const QString &channelId, const QString &alarmId) {
            fireConfirmationRequests.append(channelId + QLatin1Char('|') + alarmId);
        });
    QObject::connect(
        &controller, &ParkingController::fireConfirmationRetryRequested,
        &app, [&](const QString &channelId, const QString &alarmId) {
            fireConfirmationRetries.append(channelId + QLatin1Char('|') + alarmId);
        });
    QObject::connect(
        &controller, &ParkingController::fireConfirmationClosed,
        &app, [&](const QString &channelId, const QString &) {
            fireConfirmationCloses.append(channelId);
        });
    QObject::connect(
        &controller, &ParkingController::stateChanged, &app,
        [&]() { ++stateChangeCount; });
    QObject::connect(
        &notifications, &NotificationCenter::notificationsChanged, &app,
        [&]() { ++notificationChangeCount; });

    controller.applyManualJsonMessage(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("INVALID")}});
    if (events.size() != 1) return 3;
    if (events.constLast().id.isEmpty() || !events.constLast().occurredAt.isValid()) return 4;
    if (events.constLast().eventType != QStringLiteral("RX_UNSUPPORTED")) {
        qDebug() << "Test failed at 5, event type:" << events.constLast().eventType;
        return 5;
    }
    if (events.constLast().status != QStringLiteral("REJECTED")) return 6;
    if (events.constLast().ackState != EventAckState::None) return 7;
    if (notifications.hasNotifications()) return 8;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SLOT_OCCUPIED")},
        {QStringLiteral("slot_id"), QStringLiteral("P01")},
        {QStringLiteral("plate_number"), QStringLiteral("34C5678")},
        {QStringLiteral("parking_state"), QStringLiteral("OCCUPIED")}
    });
    if (controller.slotState(QStringLiteral("P-01")) != SlotState::Occupied) return 9;
    if (controller.plateNumber(QStringLiteral("P-01"))
        != QStringLiteral("34C5678")) return 129;
    if (events.constLast().sourceId != QStringLiteral("P-01")) return 10;
    if (events.constLast().eventType != QStringLiteral("SLOT_OCCUPIED")) return 11;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SENSOR_ERROR")},
        {QStringLiteral("slot_id"), QStringLiteral("P-03")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")},
        {QStringLiteral("message"), QStringLiteral("sensor, disconnected")}
    });
    if (events.constLast().sourceId != QStringLiteral("P-03")) return 12;
    if (events.constLast().message != QStringLiteral("sensor, disconnected")) return 13;
    if (events.constLast().ackState != EventAckState::Open) return 14;
    if (notifications.notifications().size() != 1) return 15;
    if (notifications.notifications().constFirst().severity != QStringLiteral("WARNING")) return 16;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SENSOR_RECOVERED")},
        {QStringLiteral("slot_id"), QStringLiteral("P-03")},
        {QStringLiteral("alarm_state"), QStringLiteral("CLEARED")},
        {QStringLiteral("message"), QStringLiteral("sensor recovered")}
    });
    if (events.constLast().ackState != EventAckState::Cleared) return 17;
    if (notifications.hasNotifications()) return 18;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("event_id"), QStringLiteral("ev-123")},
        {QStringLiteral("alarm_id"), QStringLiteral("al-123")},
        {QStringLiteral("channel_id"), QStringLiteral("CH2")},
        {QStringLiteral("alarm_kind"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")},
        {QStringLiteral("active"), true}
    });
    if (events.constLast().sourceId != QStringLiteral("CH2")) return 19;
    if (events.constLast().eventType != QStringLiteral("FIRE_SUSPECTED")) return 20;
    if (events.constLast().status != QStringLiteral("OPEN")) return 20;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH2"))) return 21;
    if (notifications.notifications().size() != 1) return 21;
    if (notifications.notifications().constFirst().severity != QStringLiteral("CRITICAL")) return 22;
    if (notifications.notifications().constFirst().sourceId
        != QStringLiteral("CH2")) return 23;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("FIRE_CLEARED")},
        {QStringLiteral("alarm_id"), QStringLiteral("al-123")},
        {QStringLiteral("channel_id"), QStringLiteral("CH2")},
        {QStringLiteral("alarm_state"), QStringLiteral("RESOLVED")},
        {QStringLiteral("ack_state"), QStringLiteral("resolved")},
        {QStringLiteral("active"), false}
    });
    if (events.constLast().eventType != QStringLiteral("FIRE_CLEARED")) {
        qDebug() << "Actual eventType:" << events.constLast().eventType;
        return 23;
    }
    if (events.constLast().status != QStringLiteral("CLEARED")) {
        qDebug() << "Actual status:" << events.constLast().status;
        return 24;
    }
    if (events.constLast().ackState != EventAckState::Cleared) return 25;
    if (!controller.state().fireChannels.isEmpty()) return 26;
    if (notifications.hasNotifications()) return 26;

    ParkingViewState liveState;
    EvSlotInfo fireSlot;
    fireSlot.slotId = QStringLiteral("EV-01");
    fireSlot.state = SlotState::Occupied;
    fireSlot.alarmText = QStringLiteral("NORMAL");
    fireSlot.visual.occupancy = SlotOccupancy::Occupied;
    fireSlot.visual.vehicleClass = VehicleClass::Electric;
    liveState.evSlots.insert(fireSlot.slotId, fireSlot);
    controller.replaceViewState(liveState);

    const QByteArray suspectedPayload = R"JSON({
        "event_id": "legacy-fire-01-open",
        "alarm_id": "legacy-alarm-01",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch01",
        "source_id": "fire_sensor_01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": true,
        "scope": "CAMERA_CHANNEL",
        "slot_id": "EV-01",
        "raw_payload": "FIRE:1"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/events/ch01")),
            Q_ARG(QByteArray, suspectedPayload))) return 27;
    if (events.constLast().eventType != QStringLiteral("FIRE_SUSPECTED")) return 28;
    if (events.constLast().status != QStringLiteral("OPEN")) return 29;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))) return 30;
    if (controller.state().evSlots.value(QStringLiteral("EV-01")).visual.alarm
        != SlotAlarmKind::None) return 30;
    if (notifications.notifications().size() != 1) return 31;
    const NotificationRecord mqttFire = notifications.notifications().constFirst();
    if (mqttFire.sourceId != QStringLiteral("CH1")) return 32;
    if (mqttFire.eventType != QStringLiteral("FIRE_SUSPECTED")) return 33;
    if (mqttFire.severity != QStringLiteral("CRITICAL")) return 34;
    if (mqttFire.title != QStringLiteral("Fire suspected")) return 35;

    const QByteArray clearedPayload = R"JSON({
        "event_id": "legacy-fire-01-cleared",
        "alarm_id": "legacy-alarm-01",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch01",
        "source_id": "fire_sensor_01",
        "alarm_kind": "NONE",
        "alarm_state": "RESOLVED",
        "ack_state": "resolved",
        "active": false,
        "scope": "CAMERA_CHANNEL",
        "slot_id": "EV-01",
        "raw_payload": "FIRE:0"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/events/ch01")),
            Q_ARG(QByteArray, clearedPayload))) return 36;
    if (events.constLast().eventType != QStringLiteral("FIRE_CLEARED")) return 37;
    if (events.constLast().status != QStringLiteral("CLEARED")) return 38;
    if (!controller.state().fireChannels.isEmpty()) return 39;
    if (notifications.hasNotifications()) return 39;
    const EvSlotInfo restoredSlot =
        controller.state().evSlots.value(QStringLiteral("EV-01"));
    if (restoredSlot.visual.alarm != SlotAlarmKind::None) return 40;
    if (restoredSlot.visual.occupancy != SlotOccupancy::Occupied) return 41;

    const QByteArray plateRecognizedPayload = R"JSON({
        "event_id": "session-7-plate",
        "event_type": "PLATE_RECOGNIZED",
        "session_id": 7,
        "slot_id": "EV01",
        "plate_number": "52A3108",
        "vehicle_type": "PHEV",
        "parking_state": "OCCUPIED",
        "occupied_seconds": 125,
        "alarm_state": "NONE",
        "timestamp": "2026-07-31T09:00:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/events/EV01")),
            Q_ARG(QByteArray, plateRecognizedPayload))) return 42;
    const EvSlotInfo recognized =
        controller.state().evSlots.value(QStringLiteral("EV-01"));
    if (recognized.state != SlotState::Occupied) return 43;
    if (recognized.plateNumber != QStringLiteral("52A3108")) return 44;
    if (!recognized.isEv
        || recognized.visual.vehicleClass != VehicleClass::Electric) return 45;
    if (events.constLast().id != QStringLiteral("session-7-plate")) return 46;
    if (events.constLast().eventType != QStringLiteral("PLATE_RECOGNIZED")) return 47;
    if (notifications.hasNotifications()) return 48;

    const int eventCountBeforeRetainedDuplicate = events.size();
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/state/EV01")),
            Q_ARG(QByteArray, plateRecognizedPayload))) return 49;
    if (events.size() != eventCountBeforeRetainedDuplicate) return 50;

    const QByteArray legacyNonEvPayload = R"JSON({
        "event_id": "session-7-non-ev",
        "event_type": "NON_EV_ALERT",
        "session_id": 7,
        "slot_id": "EV01",
        "plate_number": "12B3456",
        "vehicle_type": "NON_EV",
        "parking_state": "OCCUPIED",
        "alarm_state": "NONE",
        "timestamp": "2026-07-31T09:01:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/events/EV01")),
            Q_ARG(QByteArray, legacyNonEvPayload))) return 51;
    const EvSlotInfo nonEv =
        controller.state().evSlots.value(QStringLiteral("EV-01"));
    if (nonEv.state != SlotState::NonEvAlert) return 52;
    if (nonEv.visual.alarm != SlotAlarmKind::NonEvViolation) return 53;
    if (nonEv.visual.vehicleClass != VehicleClass::General) return 54;
    if (events.constLast().eventType != QStringLiteral("NON_EV_ALERT")) return 55;
    if (events.constLast().status != QStringLiteral("OPEN")) return 56;
    if (notifications.notifications().size() != 1) return 57;
    if (notifications.notifications().constFirst().eventId
        != QStringLiteral("session-7-non-ev")) return 131;

    const QByteArray overstayPayload = R"JSON({
        "event_id": "session-8-overstay",
        "event_type": "OVERTIME_VIOLATION",
        "session_id": 8,
        "slot_id": "EV02",
        "plate_number": "34C7788",
        "vehicle_type": "EV",
        "parking_state": "OCCUPIED",
        "occupied_seconds": 3600,
        "alarm_kind": "OVERSTAY",
        "alarm_state": "OPEN",
        "timestamp": "2026-07-31T10:00:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/state/EV02")),
            Q_ARG(QByteArray, overstayPayload))) return 58;
    const EvSlotInfo overstay =
        controller.state().evSlots.value(QStringLiteral("EV-02"));
    if (overstay.state != SlotState::OvertimeAlert) return 59;
    if (overstay.visual.alarm != SlotAlarmKind::Overstay) return 60;
    if (events.constLast().eventType != QStringLiteral("OVERTIME_ALERT")) return 61;
    if (notifications.notifications().size() != 2) return 62;

    const QByteArray firstVacatedPayload = R"JSON({
        "event_id": "session-7-vacated",
        "event_type": "SLOT_VACATED",
        "session_id": 7,
        "slot_id": "EV01",
        "parking_state": "VACANT",
        "alarm_state": "NONE",
        "timestamp": "2026-07-31T10:01:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/state/EV01")),
            Q_ARG(QByteArray, firstVacatedPayload))) return 63;
    if (controller.slotState(QStringLiteral("EV-01")) != SlotState::Vacant) return 64;
    if (notifications.notifications().size() != 1) return 65;

    const QByteArray secondVacatedPayload = R"JSON({
        "event_id": "session-8-vacated",
        "event_type": "SLOT_VACATED",
        "session_id": 8,
        "slot_id": "EV02",
        "parking_state": "VACANT",
        "alarm_state": "NONE",
        "timestamp": "2026-07-31T10:02:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/state/EV02")),
            Q_ARG(QByteArray, secondVacatedPayload))) return 66;
    if (controller.slotState(QStringLiteral("EV-02")) != SlotState::Vacant) return 67;
    if (notifications.hasNotifications()) return 68;
    if (controller.state().evSlots.size() != 2) return 69;

    const QJsonDocument oneSlotSnapshot = QJsonDocument::fromJson(R"JSON({
        "items": [
            {
                "slot_id": "EV01",
                "parking_status": "OCCUPIED",
                "active_session": {
                    "session_id": 9,
                    "plate_number": "52A3108",
                    "ev_status": "PHEV",
                    "entry_time": "2026-07-31T09:00:00+09:00"
                }
            }
        ]
    })JSON");
    if (!QMetaObject::invokeMethod(
            &controller, "applyParkingSnapshot", Qt::DirectConnection,
            Q_ARG(QJsonDocument, oneSlotSnapshot))) return 70;
    if (controller.state().evSlots.size() != 1) return 71;
    if (!controller.state().parkingSlots.isEmpty()) return 72;
    if (controller.state().evSlots.contains(QStringLiteral("EV-02"))) return 73;
    if (!controller.state().hasServerSnapshot
        || controller.state().serverSlotCount != 1) return 74;
    const EvSlotInfo snapshotSlot =
        controller.state().evSlots.value(QStringLiteral("EV-01"));
    if (snapshotSlot.visual.occupancy != SlotOccupancy::Occupied) return 75;
    if (snapshotSlot.visual.vehicleClass != VehicleClass::Electric) return 76;
    controller.applyParkingSlotUpdate(QStringLiteral("P-99"), SlotState::Vacant);
    if (controller.state().serverSlotCount != 1) return 77;

    const auto apiSyncCount = [&events]() {
        return std::count_if(
            events.cbegin(), events.cend(), [](const MonitoringEvent &event) {
                return event.eventType == QStringLiteral("API_SYNC");
            });
    };
    if (apiSyncCount() != 1) return 208;
    if (!QMetaObject::invokeMethod(
            &controller, "applyParkingSnapshot", Qt::DirectConnection,
            Q_ARG(QJsonDocument, oneSlotSnapshot))) return 209;
    if (apiSyncCount() != 1) return 210;

    const QByteArray systemErrorPayload = R"JSON({
        "event_id": "system-uart-disconnected",
        "event_type": "SENSOR_ERROR",
        "alarm_kind": "SENSOR_ERROR",
        "alarm_state": "OPEN",
        "error_code": "UART_DISCONNECTED",
        "scope": "SYSTEM",
        "severity": "ERROR",
        "message": "UART disconnected",
        "timestamp": "2026-07-31T10:03:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/events/system")),
            Q_ARG(QByteArray, systemErrorPayload))) return 76;
    if (events.constLast().sourceId != QStringLiteral("SYSTEM")) return 77;
    if (events.constLast().eventType != QStringLiteral("HALL_SENSOR_ERROR")) return 78;
    if (events.constLast().status != QStringLiteral("OPEN")) return 79;
    if (!notifications.hasNotifications()) return 80;

    const QByteArray systemRecoveredPayload = R"JSON({
        "event_id": "system-uart-recovered",
        "event_type": "SENSOR_RECOVERED",
        "alarm_kind": "NONE",
        "alarm_state": "CLEARED",
        "error_code": "UART_RECOVERED",
        "scope": "SYSTEM",
        "severity": "INFO",
        "message": "UART recovered",
        "timestamp": "2026-07-31T10:04:00+09:00"
    })JSON";
    if (!QMetaObject::invokeMethod(
            &controller, "handleMqttMessage", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("parking/v1/state/system")),
            Q_ARG(QByteArray, systemRecoveredPayload))) return 81;
    if (events.constLast().eventType != QStringLiteral("HALL_SENSOR_CLEAR")) return 82;
    if (events.constLast().status != QStringLiteral("CLEARED")) return 83;
    if (notifications.hasNotifications()) return 84;

    const auto deliverMqtt = [&](const QString &topic,
                                 const QByteArray &payload,
                                 const bool retained) {
        return QMetaObject::invokeMethod(
            &controller, "handleMqttMessageWithMetadata", Qt::DirectConnection,
            Q_ARG(QString, topic), Q_ARG(QByteArray, payload),
            Q_ARG(bool, retained));
    };

    const QByteArray channelOneFirePayload = R"JSON({
        "event_id": "fire-FLAME01-9201",
        "alarm_id": "alarm-FLAME01-9201",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch01",
        "source_id": "FLAME01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "severity": "critical",
        "active": true,
        "scope": "CAMERA_CHANNEL",
        "zone_id": "",
        "slot_id": ""
    })JSON";
    const int eventCountBeforeChannelFire = events.size();
    const int confirmationCountBeforeChannelFire = fireConfirmationRequests.size();
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     channelOneFirePayload, false)) return 85;
    if (controller.state().fireChannels
        != QSet<QString>{QStringLiteral("CH1")}) return 86;
    if (controller.state().evSlots.value(QStringLiteral("EV-01")).visual.alarm
        != SlotAlarmKind::None) return 87;
    if (events.size() != eventCountBeforeChannelFire + 1) return 88;
    if (events.constLast().sourceId != QStringLiteral("CH1")) return 89;
    if (events.constLast().eventType != QStringLiteral("FIRE_SUSPECTED")) return 90;
    if (notifications.notifications().size() != 1) return 91;
    if (fireConfirmationRequests.size() != confirmationCountBeforeChannelFire + 1
        || fireConfirmationRequests.constLast()
            != QStringLiteral("CH1|alarm-FLAME01-9201")) return 140;

    const int eventCountBeforeFireStateDuplicate = events.size();
    if (!deliverMqtt(QStringLiteral("parking/fire/ch01"),
                     channelOneFirePayload, true)) return 92;
    if (events.size() != eventCountBeforeFireStateDuplicate) return 93;
    if (notifications.notifications().size() != 1) return 94;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))) return 95;
    if (fireConfirmationRequests.size()
        != confirmationCountBeforeChannelFire + 1) return 141;
    const ChannelFireAlarmState openFire =
        controller.state().fireAlarms.value(QStringLiteral("CH1"));
    if (!openFire.active || openFire.acknowledged
        || openFire.alarmId != QStringLiteral("alarm-FLAME01-9201")) return 129;

    controller.acknowledgeFireAlarm(QStringLiteral("CH1"));
    if (preparedFireAckTopic
        != QStringLiteral("parking/v1/commands/fire/ch01")) return 130;
    const QJsonObject preparedCommand =
        QJsonDocument::fromJson(preparedFireAckPayload).object();
    if (preparedCommand.value(QStringLiteral("command")).toString()
            != QStringLiteral("ALARM_ACK")
        || preparedCommand.value(QStringLiteral("channel_id")).toString()
            != QStringLiteral("ch01")
        || preparedCommand.value(QStringLiteral("alarm_id")).toString()
            != QStringLiteral("alarm-FLAME01-9201")) return 131;
    if (fireConfirmationRetries
            != QStringList{QStringLiteral("CH1|alarm-FLAME01-9201")}) {
        return 154;
    }
    controller.acknowledgeFireAlarm(QStringLiteral("CH1"));
    if (preparedFireAckCount != 2
        || fireConfirmationRetries.size() != 2) return 142;

    const QByteArray mismatchedAckPayload = R"JSON({
        "event_id": "fire-FLAME01-wrong-ack",
        "alarm_id": "alarm-FLAME01-stale",
        "event_type": "FIRE_ACKNOWLEDGED",
        "channel_id": "ch01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "ACKNOWLEDGED",
        "ack_state": "acknowledged",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     mismatchedAckPayload, false)) return 137;
    if (controller.state().fireAlarms.value(QStringLiteral("CH1"))
            .acknowledged) return 138;
    if (events.constLast().eventType
        != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 139;

    const QByteArray channelOneAckPayload = R"JSON({
        "event_id": "fire-FLAME01-ack-9201",
        "alarm_id": "alarm-FLAME01-9201",
        "event_type": "FIRE_ACKNOWLEDGED",
        "channel_id": "ch01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "ACKNOWLEDGED",
        "ack_state": "acknowledged",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     channelOneAckPayload, false)) return 132;
    const ChannelFireAlarmState acknowledgedFire =
        controller.state().fireAlarms.value(QStringLiteral("CH1"));
    if (!acknowledgedFire.active || !acknowledgedFire.acknowledged
        || !controller.state().fireChannels.contains(QStringLiteral("CH1"))) {
        return 133;
    }
    if (events.constLast().eventType != QStringLiteral("FIRE_ACKNOWLEDGED")
        || events.constLast().ackState != EventAckState::Acknowledged) return 134;
    if (fireConfirmationCloses.isEmpty()
        || fireConfirmationCloses.constLast() != QStringLiteral("CH1")) return 143;
    const int eventCountAfterAck = events.size();
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     channelOneAckPayload, false)) return 146;
    if (events.size() != eventCountAfterAck
        || !controller.state().fireAlarms.value(QStringLiteral("CH1"))
                .acknowledged) return 147;

    if (!deliverMqtt(QStringLiteral("parking/fire/ch01"),
                     channelOneFirePayload, true)) return 135;
    if (!controller.state().fireAlarms.value(QStringLiteral("CH1"))
             .acknowledged) return 136;

    const QByteArray staleChannelClearPayload = R"JSON({
        "event_id": "fire-FLAME01-stale-clear",
        "alarm_id": "alarm-FLAME01-stale",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch01",
        "alarm_kind": "NONE",
        "alarm_state": "RESOLVED",
        "ack_state": "resolved",
        "active": false,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     staleChannelClearPayload, false)) return 155;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))
        || controller.state().fireAlarms.value(QStringLiteral("CH1")).alarmId
            != QStringLiteral("alarm-FLAME01-9201")
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 156;

    const QByteArray inactiveChannelClearPayload = R"JSON({
        "event_id": "fire-FLAME02-9202",
        "alarm_id": "alarm-FLAME02-9202",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch02",
        "alarm_kind": "NONE",
        "alarm": "NONE",
        "alarm_state": "RESOLVED",
        "ack_state": "resolved",
        "active": false,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/fire/ch02"),
                     inactiveChannelClearPayload, true)) return 96;
    if (controller.state().fireChannels
        != QSet<QString>{QStringLiteral("CH1")}) return 97;

    const QByteArray channelOneClearPayload = R"JSON({
        "event_id": "fire-FLAME01-9205",
        "alarm_id": "alarm-FLAME01-9201",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch01",
        "source_id": "FLAME01",
        "alarm_kind": "NONE",
        "alarm": "NONE",
        "alarm_state": "RESOLVED",
        "ack_state": "resolved",
        "severity": "info",
        "active": false,
        "scope": "CAMERA_CHANNEL",
        "zone_id": "",
        "slot_id": ""
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     channelOneClearPayload, false)) return 98;
    if (!controller.state().fireChannels.isEmpty()) return 99;
    if (notifications.hasNotifications()) return 100;

    const QByteArray retainedRestorePayload = R"JSON({
        "event_id": "fire-FLAME01-9301",
        "alarm_id": "alarm-FLAME01-9301",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    const int eventCountBeforeRestore = events.size();
    const int confirmationCountBeforeRestore = fireConfirmationRequests.size();
    if (!deliverMqtt(QStringLiteral("parking/fire/ch01"),
                     retainedRestorePayload, true)) return 101;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))) return 102;
    if (events.size() != eventCountBeforeRestore) return 103;
    if (notifications.hasNotifications()) return 104;
    if (fireConfirmationRequests.size() != confirmationCountBeforeRestore + 1
        || fireConfirmationRequests.constLast()
            != QStringLiteral("CH1|alarm-FLAME01-9301")) return 144;

    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     retainedRestorePayload, false)) return 105;
    if (events.size() != eventCountBeforeRestore + 1) return 106;
    if (notifications.notifications().size() != 1) return 107;
    if (fireConfirmationRequests.size()
        != confirmationCountBeforeRestore + 1) return 145;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch01"),
                     retainedRestorePayload, false)) return 108;
    if (events.size() != eventCountBeforeRestore + 1) return 109;
    if (notifications.notifications().size() != 1) return 110;

    const QByteArray nonEvCannotBecomeFirePayload = R"JSON({
        "event_id": "session-10-non-ev",
        "event_type": "NON_EV_ALERT",
        "session_id": 10,
        "slot_id": "EV01",
        "vehicle_type": "NON_EV",
        "parking_state": "OCCUPIED",
        "alarm_kind": "NON_EV",
        "alarm_state": "OPEN",
        "active": true
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/EV01"),
                     nonEvCannotBecomeFirePayload, false)) return 111;
    if (controller.state().fireChannels
        != QSet<QString>{QStringLiteral("CH1")}) return 112;
    if (controller.state().evSlots.value(QStringLiteral("EV-01")).visual.alarm
        != SlotAlarmKind::NonEvViolation) return 113;

    const QByteArray stateTopicFirePayload = R"JSON({
        "event_id": "fire-FLAME01-invalid-topic",
        "alarm_id": "alarm-FLAME01-9301",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch01",
        "alarm_kind": "NONE",
        "alarm_state": "RESOLVED",
        "ack_state": "resolved",
        "active": false,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/state/EV01"),
                     stateTopicFirePayload, true)) return 114;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))) return 115;
    if (events.constLast().eventType
        != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 116;

    const QByteArray incompleteClearPayload = R"JSON({
        "event_id": "fire-FLAME01-incomplete-clear",
        "event_type": "FIRE_CLEARED",
        "channel_id": "ch01",
        "alarm_kind": "NONE",
        "alarm_state": "RESOLVED",
        "active": false,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/fire/ch01"),
                     incompleteClearPayload, true)) return 148;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH1"))
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 149;

    const QByteArray missingChannelPayload = R"JSON({
        "event_id": "fire-FLAME01-missing-channel",
        "alarm_id": "alarm-FLAME01-missing-channel",
        "event_type": "FIRE_SUSPECTED",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    const int confirmationsBeforeMissingChannel =
        fireConfirmationRequests.size();
    if (!deliverMqtt(QStringLiteral("parking/fire/ch01"),
                     missingChannelPayload, true)) return 150;
    if (controller.state().fireChannels
            != QSet<QString>{QStringLiteral("CH1")}
        || fireConfirmationRequests.size()
            != confirmationsBeforeMissingChannel
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 151;

    const QByteArray mismatchedTopicChannelPayload = R"JSON({
        "event_id": "fire-FLAME01-topic-mismatch",
        "alarm_id": "alarm-FLAME01-topic-mismatch",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch01",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/fire/ch02"),
                     mismatchedTopicChannelPayload, true)) return 152;
    if (controller.state().fireChannels
            != QSet<QString>{QStringLiteral("CH1")}
        || fireConfirmationRequests.size()
            != confirmationsBeforeMissingChannel
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 153;

    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch02"),
                     mismatchedTopicChannelPayload, false)) return 157;
    if (controller.state().fireChannels
            != QSet<QString>{QStringLiteral("CH1")}
        || controller.state().fireAlarms.value(QStringLiteral("CH1")).alarmId
            != QStringLiteral("alarm-FLAME01-9301")
        || fireConfirmationRequests.size()
            != confirmationsBeforeMissingChannel
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 158;

    const QByteArray unknownChannelPayload = R"JSON({
        "event_id": "fire-FLAME09-9401",
        "alarm_id": "alarm-FLAME09-9401",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch09",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": true,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch09"),
                     unknownChannelPayload, false)) return 117;
    if (controller.state().fireChannels
        != QSet<QString>{QStringLiteral("CH1")}) return 118;
    if (events.constLast().status != QStringLiteral("REJECTED")) return 119;

    const QByteArray contradictoryPayload = R"JSON({
        "event_id": "fire-FLAME02-9402",
        "alarm_id": "alarm-FLAME02-9402",
        "event_type": "FIRE_SUSPECTED",
        "channel_id": "ch02",
        "alarm_kind": "FIRE_SUSPECTED",
        "alarm_state": "OPEN",
        "active": false,
        "scope": "CAMERA_CHANNEL"
    })JSON";
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch02"),
                     contradictoryPayload, false)) return 120;
    if (controller.state().fireChannels.contains(QStringLiteral("CH2"))) return 121;
    if (events.constLast().eventType
        != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 122;

    notifications.clearAll();
    const QSet<QString> fireChannelsBeforeRejectedSlotFire =
        controller.state().fireChannels;
    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("slot_id"), QStringLiteral("EV-04")}
    });
    if (events.constLast().sourceId != QStringLiteral("SYSTEM")) return 123;
    if (events.constLast().eventType
        != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 124;
    if (controller.state().fireChannels
        != fireChannelsBeforeRejectedSlotFire) return 125;

    controller.applyManualJsonMessage(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("slot_id"), QStringLiteral("P-16")}
    });
    if (events.constLast().sourceId != QStringLiteral("SYSTEM")) return 126;
    if (events.constLast().eventType
        != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 127;
    if (controller.state().parkingSlots.contains(QStringLiteral("P-16"))) return 128;

    const auto revisionedFirePayload = [](
        const QString &channelId, const QString &eventId,
        const QString &deliveryId, const QString &alarmId,
        const quint64 revision, const QString &eventType,
        const QString &alarmKind, const QString &alarmState,
        const QString &ackState, const bool active,
        const QString &message = QString()) {
        QJsonObject object{
            {QStringLiteral("event_id"), eventId},
            {QStringLiteral("delivery_id"), deliveryId},
            {QStringLiteral("alarm_id"), alarmId},
            {QStringLiteral("fire_revision"), static_cast<double>(revision)},
            {QStringLiteral("event_type"), eventType},
            {QStringLiteral("channel_id"), channelId},
            {QStringLiteral("source_id"), QStringLiteral("FIRE-") + channelId},
            {QStringLiteral("alarm_kind"), alarmKind},
            {QStringLiteral("alarm_state"), alarmState},
            {QStringLiteral("ack_state"), ackState},
            {QStringLiteral("active"), active},
            {QStringLiteral("scope"), QStringLiteral("CAMERA_CHANNEL")}
        };
        if (!message.isEmpty()) {
            object.insert(QStringLiteral("message"), message);
        }
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    };

    const QByteArray revisionedOpenEvent = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-1"),
        QStringLiteral("event-ch3-open-1"), QStringLiteral("alarm-ch3-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    const int statesBeforeRevisionedOpen = stateChangeCount;
    const int eventsBeforeRevisionedOpen = events.size();
    const int confirmationsBeforeRevisionedOpen = fireConfirmationRequests.size();
    const int notificationsBeforeRevisionedOpen = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedOpenEvent, false)) return 159;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesBeforeRevisionedOpen + 1
        || events.size() != eventsBeforeRevisionedOpen + 1
        || fireConfirmationRequests.size()
            != confirmationsBeforeRevisionedOpen + 1
        || notificationChangeCount != notificationsBeforeRevisionedOpen + 1
        || notifications.notifications().size() != 1) return 160;

    const QByteArray revisionedOpenState = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-1"),
        QStringLiteral("state-ch3-open-1"), QStringLiteral("alarm-ch3-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    const int statesBeforeCrossSinkReplay = stateChangeCount;
    const int eventsBeforeCrossSinkReplay = events.size();
    const int confirmationsBeforeCrossSinkReplay = fireConfirmationRequests.size();
    const int notificationsBeforeCrossSinkReplay = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"),
                     revisionedOpenState, true)) return 161;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedOpenEvent, false)) return 162;
    if (stateChangeCount != statesBeforeCrossSinkReplay
        || events.size() != eventsBeforeCrossSinkReplay
        || fireConfirmationRequests.size() != confirmationsBeforeCrossSinkReplay
        || notificationChangeCount != notificationsBeforeCrossSinkReplay
        || notifications.notifications().size() != 1) return 163;

    const QByteArray changedDeliveryPayload = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-1"),
        QStringLiteral("event-ch3-open-1"), QStringLiteral("alarm-ch3-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true, QStringLiteral("changed payload"));
    const int statesBeforeDeliveryConflict = stateChangeCount;
    const int confirmationsBeforeDeliveryConflict = fireConfirmationRequests.size();
    const int notificationsBeforeDeliveryConflict = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     changedDeliveryPayload, false)) return 164;
    if (stateChangeCount != statesBeforeDeliveryConflict
        || fireConfirmationRequests.size() != confirmationsBeforeDeliveryConflict
        || notificationChangeCount != notificationsBeforeDeliveryConflict
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 165;

    const QByteArray sameRevisionConflict = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-conflict"),
        QStringLiteral("event-ch3-open-conflict"),
        QStringLiteral("alarm-ch3-conflict"), 1,
        QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     sameRevisionConflict, false)) return 166;
    if (stateChangeCount != statesBeforeDeliveryConflict
        || fireConfirmationRequests.size() != confirmationsBeforeDeliveryConflict
        || notificationChangeCount != notificationsBeforeDeliveryConflict
        || controller.state().fireAlarms.value(QStringLiteral("CH3")).alarmId
            != QStringLiteral("alarm-ch3-a")) return 167;

    const QByteArray revisionedAck = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-ack-2"),
        QStringLiteral("event-ch3-ack-2"), QStringLiteral("alarm-ch3-a"),
        2, QStringLiteral("FIRE_ACKNOWLEDGED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("ACKNOWLEDGED"),
        QStringLiteral("acknowledged"), true);
    const int closesBeforeAck = fireConfirmationCloses.size();
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedAck, false)) return 168;
    if (!controller.state().fireAlarms.value(QStringLiteral("CH3")).acknowledged
        || fireConfirmationCloses.size() != closesBeforeAck + 1
        || notifications.hasNotifications()) return 169;
    const int statesAfterAck = stateChangeCount;
    const int eventsAfterAck = events.size();
    const int closesAfterAck = fireConfirmationCloses.size();
    const int notificationChangesAfterAck = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedAck, false)) return 170;
    if (stateChangeCount != statesAfterAck || events.size() != eventsAfterAck
        || fireConfirmationCloses.size() != closesAfterAck
        || notificationChangeCount != notificationChangesAfterAck) return 171;

    const QByteArray revisionedClear = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-clear-3"),
        QStringLiteral("event-ch3-clear-3"), QStringLiteral("alarm-ch3-a"),
        3, QStringLiteral("FIRE_CLEARED"), QStringLiteral("NONE"),
        QStringLiteral("RESOLVED"), QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedClear, false)) return 172;
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))) return 173;
    const int statesAfterClear = stateChangeCount;
    const int eventsAfterClear = events.size();
    const int closesAfterClear = fireConfirmationCloses.size();
    const int confirmationsAfterClear = fireConfirmationRequests.size();
    const int notificationChangesAfterClear = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedClear, false)) return 174;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     revisionedOpenEvent, false)) return 175;
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"),
                     revisionedOpenState, true)) return 176;
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesAfterClear
        || events.size() != eventsAfterClear
        || fireConfirmationCloses.size() != closesAfterClear
        || fireConfirmationRequests.size() != confirmationsAfterClear
        || notificationChangeCount != notificationChangesAfterClear) return 177;

    QJsonObject revisionlessAfterV2 =
        QJsonDocument::fromJson(revisionedOpenEvent).object();
    revisionlessAfterV2.remove(QStringLiteral("fire_revision"));
    revisionlessAfterV2.remove(QStringLiteral("delivery_id"));
    const int statesBeforeInvalidRevisions = stateChangeCount;
    const int confirmationsBeforeInvalidRevisions = fireConfirmationRequests.size();
    const int closesBeforeInvalidRevisions = fireConfirmationCloses.size();
    const int notificationsBeforeInvalidRevisions = notificationChangeCount;
    if (!deliverMqtt(
            QStringLiteral("parking/v1/events/ch03"),
            QJsonDocument(revisionlessAfterV2).toJson(QJsonDocument::Compact),
            false)) return 178;
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesBeforeInvalidRevisions
        || fireConfirmationRequests.size() != confirmationsBeforeInvalidRevisions
        || fireConfirmationCloses.size() != closesBeforeInvalidRevisions
        || notificationChangeCount != notificationsBeforeInvalidRevisions
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 179;

    const QList<QJsonValue> invalidRevisions{
        QJsonValue(0), QJsonValue(-1), QJsonValue(1.5),
        QJsonValue(QStringLiteral("4")), QJsonValue(9007199254740992.0)};
    for (const QJsonValue &invalidRevision : invalidRevisions) {
        QJsonObject invalid = QJsonDocument::fromJson(revisionedOpenEvent).object();
        invalid.insert(QStringLiteral("fire_revision"), invalidRevision);
        if (!deliverMqtt(
                QStringLiteral("parking/v1/events/ch03"),
                QJsonDocument(invalid).toJson(QJsonDocument::Compact),
                false)) return 180;
    }
    const QStringList requiredV2Ids{
        QStringLiteral("delivery_id"), QStringLiteral("event_id"),
        QStringLiteral("alarm_id")};
    for (const QString &field : requiredV2Ids) {
        QJsonObject invalid = QJsonDocument::fromJson(revisionedOpenEvent).object();
        invalid.insert(field, QString());
        if (!deliverMqtt(
                QStringLiteral("parking/v1/events/ch03"),
                QJsonDocument(invalid).toJson(QJsonDocument::Compact),
                false)) return 181;
    }
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesBeforeInvalidRevisions
        || fireConfirmationRequests.size() != confirmationsBeforeInvalidRevisions
        || fireConfirmationCloses.size() != closesBeforeInvalidRevisions
        || notificationChangeCount != notificationsBeforeInvalidRevisions) return 182;

    const QByteArray reusedEventId = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-clear-3"),
        QStringLiteral("event-ch3-reused-id"), QStringLiteral("alarm-ch3-b"),
        4, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     reusedEventId, false)) return 200;
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesBeforeInvalidRevisions
        || fireConfirmationRequests.size() != confirmationsBeforeInvalidRevisions
        || notificationChangeCount != notificationsBeforeInvalidRevisions
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 201;

    const QByteArray reusedDeliveryId = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-4"),
        QStringLiteral("state-ch3-open-1"), QStringLiteral("alarm-ch3-b"),
        4, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"),
                     reusedDeliveryId, true)) return 202;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || stateChangeCount != statesBeforeInvalidRevisions + 1
        || fireConfirmationRequests.size()
            != confirmationsBeforeInvalidRevisions + 1
        || notificationChangeCount != notificationsBeforeInvalidRevisions
        || controller.state().fireAlarms.value(QStringLiteral("CH3")).alarmId
            != QStringLiteral("alarm-ch3-b")) return 203;

    const QByteArray skippedOpen = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-5"),
        QStringLiteral("state-ch3-open-5"), QStringLiteral("alarm-ch3-b"),
        5, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"), skippedOpen, true)) {
        return 183;
    }
    if (!controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || controller.state().fireAlarms.value(QStringLiteral("CH3")).alarmId
            != QStringLiteral("alarm-ch3-b")) return 184;
    const int confirmationsAfterSkippedOpen = fireConfirmationRequests.size();

    const QByteArray authoritativeMismatchedClear = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-clear-7"),
        QStringLiteral("state-ch3-clear-7"), QStringLiteral("alarm-ch3-c"),
        7, QStringLiteral("FIRE_CLEARED"), QStringLiteral("NONE"),
        QStringLiteral("RESOLVED"), QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"),
                     authoritativeMismatchedClear, true)) return 185;
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || fireConfirmationRequests.size() != confirmationsAfterSkippedOpen) {
        return 186;
    }

    const QByteArray newestOpen = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-open-8"),
        QStringLiteral("state-ch3-open-8"), QStringLiteral("alarm-ch3-d"),
        8, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"), newestOpen, true)) {
        return 187;
    }
    const int closesBeforeStaleClear = fireConfirmationCloses.size();
    const QByteArray staleClear = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-clear-stale"),
        QStringLiteral("event-ch3-clear-stale"), QStringLiteral("alarm-ch3-d"),
        7, QStringLiteral("FIRE_CLEARED"), QStringLiteral("NONE"),
        QStringLiteral("RESOLVED"), QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch03"),
                     staleClear, false)) return 188;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH3"))
        || fireConfirmationCloses.size() != closesBeforeStaleClear) return 189;

    const QByteArray newestClear = revisionedFirePayload(
        QStringLiteral("ch03"), QStringLiteral("fire-ch3-clear-9"),
        QStringLiteral("state-ch3-clear-9"), QStringLiteral("alarm-ch3-d"),
        9, QStringLiteral("FIRE_CLEARED"), QStringLiteral("NONE"),
        QStringLiteral("RESOLVED"), QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch03"), newestClear, true)) {
        return 190;
    }
    if (controller.state().fireChannels.contains(QStringLiteral("CH3"))) return 191;

    notifications.clearAll();
    const QByteArray channelFourState = revisionedFirePayload(
        QStringLiteral("ch04"), QStringLiteral("fire-ch4-open-1"),
        QStringLiteral("state-ch4-open-1"), QStringLiteral("alarm-ch4-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    const int statesBeforeChannelFour = stateChangeCount;
    const int confirmationsBeforeChannelFour = fireConfirmationRequests.size();
    if (!deliverMqtt(QStringLiteral("parking/fire/ch04"),
                     channelFourState, true)) return 192;
    if (!controller.state().fireChannels.contains(QStringLiteral("CH4"))
        || stateChangeCount != statesBeforeChannelFour + 1
        || fireConfirmationRequests.size() != confirmationsBeforeChannelFour + 1
        || notifications.hasNotifications()) return 193;

    const QByteArray channelFourEvent = revisionedFirePayload(
        QStringLiteral("ch04"), QStringLiteral("fire-ch4-open-1"),
        QStringLiteral("event-ch4-open-1"), QStringLiteral("alarm-ch4-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    const int statesBeforeChannelFourHistory = stateChangeCount;
    const int confirmationsBeforeChannelFourHistory = fireConfirmationRequests.size();
    const int notificationsBeforeChannelFourHistory = notificationChangeCount;
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch04"),
                     channelFourEvent, false)) return 194;
    if (stateChangeCount != statesBeforeChannelFourHistory
        || fireConfirmationRequests.size() != confirmationsBeforeChannelFourHistory
        || notificationChangeCount != notificationsBeforeChannelFourHistory + 1
        || notifications.notifications().size() != 1
        || notifications.notifications().constFirst().eventId
            != QStringLiteral("fire-ch4-open-1")) return 195;
    const int eventsAfterChannelFourHistory = events.size();
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch04"),
                     channelFourEvent, false)) return 196;
    if (events.size() != eventsAfterChannelFourHistory
        || stateChangeCount != statesBeforeChannelFourHistory
        || notificationChangeCount != notificationsBeforeChannelFourHistory + 1) {
        return 197;
    }

    const QByteArray changedChannelFourDelivery = revisionedFirePayload(
        QStringLiteral("ch04"), QStringLiteral("fire-ch4-open-1"),
        QStringLiteral("event-ch4-open-other"), QStringLiteral("alarm-ch4-a"),
        1, QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FIRE_SUSPECTED"), QStringLiteral("OPEN"),
        QStringLiteral("unacked"), true);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch04"),
                     changedChannelFourDelivery, false)) return 198;
    if (stateChangeCount != statesBeforeChannelFourHistory
        || notificationChangeCount != notificationsBeforeChannelFourHistory + 1
        || fireConfirmationRequests.size() != confirmationsBeforeChannelFourHistory
        || events.constLast().eventType
            != QStringLiteral("MQTT_FIRE_CONTRACT_ERROR")) return 199;

    constexpr quint64 kMaximumExactFireRevision = 9007199254740991ULL;
    const QByteArray maximumRevisionClearState = revisionedFirePayload(
        QStringLiteral("ch04"), QStringLiteral("fire-ch4-clear-max"),
        QStringLiteral("state-ch4-clear-max"), QStringLiteral("alarm-ch4-a"),
        kMaximumExactFireRevision, QStringLiteral("FIRE_CLEARED"),
        QStringLiteral("NONE"), QStringLiteral("RESOLVED"),
        QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/fire/ch04"),
                     maximumRevisionClearState, true)) return 204;
    if (controller.state().fireChannels.contains(QStringLiteral("CH4"))) return 205;
    const int statesAfterMaximumRevision = stateChangeCount;
    const QByteArray maximumRevisionClearEvent = revisionedFirePayload(
        QStringLiteral("ch04"), QStringLiteral("fire-ch4-clear-max"),
        QStringLiteral("event-ch4-clear-max"), QStringLiteral("alarm-ch4-a"),
        kMaximumExactFireRevision, QStringLiteral("FIRE_CLEARED"),
        QStringLiteral("NONE"), QStringLiteral("RESOLVED"),
        QStringLiteral("resolved"), false);
    if (!deliverMqtt(QStringLiteral("parking/v1/events/ch04"),
                     maximumRevisionClearEvent, false)) return 206;
    if (stateChangeCount != statesAfterMaximumRevision
        || notifications.hasNotifications()) return 207;

    return 0;
}
