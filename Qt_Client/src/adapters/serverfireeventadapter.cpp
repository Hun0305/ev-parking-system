#include "serverfireeventadapter.h"

#include <QJsonValue>
#include <QRegularExpression>

namespace {
QString upperString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString().trimmed().toUpper();
}

QString normalizeChannelId(const QString &rawChannelId)
{
    static const QRegularExpression channelPattern(
        QStringLiteral("^CH0?([1-4])$"));
    const QRegularExpressionMatch match = channelPattern.match(
        rawChannelId.trimmed().toUpper());
    return match.hasMatch()
        ? QStringLiteral("CH%1").arg(match.captured(1)) : QString();
}

ServerFireEvent invalidEvent(ServerFireEvent event, const QString &message)
{
    event.action = ServerFireEventAction::Invalid;
    event.errorMessage = message;
    return event;
}
}

ServerFireEvent ServerFireEventAdapter::parse(const QJsonObject &object)
{
    ServerFireEvent event;
    event.eventId = object.value(QStringLiteral("event_id")).toString().trimmed();
    event.alarmId = object.value(QStringLiteral("alarm_id")).toString().trimmed();
    event.eventType = upperString(object, QStringLiteral("event_type"));
    event.channelId = normalizeChannelId(
        object.value(QStringLiteral("channel_id")).toString());
    event.sourceId = object.value(QStringLiteral("source_id")).toString().trimmed();
    event.alarmKind = upperString(object, QStringLiteral("alarm_kind"));
    const QString compatibilityAlarm = upperString(object, QStringLiteral("alarm"));
    event.alarmState = upperString(object, QStringLiteral("alarm_state"));
    event.ackState = object.value(QStringLiteral("ack_state"))
                         .toString().trimmed().toLower();
    event.scope = upperString(object, QStringLiteral("scope"));
    event.severity = upperString(object, QStringLiteral("severity"));
    event.message = object.value(QStringLiteral("message")).toString().trimmed();
    event.occurredAt = QDateTime::fromString(
        object.value(QStringLiteral("timestamp")).toString().trimmed(),
        Qt::ISODate);

    const QJsonValue activeValue = object.value(QStringLiteral("active"));
    event.activePresent = !activeValue.isUndefined() && !activeValue.isNull();
    if (event.activePresent) {
        if (!activeValue.isBool()) {
            return invalidEvent(event, QStringLiteral("active must be a boolean"));
        }
        event.active = activeValue.toBool();
    }

    const bool suspectedType =
        event.eventType == QStringLiteral("FIRE_SUSPECTED");
    const bool clearedType =
        event.eventType == QStringLiteral("FIRE_CLEARED");
    const bool acknowledgedType =
        event.eventType == QStringLiteral("FIRE_ACKNOWLEDGED")
        || event.eventType == QStringLiteral("FIRE_ALARM_ACK");
    const bool alarmKindFire = event.alarmKind == QStringLiteral("FIRE_SUSPECTED");
    const bool compatibilityAlarmFire =
        compatibilityAlarm == QStringLiteral("FIRE_SUSPECTED");
    if (!suspectedType && !clearedType && !acknowledgedType
        && !alarmKindFire && !compatibilityAlarmFire) {
        return event;
    }

    if (event.channelId.isEmpty()) {
        return invalidEvent(event, QStringLiteral("unknown or missing channel_id"));
    }
    if (!event.scope.isEmpty()
        && event.scope != QStringLiteral("CAMERA_CHANNEL")) {
        return invalidEvent(event, QStringLiteral("fire scope must be CAMERA_CHANNEL"));
    }

    if (acknowledgedType) {
        if (!alarmKindFire && !compatibilityAlarmFire) {
            return invalidEvent(
                event,
                QStringLiteral("FIRE_ACKNOWLEDGED requires FIRE_SUSPECTED alarm kind"));
        }
        if (event.alarmId.isEmpty()) {
            return invalidEvent(
                event, QStringLiteral("FIRE_ACKNOWLEDGED requires alarm_id"));
        }
        if (event.alarmState != QStringLiteral("ACKNOWLEDGED")) {
            return invalidEvent(
                event,
                QStringLiteral("FIRE_ACKNOWLEDGED requires alarm_state=ACKNOWLEDGED"));
        }
        if (event.ackState != QStringLiteral("acknowledged")) {
            return invalidEvent(
                event,
                QStringLiteral("FIRE_ACKNOWLEDGED requires ack_state=acknowledged"));
        }
        if (!event.activePresent || !event.active) {
            return invalidEvent(
                event, QStringLiteral("FIRE_ACKNOWLEDGED requires active=true"));
        }
        event.action = ServerFireEventAction::Acknowledge;
        return event;
    }

    if (clearedType) {
        if (event.alarmId.isEmpty()) {
            return invalidEvent(
                event, QStringLiteral("FIRE_CLEARED requires alarm_id"));
        }
        if (!event.alarmKind.isEmpty()
            && event.alarmKind != QStringLiteral("NONE")
            && !alarmKindFire) {
            return invalidEvent(
                event,
                QStringLiteral("cleared alarm_kind must be NONE or FIRE_SUSPECTED"));
        }
        if (!compatibilityAlarm.isEmpty()
            && compatibilityAlarm != QStringLiteral("NONE")
            && !compatibilityAlarmFire) {
            return invalidEvent(
                event,
                QStringLiteral("cleared alarm must be NONE or FIRE_SUSPECTED"));
        }
        if (event.alarmState != QStringLiteral("RESOLVED")) {
            return invalidEvent(event, QStringLiteral("cleared alarm_state must be RESOLVED"));
        }
        if (event.ackState != QStringLiteral("resolved")) {
            return invalidEvent(event, QStringLiteral("FIRE_CLEARED requires ack_state=resolved"));
        }
        if (!event.activePresent || event.active) {
            return invalidEvent(event, QStringLiteral("FIRE_CLEARED requires active=false"));
        }
        event.action = ServerFireEventAction::Clear;
        return event;
    }

    if (!suspectedType) {
        return invalidEvent(event, QStringLiteral("non-fire event_type contradicts fire alarm kind"));
    }
    if (!alarmKindFire && !compatibilityAlarmFire) {
        return invalidEvent(
            event,
            QStringLiteral("FIRE_SUSPECTED requires alarm_kind or alarm=FIRE_SUSPECTED"));
    }
    if (event.eventId.isEmpty() || event.alarmId.isEmpty()) {
        return invalidEvent(
            event, QStringLiteral("FIRE_SUSPECTED requires event_id and alarm_id"));
    }
    if (event.alarmState != QStringLiteral("OPEN")) {
        return invalidEvent(event, QStringLiteral("suspected alarm_state must be OPEN"));
    }
    if (!event.activePresent || !event.active) {
        return invalidEvent(event, QStringLiteral("FIRE_SUSPECTED requires active=true"));
    }

    event.action = ServerFireEventAction::Activate;
    return event;
}
