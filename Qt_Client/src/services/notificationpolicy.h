#pragma once

#include "models/monitoringevent.h"

#include <QString>

enum class NotificationAction {
    Ignore,
    Upsert,
    Resolve
};

struct NotificationDecision {
    NotificationAction action = NotificationAction::Ignore;
    QString sourceId;
    QString eventType;
    QString status;
    EventSeverity severity = EventSeverity::Unknown;
    QString title;
};

class NotificationPolicy final
{
public:
    NotificationDecision evaluate(const MonitoringEvent &event) const;
    bool isSameAlertGroup(const QString &left, const QString &right) const;

private:
    QString normalizeEventType(const QString &eventType) const;
    EventSeverity defaultSeverityForEvent(const QString &eventType) const;
    QString titleForEvent(const QString &eventType) const;
    bool isResolutionEvent(const QString &eventType, const QString &status) const;
    bool isNotifiableEvent(const QString &eventType, const QString &status) const;
};
