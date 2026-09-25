#ifndef NOTIFICATIONCENTER_H
#define NOTIFICATIONCENTER_H

#include "models/monitoringevent.h"
#include "services/notificationpolicy.h"

#include <QList>
#include <QObject>
#include <QString>

struct NotificationRecord {
    QString id;
    QString eventId;
    QString time;
    QString sourceId;
    QString eventType;
    QString severity;
    QString title;
    QString message;
    QString status;
    bool unread = true;
};

class NotificationCenter : public QObject
{
    Q_OBJECT

public:
    explicit NotificationCenter(QObject *parent = nullptr);

    QList<NotificationRecord> notifications() const;
    int unreadCount() const;
    bool hasNotifications() const;

    void ingestEvent(const MonitoringEvent &event);
    void markAllRead();
    void clearAll();

signals:
    void notificationsChanged();

private:
    bool acknowledgeMatching(const QString &sourceId, const QString &eventType);
    void addOrUpdateNotification(NotificationRecord record);

    NotificationPolicy m_policy;
    QList<NotificationRecord> m_notifications;
    int m_nextNotificationId = 1;
};

#endif
