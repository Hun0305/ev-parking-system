#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class EventSeverity {
    Unknown,
    Info,
    Warning,
    Critical
};

enum class EventAckState {
    None,
    Open,
    Acknowledged,
    Cleared
};

struct MonitoringEvent {
    QString id;
    QDateTime occurredAt;
    QString sourceId;
    QString eventType;
    EventSeverity severity = EventSeverity::Unknown;
    EventAckState ackState = EventAckState::None;
    QString message;
    QString status;
};

EventAckState eventAckStateFromStatus(const QString &status);
QString eventSeverityText(EventSeverity severity);
QString monitoringEventStatusText(const MonitoringEvent &event);
QString monitoringEventTimeText(const MonitoringEvent &event);

Q_DECLARE_METATYPE(MonitoringEvent)
