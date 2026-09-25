#ifndef PARKINGSTATE_H
#define PARKINGSTATE_H

#include "api/parkingmodels.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

enum class SlotState {
    Vacant,
    Occupied,
    NonEvAlert,
    OvertimeAlert,
    SensorError,
    Acked
};

enum class SlotOccupancy {
    Unknown,
    Vacant,
    Occupied
};

enum class VehicleClass {
    Unknown,
    Electric,
    General
};

enum class SlotAlarmKind {
    None,
    NonEvViolation,
    Overstay,
    SensorError
};

struct SlotVisualState {
    SlotOccupancy occupancy = SlotOccupancy::Unknown;
    VehicleClass vehicleClass = VehicleClass::Unknown;
    SlotAlarmKind alarm = SlotAlarmKind::None;
    bool alarmAcknowledged = false;
};

struct EvSlotInfo {
    QString slotId;
    QString plateNumber;
    bool isEv = false;
    QString occupiedTime;
    SlotState state = SlotState::Vacant;
    QString alarmText;
    SlotVisualState visual;
};

struct ParkingSlotInfo {
    QString slotId;
    SlotState state = SlotState::Vacant;
    SlotVisualState visual;
};

struct ChannelFireAlarmState {
    QString alarmId;
    QString alarmState;
    QString ackState;
    bool active = false;
    bool acknowledged = false;
};

struct ParkingViewState {
    QHash<QString, EvSlotInfo> evSlots;
    QHash<QString, ParkingSlotInfo> parkingSlots;
    QHash<QString, QList<ParkingImageResource>> slotImages;
    QHash<QString, QString> slotPlateNumbers;
    // Fire belongs to a camera channel, never to an individual parking slot.
    QSet<QString> fireChannels;
    QHash<QString, ChannelFireAlarmState> fireAlarms;
    bool apiEnabled = false;
};

QString slotStateText(SlotState state);
QString slotStateStyle(SlotState state);
SlotState slotStateFromText(const QString &text);
SlotAlarmKind slotAlarmKindFromText(const QString &text, SlotState fallbackState = SlotState::Vacant);
SlotVisualState deriveSlotVisualState(SlotState state, bool vehicleTypeKnown,
                                      bool isEv, const QString &alarmText = QString());
QString slotAlarmText(SlotAlarmKind alarm);
QString vehicleClassText(VehicleClass vehicleClass);
QString normalizeParkingSlotId(const QString &rawSlotId);

#endif
