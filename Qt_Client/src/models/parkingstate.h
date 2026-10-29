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

enum class OcrStatus {
    None,
    Requested,
    Completed,
    Unrecognized
};

struct SlotVisualState {
    SlotOccupancy occupancy = SlotOccupancy::Unknown;
    VehicleClass vehicleClass = VehicleClass::Unknown;
    SlotAlarmKind alarm = SlotAlarmKind::None;
    bool alarmAcknowledged = false;
    OcrStatus ocrStatus = OcrStatus::None;
    QString correlationId;
};

struct EvSlotInfo {
    QString slotId;
    QString plateNumber;
    bool isEv = false;
    QString occupiedTime;
    SlotState state = SlotState::Vacant;
    QString alarmText;
    QString correlationId;
    OcrStatus ocrStatus = OcrStatus::None;
    SlotVisualState visual;
    QDateTime occupiedSince;
    QDateTime lastUpdatedAt;
    QString eventId;
};

struct ParkingSlotInfo {
    QString slotId;
    SlotState state = SlotState::Vacant;
    QString correlationId;
    OcrStatus ocrStatus = OcrStatus::None;
    SlotVisualState visual;
    QString occupiedTime;
    QDateTime occupiedSince;
    QDateTime lastUpdatedAt;
    QString eventId;
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
    // Number of unique slots accepted from the latest server snapshot.
    // Live MQTT/manual updates may change the maps above, but must not change
    // the Dashboard's server-reported capacity.
    int serverSlotCount = 0;
    bool hasServerSnapshot = false;
    QHash<QString, QList<ParkingImageResource>> slotImages;
    QHash<QString, QString> slotPlateNumbers;
    QHash<QString, qint64> slotSessionIds;
    // Fire belongs to a camera channel, never to an individual parking slot.
    QSet<QString> fireChannels;
    QHash<QString, ChannelFireAlarmState> fireAlarms;
    QDateTime generatedAt;
    bool apiEnabled = false;
};

QString slotStateText(SlotState state);
QString slotStateStyle(SlotState state);
SlotState slotStateFromText(const QString &text);
SlotAlarmKind slotAlarmKindFromText(const QString &text, SlotState fallbackState = SlotState::Vacant);
SlotVisualState deriveSlotVisualState(SlotState state, bool vehicleTypeKnown,
                                      bool isEv, const QString &alarmText = QString(),
                                      OcrStatus ocrStatus = OcrStatus::None,
                                      const QString &correlationId = QString());
QString slotAlarmText(SlotAlarmKind alarm);
QString vehicleClassText(VehicleClass vehicleClass);
QString normalizeParkingSlotId(const QString &rawSlotId);

#endif
