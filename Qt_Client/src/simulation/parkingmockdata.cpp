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

#include <QJsonArray>
#include <QJsonObject>

QJsonArray ParkingMockData::sampleIncomingMessages()
{
    QJsonArray messages;
    
    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SLOT_OCCUPIED")},
        {QStringLiteral("slot_id"), QStringLiteral("P-01")},
        {QStringLiteral("parking_state"), QStringLiteral("OCCUPIED")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SLOT_VACATED")},
        {QStringLiteral("slot_id"), QStringLiteral("P-02")},
        {QStringLiteral("parking_state"), QStringLiteral("VACANT")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("NON_EV_ALERT")},
        {QStringLiteral("slot_id"), QStringLiteral("EV-01")},
        {QStringLiteral("vehicle_type"), QStringLiteral("NON_EV")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")},
        {QStringLiteral("message"), QStringLiteral("Non-EV vehicle detected")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("OVERTIME_VIOLATION")},
        {QStringLiteral("slot_id"), QStringLiteral("EV-02")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")},
        {QStringLiteral("message"), QStringLiteral("Overtime charging duration exceeded")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("event_id"), QStringLiteral("mock-ev-1")},
        {QStringLiteral("alarm_id"), QStringLiteral("mock-al-1")},
        {QStringLiteral("channel_id"), QStringLiteral("CH2")},
        {QStringLiteral("alarm_kind"), QStringLiteral("FIRE_SUSPECTED")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")},
        {QStringLiteral("active"), true}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("SENSOR_ERROR")},
        {QStringLiteral("slot_id"), QStringLiteral("P-03")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("CAMERA_DISCONNECTED")},
        {QStringLiteral("channel_id"), QStringLiteral("CH1")},
        {QStringLiteral("message"), QStringLiteral("RTSP stream disconnected")},
        {QStringLiteral("alarm_state"), QStringLiteral("OPEN")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("OCR_REQUESTED")},
        {QStringLiteral("slot_id"), QStringLiteral("EV-03")},
        {QStringLiteral("correlation_id"), QStringLiteral("track-123")}
    });

    messages.append(QJsonObject{
        {QStringLiteral("event_type"), QStringLiteral("VEHICLE_CLASSIFIED")},
        {QStringLiteral("slot_id"), QStringLiteral("EV-03")},
        {QStringLiteral("correlation_id"), QStringLiteral("track-123")},
        {QStringLiteral("plate_number"), QStringLiteral("12가3456")},
        {QStringLiteral("vehicle_classification"), QStringLiteral("ELECTRIC")}
    });

    return messages;
}
