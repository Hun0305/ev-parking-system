#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct ServerParkingEvent {
    QString eventId;
    QString eventType;
    QString slotId;
    QString channelId;
    QString plateNumber;
    QString vehicleType;
    QString parkingState;
    QString alarmKind;
    QString alarmState;
    QString severity;
    QString message;
    QString evidencePath;
    QDateTime occurredAt;
    qint64 sessionId = -1;
    int occupiedSeconds = 0;
};

class ServerParkingEventAdapter
{
public:
    static bool parse(const QJsonObject &object,
                      ServerParkingEvent &event,
                      QString &errorMessage);
    static QString effectiveAlarmKind(const ServerParkingEvent &event);
    static QString monitoringEventType(const ServerParkingEvent &event);
    static QString monitoringStatus(const ServerParkingEvent &event);
};
