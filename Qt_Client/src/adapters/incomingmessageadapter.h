#pragma once

#include <QString>

enum class IncomingMessageKind {
    Invalid,
    NormalizedEvent,
    ParkingSlot,
    HallSensor,
    EvAlert,
    FireAlarm,
    Unsupported
};

struct ParsedIncomingMessage {
    IncomingMessageKind kind = IncomingMessageKind::Invalid;
    QString raw;
    QString messageType;
    QString sourceId;
    QString value;
    QString eventType;
    QString status;
    QString message;
    QString errorMessage;

    bool isValid() const { return kind != IncomingMessageKind::Invalid; }
};

class IncomingMessageAdapter
{
public:
    static ParsedIncomingMessage parse(const QString &message);
};
