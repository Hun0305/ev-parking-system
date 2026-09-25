#include "incomingmessageadapter.h"

#include "models/parkingstate.h"

#include <QStringList>

ParsedIncomingMessage IncomingMessageAdapter::parse(const QString &message)
{
    ParsedIncomingMessage parsed;
    parsed.raw = message.trimmed();

    QStringList parts = parsed.raw.split(QLatin1Char(','));
    for (QString &part : parts) part = part.trimmed();
    parts.removeAll(QString());

    if (parts.size() < 3) {
        parsed.errorMessage = QStringLiteral("Invalid message format: ") + parsed.raw;
        return parsed;
    }

    parsed.messageType = parts.at(0).toUpper();
    parsed.sourceId = normalizeParkingSlotId(parts.at(1));
    parsed.value = parts.at(2).toUpper();

    if (parsed.messageType == QStringLiteral("EVENT")
        || parsed.messageType == QStringLiteral("DB_EVENT")) {
        if (parts.size() < 4) {
            parsed.errorMessage = QStringLiteral("Invalid normalized event: ") + parsed.raw;
            return parsed;
        }
        parsed.kind = IncomingMessageKind::NormalizedEvent;
        parsed.eventType = parsed.value;
        parsed.status = parts.at(3).toUpper();
        parsed.message = parts.size() > 4
            ? parts.mid(4).join(QStringLiteral(", "))
            : QStringLiteral("Normalized event received");
        return parsed;
    }

    if (parsed.messageType == QStringLiteral("PARKING_SLOT")) {
        parsed.kind = IncomingMessageKind::ParkingSlot;
    } else if (parsed.messageType == QStringLiteral("HALL_SENSOR")
               || parsed.messageType == QStringLiteral("HALL_SENSOR_EVENT")) {
        parsed.kind = IncomingMessageKind::HallSensor;
    } else if (parsed.messageType == QStringLiteral("EV_ALERT")) {
        parsed.kind = IncomingMessageKind::EvAlert;
    } else if (parsed.messageType == QStringLiteral("FIRE_ALARM")
               || parsed.messageType == QStringLiteral("FIRE_EVENT")) {
        parsed.kind = IncomingMessageKind::FireAlarm;
    } else {
        parsed.kind = IncomingMessageKind::Unsupported;
    }
    return parsed;
}
