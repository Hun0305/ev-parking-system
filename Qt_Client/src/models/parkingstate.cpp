#include "parkingstate.h"

QString normalizeParkingSlotId(const QString &rawSlotId)
{
    QString normalized = rawSlotId.trimmed().toUpper();
    if (normalized.startsWith(QStringLiteral("EV"))
        && !normalized.contains(QLatin1Char('-')) && normalized.size() >= 4) {
        normalized.insert(2, QLatin1Char('-'));
    } else if (normalized.startsWith(QLatin1Char('P'))
               && !normalized.contains(QLatin1Char('-')) && normalized.size() >= 3) {
        normalized.insert(1, QLatin1Char('-'));
    }
    return normalized;
}

SlotState slotStateFromText(const QString &text)
{
    const QString normalized = text.trimmed().toUpper();
    if (normalized == QStringLiteral("OCCUPIED")) return SlotState::Occupied;
    if (normalized == QStringLiteral("HALL_SENSOR_ERROR")
        || normalized == QStringLiteral("SENSOR_ERROR")
        || normalized == QStringLiteral("ERROR")) {
        return SlotState::SensorError;
    }
    if (normalized == QStringLiteral("NON_EV") || normalized == QStringLiteral("NON_EV_ALERT")) {
        return SlotState::NonEvAlert;
    }
    if (normalized == QStringLiteral("OVERTIME") || normalized == QStringLiteral("OVERTIME_ALERT")) {
        return SlotState::OvertimeAlert;
    }
    if (normalized == QStringLiteral("ACKED")) return SlotState::Acked;
    return SlotState::Vacant;
}

SlotAlarmKind slotAlarmKindFromText(const QString &text, SlotState fallbackState)
{
    const QString normalized = text.trimmed().toUpper();
    if (normalized == QStringLiteral("EV_ZONE_VIOLATION")
        || normalized == QStringLiteral("NON_EV")
        || normalized == QStringLiteral("NON_EV_ALERT")) {
        return SlotAlarmKind::NonEvViolation;
    }
    if (normalized == QStringLiteral("OVERSTAY")
        || normalized == QStringLiteral("OVERTIME")
        || normalized == QStringLiteral("OVERTIME_ALERT")) {
        return SlotAlarmKind::Overstay;
    }
    if (normalized == QStringLiteral("HALL_SENSOR_ERROR")
        || normalized == QStringLiteral("SENSOR_ERROR")
        || normalized == QStringLiteral("ERROR")) {
        return SlotAlarmKind::SensorError;
    }
    // Fire markers in parking-slot payloads are ignored. Channel fire state
    // is parsed separately and must never overwrite a parking-slot alarm.
    if (normalized == QStringLiteral("FIRE")
        || normalized == QStringLiteral("FIRE_SUSPECTED")) {
        return SlotAlarmKind::None;
    }

    switch (fallbackState) {
    case SlotState::NonEvAlert: return SlotAlarmKind::NonEvViolation;
    case SlotState::OvertimeAlert: return SlotAlarmKind::Overstay;
    case SlotState::SensorError: return SlotAlarmKind::SensorError;
    case SlotState::Vacant:
    case SlotState::Occupied:
    case SlotState::Acked:
        return SlotAlarmKind::None;
    }
    return SlotAlarmKind::None;
}

SlotVisualState deriveSlotVisualState(SlotState state, bool vehicleTypeKnown,
                                      bool isEv, const QString &alarmText)
{
    SlotVisualState visual;
    visual.alarm = slotAlarmKindFromText(alarmText, state);
    visual.alarmAcknowledged = state == SlotState::Acked;

    if (state == SlotState::Vacant) {
        visual.occupancy = SlotOccupancy::Vacant;
        return visual;
    }
    if (state == SlotState::SensorError) {
        visual.occupancy = SlotOccupancy::Unknown;
        return visual;
    }

    visual.occupancy = SlotOccupancy::Occupied;
    if (vehicleTypeKnown) {
        visual.vehicleClass = isEv ? VehicleClass::Electric : VehicleClass::General;
    }
    return visual;
}

QString slotAlarmText(SlotAlarmKind alarm)
{
    switch (alarm) {
    case SlotAlarmKind::None: return QStringLiteral("NORMAL");
    case SlotAlarmKind::NonEvViolation: return QStringLiteral("NON-EV");
    case SlotAlarmKind::Overstay: return QStringLiteral("OVERSTAY");
    case SlotAlarmKind::SensorError: return QStringLiteral("SENSOR");
    }
    return QStringLiteral("NORMAL");
}

QString vehicleClassText(VehicleClass vehicleClass)
{
    switch (vehicleClass) {
    case VehicleClass::Electric: return QStringLiteral("EV CAR");
    case VehicleClass::General: return QStringLiteral("GENERAL CAR");
    case VehicleClass::Unknown: return QStringLiteral("UNKNOWN CAR");
    }
    return QStringLiteral("UNKNOWN CAR");
}

QString slotStateText(SlotState state)
{
    switch (state) {
    case SlotState::Vacant: return QStringLiteral("VACANT");
    case SlotState::Occupied: return QStringLiteral("OCCUPIED");
    case SlotState::NonEvAlert: return QStringLiteral("NON_EV_ALERT");
    case SlotState::OvertimeAlert: return QStringLiteral("OVERTIME_ALERT");
    case SlotState::SensorError: return QStringLiteral("HALL_SENSOR_ERROR");
    case SlotState::Acked: return QStringLiteral("ACKED");
    }
    return QStringLiteral("UNKNOWN");
}

QString slotStateStyle(SlotState state)
{
    QString background;
    QString foreground = QStringLiteral("#111111");
    switch (state) {
    case SlotState::Vacant: background = QStringLiteral("#2ecc71"); break;
    case SlotState::Occupied: background = QStringLiteral("#8da2b5"); break;
    case SlotState::NonEvAlert:
        background = QStringLiteral("#e53935"); foreground = QStringLiteral("#ffffff"); break;
    case SlotState::OvertimeAlert: background = QStringLiteral("#fb8c00"); break;
    case SlotState::SensorError:
        background = QStringLiteral("#7e57c2"); foreground = QStringLiteral("#ffffff"); break;
    case SlotState::Acked: background = QStringLiteral("#b0bec5"); break;
    }
    return QStringLiteral("QFrame { background: %1; border: 2px solid #263238; border-radius: 6px; }"
                          "QLabel { color: %2; }").arg(background, foreground);
}
