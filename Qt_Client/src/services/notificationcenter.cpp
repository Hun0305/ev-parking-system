#include "notificationcenter.h"

namespace {
constexpr int kMaxNotifications = 10;
}

NotificationCenter::NotificationCenter(QObject *parent)
    : QObject(parent)
{
}

QList<NotificationRecord> NotificationCenter::notifications() const
{
    return m_notifications;
}

int NotificationCenter::unreadCount() const
{
    int count = 0;
    for (const NotificationRecord &notification : m_notifications) {
        if (notification.unread) {
            ++count;
        }
    }
    return count;
}

bool NotificationCenter::hasNotifications() const
{
    return !m_notifications.isEmpty();
}

void NotificationCenter::ingestEvent(const MonitoringEvent &event)
{
    const NotificationDecision decision = m_policy.evaluate(event);

    if (decision.action == NotificationAction::Resolve) {
        if (acknowledgeMatching(decision.sourceId, decision.eventType)) {
            emit notificationsChanged();
        }
        return;
    }

    if (decision.action == NotificationAction::Ignore) {
        return;
    }

    NotificationRecord record;
    record.id = QStringLiteral("N%1").arg(m_nextNotificationId++);
    record.eventId = event.id;
    record.time = monitoringEventTimeText(event);
    record.sourceId = decision.sourceId;
    record.eventType = decision.eventType;
    record.severity = eventSeverityText(decision.severity);
    record.title = decision.title;
    record.message = event.message;
    record.status = decision.status;
    record.unread = true;
    addOrUpdateNotification(record);
    emit notificationsChanged();
}

void NotificationCenter::markAllRead()
{
    bool changed = false;
    for (NotificationRecord &notification : m_notifications) {
        if (notification.unread) {
            notification.unread = false;
            changed = true;
        }
    }
    if (changed) {
        emit notificationsChanged();
    }
}

void NotificationCenter::clearAll()
{
    if (m_notifications.isEmpty()) {
        return;
    }
    m_notifications.clear();
    emit notificationsChanged();
}

bool NotificationCenter::acknowledgeMatching(const QString &sourceId, const QString &eventType)
{
    if (sourceId == QStringLiteral("ALL")) {
        const bool hadNotifications = !m_notifications.isEmpty();
        m_notifications.clear();
        return hadNotifications;
    }

    bool changed = false;
    for (int i = m_notifications.size() - 1; i >= 0; --i) {
        const NotificationRecord &notification = m_notifications.at(i);
        const bool sourceMatches = notification.sourceId == sourceId;
        if (sourceMatches && m_policy.isSameAlertGroup(notification.eventType, eventType)) {
            m_notifications.removeAt(i);
            changed = true;
        }
    }
    return changed;
}

void NotificationCenter::addOrUpdateNotification(NotificationRecord record)
{
    for (int i = m_notifications.size() - 1; i >= 0; --i) {
        const NotificationRecord &existing = m_notifications.at(i);
        if (existing.sourceId == record.sourceId
            && existing.eventType == record.eventType) {
            m_notifications.removeAt(i);
        }
    }

    m_notifications.prepend(record);
    while (m_notifications.size() > kMaxNotifications) {
        m_notifications.removeLast();
    }
}
