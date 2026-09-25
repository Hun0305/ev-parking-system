#include "pages/parkingmappage.h"

#include <QApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QTemporaryDir>

namespace {

QLabel *statusLabel(ParkingMapPage &page, const char *objectName)
{
    return page.findChild<QLabel *>(QString::fromLatin1(objectName));
}

bool selectRow(ParkingMapPage &page, int row)
{
    return QMetaObject::invokeMethod(&page, "handleZoneTableClicked",
                                     Qt::DirectConnection,
                                     Q_ARG(int, row), Q_ARG(int, 0));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingMapPage page(directory.filePath(QStringLiteral("parking_map_layout.json")));
    QLabel *dataStatus = statusLabel(page, "runtimeDataStatusLabel");
    QLabel *vehicle = statusLabel(page, "runtimeVehicleLabel");
    QLabel *plate = statusLabel(page, "runtimePlateLabel");
    QLabel *occupiedTime = statusLabel(page, "runtimeOccupiedTimeLabel");
    QLabel *alarm = statusLabel(page, "runtimeAlarmLabel");
    QLabel *alarmState = statusLabel(page, "runtimeAlarmStateLabel");
    QLabel *selectedState = statusLabel(page, "selectedSlotStateLabel");
    QPushButton *undoButton = page.findChild<QPushButton *>(QStringLiteral("undoLayoutButton"));
    if (!dataStatus || !vehicle || !plate || !occupiedTime || !alarm
        || !alarmState || !selectedState || !undoButton) return 3;

    if (!selectRow(page, 0)) return 4;
    if (dataStatus->text() != QStringLiteral("WAITING DATA")
        || plate->text() != QStringLiteral("-")
        || occupiedTime->text() != QStringLiteral("-")) return 5;

    ParkingViewState state;
    EvSlotInfo occupied;
    occupied.slotId = QStringLiteral("EV-01");
    occupied.plateNumber = QStringLiteral("12A3456");
    occupied.isEv = false;
    occupied.occupiedTime = QStringLiteral("00:19:32");
    occupied.state = SlotState::NonEvAlert;
    occupied.alarmText = QStringLiteral("NON_EV");
    occupied.visual = deriveSlotVisualState(
        occupied.state, true, occupied.isEv, occupied.alarmText);
    state.evSlots.insert(occupied.slotId, occupied);
    page.render(state);

    if (dataStatus->text() != QStringLiteral("AVAILABLE")) return 6;
    if (selectedState->text() != QStringLiteral("NON_EV_ALERT")) return 7;
    if (vehicle->text() != QStringLiteral("GENERAL CAR")) return 8;
    if (plate->text() != QStringLiteral("12***56")) return 9;
    if (occupiedTime->text() != QStringLiteral("00:19:32")) return 10;
    if (alarm->text() != QStringLiteral("NON-EV")) return 11;
    if (alarmState->text() != QStringLiteral("ACTIVE")) return 12;
    if (state.evSlots.value(QStringLiteral("EV-01")).plateNumber
        != QStringLiteral("12A3456")) return 13;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 14;

    EvSlotInfo acknowledged = occupied;
    acknowledged.state = SlotState::Acked;
    acknowledged.visual.alarm = SlotAlarmKind::NonEvViolation;
    acknowledged.visual.alarmAcknowledged = true;
    state.evSlots[acknowledged.slotId] = acknowledged;
    page.render(state);
    if (alarm->text() != QStringLiteral("NON-EV")
        || alarmState->text() != QStringLiteral("ACK")) return 15;

    EvSlotInfo vacant = occupied;
    vacant.state = SlotState::Vacant;
    vacant.alarmText = QStringLiteral("NORMAL");
    vacant.visual = deriveSlotVisualState(
        vacant.state, false, false, vacant.alarmText);
    state.evSlots[vacant.slotId] = vacant;
    page.render(state);
    if (vehicle->text() != QStringLiteral("VACANT")) return 16;
    if (plate->text() != QStringLiteral("-")
        || occupiedTime->text() != QStringLiteral("-")) return 17;
    if (alarm->text() != QStringLiteral("NORMAL")
        || alarmState->text() != QStringLiteral("NONE")) return 18;

    ParkingViewState unmapped;
    EvSlotInfo serverOnly = occupied;
    serverOnly.slotId = QStringLiteral("slot_01");
    unmapped.evSlots.insert(serverOnly.slotId, serverOnly);
    page.render(unmapped);
    if (dataStatus->text() != QStringLiteral("WAITING DATA")) return 19;
    if (selectedState->text() != QStringLiteral("WAITING DATA")) return 20;
    if (plate->text() != QStringLiteral("-")
        || occupiedTime->text() != QStringLiteral("-")) return 21;
    if (unmapped.evSlots.value(QStringLiteral("slot_01")).plateNumber
        != QStringLiteral("12A3456")) return 22;

    ParkingViewState generalState;
    ParkingSlotInfo general;
    general.slotId = QStringLiteral("P-01");
    general.state = SlotState::Occupied;
    general.visual = deriveSlotVisualState(general.state, true, false);
    generalState.parkingSlots.insert(general.slotId, general);
    page.render(generalState);
    if (!selectRow(page, 4)) return 23;
    if (dataStatus->text() != QStringLiteral("AVAILABLE")) return 24;
    if (vehicle->text() != QStringLiteral("GENERAL CAR")) return 25;
    if (plate->text() != QStringLiteral("N/A")
        || occupiedTime->text() != QStringLiteral("N/A")) return 26;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 27;

    return 0;
}
