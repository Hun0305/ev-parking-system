#include "monitoringevent.h"

EventAckState eventAckStateFromStatus(const QString &status)
{
    const QString normalized = status.trimmed().toUpper();
    if (normalized == QStringLiteral("OPEN")) return EventAckState::Open;
    if (normalized == QStringLiteral("ACKED")
        || normalized == QStringLiteral("ACKNOWLEDGED")) {
        return EventAckState::Acknowledged;
    }
    if (normalized == QStringLiteral("CLEAR")
        || normalized == QStringLiteral("CLEARED")
        || normalized == QStringLiteral("CLOSED")
        || normalized == QStringLiteral("RESOLVED")) {
        return EventAckState::Cleared;
    }
    return EventAckState::None;
}

QString eventSeverityText(EventSeverity severity)
{
    switch (severity) {
    case EventSeverity::Info: return QStringLiteral("INFO");
    case EventSeverity::Warning: return QStringLiteral("WARNING");
    case EventSeverity::Critical: return QStringLiteral("CRITICAL");
    case EventSeverity::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

QString monitoringEventStatusText(const MonitoringEvent &event)
{
    const QString normalized = event.status.trimmed().toUpper();
    if (!normalized.isEmpty()) return normalized;

    switch (event.ackState) {
    case EventAckState::Open: return QStringLiteral("OPEN");
    case EventAckState::Acknowledged: return QStringLiteral("ACKED");
    case EventAckState::Cleared: return QStringLiteral("CLEARED");
    case EventAckState::None: return QString();
    }
    return QString();
}

QString monitoringEventTimeText(const MonitoringEvent &event)
{
    return event.occurredAt.isValid()
        ? event.occurredAt.toLocalTime().toString(QStringLiteral("HH:mm:ss"))
        : QString();
}
