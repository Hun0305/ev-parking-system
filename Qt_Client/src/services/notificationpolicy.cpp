#include "notificationpolicy.h"

#include <QStringList>

namespace {
bool isClosedStatus(const QString &status)
{
    return status == QStringLiteral("ACKED")
        || status == QStringLiteral("CLEAR")
        || status == QStringLiteral("CLEARED");
}
}

NotificationDecision NotificationPolicy::evaluate(const MonitoringEvent &event) const
{
    NotificationDecision decision;
    decision.sourceId = event.sourceId.trimmed().toUpper();
    if (decision.sourceId.isEmpty()) {
        decision.sourceId = QStringLiteral("SYSTEM");
    }

    decision.eventType = normalizeEventType(event.eventType);
    decision.status = monitoringEventStatusText(event);

    if (isResolutionEvent(decision.eventType, decision.status)) {
        decision.action = NotificationAction::Resolve;
        return decision;
    }

    if (!isNotifiableEvent(decision.eventType, decision.status)) {
        return decision;
    }

    decision.action = NotificationAction::Upsert;
    decision.severity = event.severity == EventSeverity::Unknown
        ? defaultSeverityForEvent(decision.eventType)
        : event.severity;
    decision.title = titleForEvent(decision.eventType);
    return decision;
}

QString NotificationPolicy::normalizeEventType(const QString &eventType) const
{
    const QString normalized = eventType.trimmed().toUpper();
    if (normalized == QStringLiteral("FIRE_EVENT")) {
        return QStringLiteral("FIRE_ALARM");
    }
    if (normalized == QStringLiteral("FLAME")) {
        return QStringLiteral("FLAME_DETECTED");
    }
    if (normalized == QStringLiteral("SENSOR_ERROR")) {
        return QStringLiteral("HALL_SENSOR_ERROR");
    }
    return normalized;
}

EventSeverity NotificationPolicy::defaultSeverityForEvent(const QString &eventType) const
{
    if (eventType == QStringLiteral("FIRE_ALARM")
        || eventType == QStringLiteral("FIRE_SUSPECTED")
        || eventType == QStringLiteral("FLAME_DETECTED")) {
        return EventSeverity::Critical;
    }
    if (eventType == QStringLiteral("API_ERROR")
        || eventType == QStringLiteral("API_PARSE_ERROR")
        || eventType == QStringLiteral("CAMERA_DISCONNECTED")
        || eventType == QStringLiteral("DB_WRITE_FAILED")
        || eventType == QStringLiteral("LAYOUT_SAVE_FAILED")
        || eventType == QStringLiteral("HALL_SENSOR_ERROR")
        || eventType == QStringLiteral("NON_EV_ALERT")
        || eventType == QStringLiteral("OVERTIME_ALERT")) {
        return EventSeverity::Warning;
    }
    return EventSeverity::Info;
}

QString NotificationPolicy::titleForEvent(const QString &eventType) const
{
    if (eventType == QStringLiteral("FIRE_ALARM")) {
        return QStringLiteral("Fire alarm");
    }
    if (eventType == QStringLiteral("FIRE_SUSPECTED")) {
        return QStringLiteral("Fire suspected");
    }
    if (eventType == QStringLiteral("FLAME_DETECTED")) {
        return QStringLiteral("Flame detected");
    }
    if (eventType == QStringLiteral("HALL_SENSOR_ERROR")) {
        return QStringLiteral("Hall sensor error");
    }
    if (eventType == QStringLiteral("HALL_SENSOR_CHANGED")) {
        return QStringLiteral("Hall sensor state changed");
    }
    if (eventType == QStringLiteral("CAMERA_DISCONNECTED")) {
        return QStringLiteral("Camera disconnected");
    }
    if (eventType == QStringLiteral("API_ERROR")
        || eventType == QStringLiteral("API_PARSE_ERROR")) {
        return QStringLiteral("Parking API error");
    }
    if (eventType == QStringLiteral("DB_WRITE_FAILED")) {
        return QStringLiteral("Database write failed");
    }
    if (eventType == QStringLiteral("LAYOUT_SAVE_FAILED")) {
        return QStringLiteral("Parking map layout save failed");
    }
    if (eventType == QStringLiteral("NON_EV_ALERT")) {
        return QStringLiteral("Non-EV alert");
    }
    if (eventType == QStringLiteral("OVERTIME_ALERT")) {
        return QStringLiteral("Overtime alert");
    }
    return eventType;
}

