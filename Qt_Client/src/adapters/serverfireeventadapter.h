#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

enum class ServerFireEventAction {
    NotFire,
    Activate,
    Acknowledge,
    Clear,
    Invalid
};

struct ServerFireEvent {
    ServerFireEventAction action = ServerFireEventAction::NotFire;
    QString eventId;
    QString alarmId;
    QString eventType;
    QString channelId;
    QString sourceId;
    QString alarmKind;
    QString alarmState;
    QString ackState;
    QString scope;
    QString severity;
    QString message;
    QDateTime occurredAt;
    bool activePresent = false;
    bool active = false;
    QString errorMessage;
};

class ServerFireEventAdapter
{
public:
    static ServerFireEvent parse(const QJsonObject &object);
};
