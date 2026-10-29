#include "serverparkingeventadapter.h"

#include <QJsonValue>

namespace {
QString upperString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString().trimmed().toUpper();
}

qint64 integerValue(const QJsonValue &value, qint64 fallback)
{
    if (value.isDouble()) {
        return value.toVariant().toLongLong();
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        if (ok) return parsed;
    }
    return fallback;
}
}

bool ServerParkingEventAdapter::parse(const QJsonObject &object,
                                      ServerParkingEvent &event,
                                      QString &errorMessage)
{
    ServerParkingEvent parsed;
    parsed.eventId = object.value(QStringLiteral("event_id")).toString().trimmed();
    parsed.eventType = upperString(object, QStringLiteral("event_type"));
    parsed.slotId = object.value(QStringLiteral("slot_id")).toString().trimmed();
    parsed.channelId = object.value(QStringLiteral("channel_id")).toString().trimmed();
    parsed.plateNumber = object.value(QStringLiteral("plate_number")).toString().trimmed();
    parsed.vehicleType = upperString(object, QStringLiteral("vehicle_type"));
    parsed.vehicleClassification = upperString(object, QStringLiteral("vehicle_classification"));
    if (parsed.vehicleType.isEmpty()) {
        parsed.vehicleType = parsed.vehicleClassification;
    }
    parsed.parkingState = upperString(object, QStringLiteral("parking_state"));
    parsed.alarmKind = upperString(object, QStringLiteral("alarm_kind"));
    if (parsed.alarmKind.isEmpty()) {
        parsed.alarmKind = upperString(object, QStringLiteral("alarm"));
    }
    parsed.alarmState = upperString(object, QStringLiteral("alarm_state"));
    parsed.severity = upperString(object, QStringLiteral("severity"));
    parsed.message = object.value(QStringLiteral("message")).toString().trimmed();
    parsed.evidencePath = object.value(QStringLiteral("evidence_path")).toString().trimmed();
    parsed.sessionId = integerValue(object.value(QStringLiteral("session_id")), -1);
    parsed.occupiedSeconds = static_cast<int>(
        qMax<qint64>(0, integerValue(object.value(QStringLiteral("occupied_seconds")), 0)));
    parsed.correlationId = object.value(QStringLiteral("correlation_id")).toString().trimmed();
    parsed.ocrStatus = upperString(object, QStringLiteral("ocr_status"));
    parsed.occurredAt = QDateTime::fromString(
        object.value(QStringLiteral("timestamp")).toString().trimmed(), Qt::ISODate);

    if (parsed.eventType.isEmpty()) {
        errorMessage = QStringLiteral("Server parking event requires event_type");
        return false;
    }

    event = parsed;
    errorMessage.clear();
    return true;
}

QString ServerParkingEventAdapter::effectiveAlarmKind(
    const ServerParkingEvent &event)
{
    if (!event.alarmKind.isEmpty() && event.alarmKind != QStringLiteral("NONE")) {
        return event.alarmKind;
    }
    if (event.eventType == QStringLiteral("NON_EV_ALERT")) {
        return QStringLiteral("NON_EV");
    }
    if (event.eventType == QStringLiteral("OVERTIME_VIOLATION")
        || event.eventType == QStringLiteral("VIOLATION_TRIGGERED")) {
        return QStringLiteral("OVERSTAY");
    }
    if (event.eventType == QStringLiteral("SENSOR_ERROR")) {
        return QStringLiteral("SENSOR_ERROR");
    }
    return QStringLiteral("NONE");
}

QString ServerParkingEventAdapter::monitoringEventType(
    const ServerParkingEvent &event)
{
    if (event.eventType == QStringLiteral("OVERTIME_VIOLATION")
        || event.eventType == QStringLiteral("VIOLATION_TRIGGERED")) {
        return QStringLiteral("OVERTIME_ALERT");
    }
    if (event.eventType == QStringLiteral("SENSOR_ERROR")) {
        return QStringLiteral("HALL_SENSOR_ERROR");
    }
    if (event.eventType == QStringLiteral("SENSOR_RECOVERED")) {
        return QStringLiteral("HALL_SENSOR_CLEAR");
    }
    return event.eventType;
}

QString ServerParkingEventAdapter::monitoringStatus(
    const ServerParkingEvent &event)
{
    if (event.alarmState == QStringLiteral("OPEN")) {
        return QStringLiteral("OPEN");
    }
    if (event.alarmState == QStringLiteral("CLEAR")
        || event.alarmState == QStringLiteral("CLEARED")
        || event.alarmState == QStringLiteral("CLOSED")
        || event.alarmState == QStringLiteral("RESOLVED")) {
        return QStringLiteral("CLEARED");
    }
    if (effectiveAlarmKind(event) != QStringLiteral("NONE")) {
        // 1308cfd does not publish alarm_kind for NON_EV/OVERSTAY. Preserve
        // compatibility by deriving an open alarm from event_type.
        return QStringLiteral("OPEN");
    }
    if (event.eventType == QStringLiteral("SLOT_VACATED")) {
        return QStringLiteral("CLEARED");
    }
    if (event.eventType.endsWith(QStringLiteral("_ERROR"))) {
        return QStringLiteral("FAILED");
    }
    return QStringLiteral("RECORDED");
}
