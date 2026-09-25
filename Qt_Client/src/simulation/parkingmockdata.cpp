#include "parkingmockdata.h"

namespace {
QString numberedSlotId(const QString &prefix, int number)
{
    return QStringLiteral("%1-%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
}

SlotState mockParkingStateFor(int number)
{
    if (number == 1 || number == 3 || number == 5 || number == 8
        || number == 11 || number == 14) {
        return SlotState::Occupied;
    }
    return SlotState::Vacant;
}
}

ParkingViewState ParkingMockData::initialViewState()
{
    ParkingViewState state;

    for (int number = 1; number <= 16; ++number) {
        const QString slotId = numberedSlotId(QStringLiteral("EV"), number);
        EvSlotInfo slot{slotId, QStringLiteral("-"), true, QStringLiteral("00:00"),
                        SlotState::Vacant, QStringLiteral("NORMAL")};
        slot.visual = deriveSlotVisualState(slot.state, false, slot.isEv, slot.alarmText);
        state.evSlots.insert(slotId, slot);
    }

    EvSlotInfo nonEvAlert{QStringLiteral("EV-01"), QStringLiteral("12A3456"), false,
                          QStringLiteral("00:18"), SlotState::NonEvAlert,
                          QStringLiteral("NON_EV_ALERT")};
    nonEvAlert.visual = deriveSlotVisualState(
        nonEvAlert.state, true, nonEvAlert.isEv, nonEvAlert.alarmText);
    state.evSlots[nonEvAlert.slotId] = nonEvAlert;

    EvSlotInfo overtimeAlert{QStringLiteral("EV-02"), QStringLiteral("34B7788"), true,
                             QStringLiteral("03:42"), SlotState::OvertimeAlert,
                             QStringLiteral("OVERTIME_ALERT")};
    overtimeAlert.visual = deriveSlotVisualState(
        overtimeAlert.state, true, overtimeAlert.isEv, overtimeAlert.alarmText);
    state.evSlots[overtimeAlert.slotId] = overtimeAlert;

    for (int number = 1; number <= 16; ++number) {
        const QString slotId = numberedSlotId(QStringLiteral("P"), number);
        ParkingSlotInfo slot{slotId, mockParkingStateFor(number)};
        slot.visual = deriveSlotVisualState(
            slot.state, slot.state == SlotState::Occupied, false);
        state.parkingSlots.insert(slotId, slot);
    }

    return state;
}

QList<ParkingMockData::EventRecord> ParkingMockData::initialEvents()
{
    return {
        {QStringLiteral("EV-01"), QStringLiteral("NON_EV_ALERT"),
         QStringLiteral("Non-EV vehicle detected in EV charging slot"), QStringLiteral("OPEN")},
        {QStringLiteral("EV-02"), QStringLiteral("OVERTIME_ALERT"),
         QStringLiteral("EV charging dwell time exceeded"), QStringLiteral("OPEN")},
        {QStringLiteral("P-01"), QStringLiteral("OCCUPIED"),
         QStringLiteral("General parking slot occupied"), QStringLiteral("RECORDED")},
        {QStringLiteral("P-02"), QStringLiteral("VACANT"),
         QStringLiteral("General parking slot changed to vacant"), QStringLiteral("RECORDED")},
        {QStringLiteral("P-05"), QStringLiteral("OCCUPIED"),
         QStringLiteral("General parking slot occupied"), QStringLiteral("RECORDED")}
    };
}

QStringList ParkingMockData::sampleIncomingMessages()
{
    return {
        QStringLiteral("PARKING_SLOT,P01,OCCUPIED"),
        QStringLiteral("PARKING_SLOT,P02,VACANT"),
        QStringLiteral("EV_ALERT,EV01,NON_EV"),
        QStringLiteral("EV_ALERT,EV02,OVERTIME"),
        QStringLiteral("FIRE_ALARM,CH2,DETECTED"),
        QStringLiteral("HALL_SENSOR,P03,ERROR"),
        QStringLiteral("EVENT,CH1,CAMERA_DISCONNECTED,FAILED,RTSP stream disconnected")
    };
}
