#include "services/notificationcenter.h"
#include "services/notificationpolicy.h"

#include <QCoreApplication>
#include <QDateTime>

namespace {
MonitoringEvent makeEvent(const QString &sourceId,
                          const QString &eventType,
                          const QString &status,
                          const QString &message,
                          EventSeverity severity = EventSeverity::Unknown)
{
    MonitoringEvent event;
    event.id = sourceId + QLatin1Char('-') + eventType;
    event.occurredAt = QDateTime::currentDateTime();
    event.sourceId = sourceId;
    event.eventType = eventType;
    event.severity = severity;
    event.ackState = eventAckStateFromStatus(status);
    event.message = message;
    event.status = status;
    return event;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    NotificationCenter center;
    int changeCount = 0;

    QObject::connect(&center, &NotificationCenter::notificationsChanged,
                     &app, [&changeCount]() { ++changeCount; });

    center.ingestEvent(makeEvent(QStringLiteral("SYSTEM"),
                                 QStringLiteral("RX_ERROR"),
                                 QStringLiteral("REJECTED"),
                                 QStringLiteral("invalid input")));
    if (center.hasNotifications() || changeCount != 0) return 1;

    center.ingestEvent(makeEvent(QStringLiteral("P-03"),
                                 QStringLiteral("HALL_SENSOR_ERROR"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("sensor disconnected")));
    if (center.notifications().size() != 1 || changeCount != 1) return 2;
    if (center.notifications().constFirst().severity != QStringLiteral("WARNING")) return 3;
    if (center.notifications().constFirst().title != QStringLiteral("Hall sensor error")) return 4;
    if (center.unreadCount() != 1) return 5;

    center.ingestEvent(makeEvent(QStringLiteral("P-03"),
                                 QStringLiteral("HALL_SENSOR_ERROR"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("sensor still disconnected")));
    if (center.notifications().size() != 1 || changeCount != 2) return 6;
    if (center.notifications().constFirst().message
        != QStringLiteral("sensor still disconnected")) return 7;

    center.ingestEvent(makeEvent(QStringLiteral("P-04"),
                                 QStringLiteral("HALL_SENSOR_ERROR"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("other sensor disconnected")));
    if (center.notifications().size() != 2) return 8;

    center.ingestEvent(makeEvent(QStringLiteral("P-03"),
                                 QStringLiteral("HALL_SENSOR_CLEAR"),
                                 QStringLiteral("CLEARED"),
                                 QStringLiteral("sensor recovered")));
    if (center.notifications().size() != 1) return 9;
    if (center.notifications().constFirst().sourceId != QStringLiteral("P-04")) return 10;

    center.ingestEvent(makeEvent(QStringLiteral("CH2"),
                                 QStringLiteral("FIRE_EVENT"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("fire detected")));
    if (center.notifications().constFirst().eventType != QStringLiteral("FIRE_ALARM")) return 11;
    if (center.notifications().constFirst().severity != QStringLiteral("CRITICAL")) return 12;

    center.ingestEvent(makeEvent(QStringLiteral("CH2"),
                                 QStringLiteral("FLAME"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("flame detected")));
    if (center.notifications().size() != 3) return 13;

    center.ingestEvent(makeEvent(QStringLiteral("CH2"),
                                 QStringLiteral("FIRE_ALARM_ACK"),
                                 QStringLiteral("ACKED"),
                                 QStringLiteral("operator acknowledged")));
    if (center.notifications().size() != 1) return 14;
    if (center.notifications().constFirst().sourceId != QStringLiteral("P-04")) return 15;

    center.markAllRead();
    if (center.unreadCount() != 0) return 16;
    const int countAfterRead = changeCount;
    center.markAllRead();
    if (changeCount != countAfterRead) return 17;

    center.clearAll();
    if (center.hasNotifications()) return 18;

    for (int i = 0; i < 12; ++i) {
        center.ingestEvent(makeEvent(QStringLiteral("SOURCE-%1").arg(i),
                                     QStringLiteral("API_ERROR"),
                                     QStringLiteral("FAILED"),
                                     QStringLiteral("API failure")));
    }
    if (center.notifications().size() != 10) return 19;
    if (center.notifications().constFirst().sourceId != QStringLiteral("SOURCE-11")) return 20;
    if (center.notifications().constLast().sourceId != QStringLiteral("SOURCE-2")) return 21;

    center.ingestEvent(makeEvent(QStringLiteral("ALL"),
                                 QStringLiteral("ALARM_ACK"),
                                 QStringLiteral("ACKED"),
                                 QStringLiteral("all alarms acknowledged")));
    if (center.hasNotifications()) return 22;

    center.ingestEvent(makeEvent(QStringLiteral("CH1"),
                                 QStringLiteral("CAMERA_DISCONNECTED"),
                                 QStringLiteral("FAILED"),
                                 QStringLiteral("camera disconnected")));
    if (center.notifications().size() != 1) return 23;
    center.ingestEvent(makeEvent(QStringLiteral("CH1"),
                                 QStringLiteral("CAMERA_RECONNECTED"),
                                 QStringLiteral("DONE"),
                                 QStringLiteral("camera reconnected")));
    if (center.hasNotifications()) return 24;

    center.ingestEvent(makeEvent(QStringLiteral("SYSTEM"),
                                 QStringLiteral("API_ERROR"),
                                 QStringLiteral("FAILED"),
                                 QStringLiteral("server API unavailable")));
    center.ingestEvent(makeEvent(QStringLiteral("P-03"),
                                 QStringLiteral("NON_EV_ALERT"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("non-EV vehicle detected")));
    if (center.notifications().size() != 2) return 25;

    center.ingestEvent(makeEvent(QStringLiteral("P-03"),
                                 QStringLiteral("ALARM_ACK"),
                                 QStringLiteral("ACKED"),
                                 QStringLiteral("slot alarm acknowledged")));
    if (center.notifications().size() != 1) return 26;
    if (center.notifications().constFirst().sourceId != QStringLiteral("SYSTEM")) return 27;

    NotificationPolicy policy;
    NotificationDecision decision = policy.evaluate(
        makeEvent(QStringLiteral(" ch2 "),
                  QStringLiteral(" fire_event "),
                  QStringLiteral(" open "),
                  QStringLiteral("fire detected")));
    if (decision.action != NotificationAction::Upsert) return 28;
    if (decision.sourceId != QStringLiteral("CH2")) return 29;
    if (decision.eventType != QStringLiteral("FIRE_ALARM")) return 30;
    if (decision.status != QStringLiteral("OPEN")) return 31;
    if (decision.severity != EventSeverity::Critical) return 32;
    if (decision.title != QStringLiteral("Fire alarm")) return 33;

    decision = policy.evaluate(
        makeEvent(QStringLiteral("CH2"),
                  QStringLiteral("FIRE_ALARM"),
                  QStringLiteral("OPEN"),
                  QStringLiteral("operator override"),
                  EventSeverity::Info));
    if (decision.severity != EventSeverity::Info) return 34;

    decision = policy.evaluate(
        makeEvent(QStringLiteral("SYSTEM"),
                  QStringLiteral("RX_ERROR"),
                  QStringLiteral("REJECTED"),
                  QStringLiteral("invalid input")));
    if (decision.action != NotificationAction::Ignore) return 35;

    decision = policy.evaluate(
        makeEvent(QStringLiteral("CH2"),
                  QStringLiteral("FIRE_ALARM_ACK"),
                  QStringLiteral("ACKED"),
                  QStringLiteral("operator acknowledged")));
    if (decision.action != NotificationAction::Resolve) return 36;
    if (!policy.isSameAlertGroup(QStringLiteral("FIRE_ALARM"),
                                 QStringLiteral("FLAME_DETECTED"))) return 37;
    if (policy.isSameAlertGroup(QStringLiteral("API_ERROR"),
                                QStringLiteral("HALL_SENSOR_CLEAR"))) return 38;

    center.clearAll();
    center.ingestEvent(makeEvent(QStringLiteral("CH1"),
                                 QStringLiteral("FIRE_SUSPECTED"),
                                 QStringLiteral("OPEN"),
                                 QStringLiteral("channel fire requires verification")));
    if (center.notifications().size() != 1) return 39;
    const NotificationRecord fireSuspected = center.notifications().constFirst();
    if (fireSuspected.eventType != QStringLiteral("FIRE_SUSPECTED")) return 40;
    if (fireSuspected.severity != QStringLiteral("CRITICAL")) return 41;
    if (fireSuspected.title != QStringLiteral("Fire suspected")) return 42;
    if (center.unreadCount() != 1) return 43;
    if (fireSuspected.sourceId != QStringLiteral("CH1")) return 44;

    center.ingestEvent(makeEvent(QStringLiteral("CH1"),
                                 QStringLiteral("FIRE_CLEARED"),
                                 QStringLiteral("CLEARED"),
                                 QStringLiteral("channel fire cleared")));
    if (center.hasNotifications()) return 45;

    return 0;
}