bool NotificationPolicy::isResolutionEvent(const QString &eventType,
                                           const QString &status) const
{
    return eventType.contains(QStringLiteral("ACK"))
        || eventType.contains(QStringLiteral("CLEAR"))
        || eventType == QStringLiteral("CAMERA_RECONNECTED")
        || isClosedStatus(status);
}

bool NotificationPolicy::isNotifiableEvent(const QString &eventType,
                                            const QString &status) const
{
    if (status == QStringLiteral("RECORDED")
        || status == QStringLiteral("DONE")
        || status == QStringLiteral("SKIPPED")
        || status == QStringLiteral("REJECTED")) {
        return false;
    }

    static const QStringList explicitTypes = {
        QStringLiteral("FIRE_ALARM"),
        QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("FLAME_DETECTED"),
        QStringLiteral("HALL_SENSOR_ERROR"),
        QStringLiteral("HALL_SENSOR_CHANGED"),
        QStringLiteral("CAMERA_DISCONNECTED"),
        QStringLiteral("API_ERROR"),
        QStringLiteral("API_PARSE_ERROR"),
        QStringLiteral("DB_WRITE_FAILED"),
        QStringLiteral("NON_EV_ALERT"),
        QStringLiteral("OVERTIME_ALERT")
    };
    if (explicitTypes.contains(eventType)) {
        return true;
    }

    if (eventType.endsWith(QStringLiteral("_ERROR"))
        || eventType.contains(QStringLiteral("DISCONNECTED"))
        || eventType.contains(QStringLiteral("FAILED"))) {
        return true;
    }

    return (status == QStringLiteral("OPEN") || status == QStringLiteral("FAILED"))
        && (eventType.contains(QStringLiteral("ALARM"))
            || eventType.contains(QStringLiteral("ALERT"))
            || eventType.contains(QStringLiteral("ERROR")));
}

bool NotificationPolicy::isSameAlertGroup(const QString &left,
                                          const QString &right) const
{
    const QString normalizedLeft = normalizeEventType(left);
    const QString normalizedRight = normalizeEventType(right);
    if (normalizedLeft == normalizedRight) {
        return true;
    }
    if ((normalizedLeft.contains(QStringLiteral("FIRE"))
         || normalizedLeft.contains(QStringLiteral("FLAME")))
        && (normalizedRight.contains(QStringLiteral("FIRE"))
            || normalizedRight.contains(QStringLiteral("FLAME")))) {
        return true;
    }
    if (normalizedLeft.startsWith(QStringLiteral("HALL_SENSOR"))
        && normalizedRight.startsWith(QStringLiteral("HALL_SENSOR"))) {
        return true;
    }
    if (normalizedRight == QStringLiteral("ALARM_ACK")) {
        return normalizedLeft.contains(QStringLiteral("ALARM"))
            || normalizedLeft.contains(QStringLiteral("ALERT"))
            || normalizedLeft.endsWith(QStringLiteral("_ERROR"));
    }
    if (normalizedRight == QStringLiteral("SLOT_VACATED")) {
        return normalizedLeft == QStringLiteral("NON_EV_ALERT")
            || normalizedLeft == QStringLiteral("OVERTIME_ALERT");
    }
    if (normalizedRight == QStringLiteral("CAMERA_RECONNECTED")) {
        return normalizedLeft == QStringLiteral("CAMERA_DISCONNECTED");
    }
    return false;
}
