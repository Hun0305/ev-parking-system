#include "adapters/incomingmessageadapter.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ParsedIncomingMessage parsed = IncomingMessageAdapter::parse(QStringLiteral("INVALID"));
    if (parsed.isValid()) return 1;
    if (!parsed.errorMessage.startsWith(QStringLiteral("Invalid message format"))) return 2;

    parsed = IncomingMessageAdapter::parse(
        QStringLiteral(" EVENT, P03, hall_sensor_error, open, sensor, disconnected "));
    if (parsed.kind != IncomingMessageKind::NormalizedEvent) return 3;
    if (parsed.sourceId != QStringLiteral("P-03")) return 4;
    if (parsed.eventType != QStringLiteral("HALL_SENSOR_ERROR")) return 5;
    if (parsed.status != QStringLiteral("OPEN")) return 6;
    if (parsed.message != QStringLiteral("sensor, disconnected")) return 7;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("DB_EVENT,P01,TEST_EVENT,DONE"));
    if (parsed.kind != IncomingMessageKind::NormalizedEvent) return 8;
    if (parsed.message != QStringLiteral("Normalized event received")) return 9;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("PARKING_SLOT,P04,OCCUPIED"));
    if (parsed.kind != IncomingMessageKind::ParkingSlot) return 10;
    if (parsed.sourceId != QStringLiteral("P-04")) return 11;
    if (parsed.value != QStringLiteral("OCCUPIED")) return 12;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("HALL_SENSOR_EVENT,P03,ERROR"));
    if (parsed.kind != IncomingMessageKind::HallSensor) return 13;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("EV_ALERT,EV01,NON_EV"));
    if (parsed.kind != IncomingMessageKind::EvAlert) return 14;
    if (parsed.sourceId != QStringLiteral("EV-01")) return 15;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("FIRE_EVENT,CH2,DETECTED"));
    if (parsed.kind != IncomingMessageKind::FireAlarm) return 16;
    if (parsed.sourceId != QStringLiteral("CH2")) return 17;

    parsed = IncomingMessageAdapter::parse(QStringLiteral("UNKNOWN,P01,VALUE"));
    if (parsed.kind != IncomingMessageKind::Unsupported) return 18;
    if (!parsed.isValid()) return 19;

    return 0;
}
