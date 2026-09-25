#include "pages/parkingmappage.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QComboBox>
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
    QString requestedSlotId;
    QObject::connect(&page, &ParkingMapPage::slotDetailRequested, &app,
                     [&](const QString &slotId) { requestedSlotId = slotId; });
    QLabel *dataStatus = statusLabel(page, "runtimeDataStatusLabel");
    QLabel *vehicle = statusLabel(page, "runtimeVehicleLabel");
    QLabel *vehicleImage = statusLabel(page, "runtimeVehicleImageLabel");
    QLabel *occupiedSince = statusLabel(page, "runtimeOccupiedSinceLabel");
    QLabel *occupiedTime = statusLabel(page, "runtimeOccupiedTimeLabel");
    QLabel *alarm = statusLabel(page, "runtimeAlarmLabel");
    QLabel *alarmState = statusLabel(page, "runtimeAlarmStateLabel");
    QLabel *selectedState = statusLabel(page, "selectedSlotStateLabel");
    QLabel *summaryTotal = statusLabel(page, "parkingSummaryTotal");
    QLabel *summaryVacant = statusLabel(page, "parkingSummaryVacant");
    QLabel *summaryOccupied = statusLabel(page, "parkingSummaryOccupied");
    QLabel *summaryAlert = statusLabel(page, "parkingSummaryAlert");
    QLabel *overviewTotal = statusLabel(page, "parkingOverviewSummaryTotal");
    QLabel *overviewVacant = statusLabel(page, "parkingOverviewSummaryVacant");
    QLabel *overviewOccupied = statusLabel(page, "parkingOverviewSummaryOccupied");
    QLabel *overviewAlert = statusLabel(page, "parkingOverviewSummaryAlert");
    QLabel *filterResult = statusLabel(page, "parkingFilterResultLabel");
    QLineEdit *search = page.findChild<QLineEdit *>(QStringLiteral("parkingZoneSearchEdit"));
    QComboBox *stateFilter = page.findChild<QComboBox *>(QStringLiteral("parkingStateFilterCombo"));
    QPushButton *undoButton = page.findChild<QPushButton *>(QStringLiteral("undoLayoutButton"));
    if (!dataStatus || !vehicle || !vehicleImage || !occupiedSince
        || !occupiedTime || !alarm
        || !alarmState || !selectedState || !summaryTotal || !summaryOccupied
        || !summaryVacant || !summaryAlert || !overviewTotal || !overviewVacant
        || !overviewOccupied || !overviewAlert || !filterResult || !search
        || !stateFilter || !undoButton) return 3;
    if (statusLabel(page, "runtimePlateLabel")
        || statusLabel(page, "runtimeLastUpdatedLabel")) return 28;

    if (!selectRow(page, 0)) return 4;
    if (requestedSlotId != QStringLiteral("EV-01")) return 35;
    if (dataStatus->text() != QStringLiteral("WAITING DATA")
        || occupiedSince->text() != QStringLiteral("-")
        || occupiedTime->text() != QStringLiteral("-")) return 5;

    ParkingViewState state;
    EvSlotInfo occupied;
    occupied.slotId = QStringLiteral("EV-01");
    occupied.plateNumber = QStringLiteral("12A3456");
    occupied.isEv = false;
    occupied.occupiedTime = QStringLiteral("00:19:32");
    occupied.occupiedSince = QDateTime::currentDateTime().addSecs(-1172);
    occupied.lastUpdatedAt = QDateTime::currentDateTime();
    occupied.eventId = QStringLiteral("evt-ev-01");
    occupied.state = SlotState::NonEvAlert;
    occupied.alarmText = QStringLiteral("NON_EV");
    occupied.visual = deriveSlotVisualState(
        occupied.state, true, occupied.isEv, occupied.alarmText);
    state.evSlots.insert(occupied.slotId, occupied);
    page.render(state);

    if (dataStatus->text() != QStringLiteral("AVAILABLE")) return 6;
    if (selectedState->text() != QStringLiteral("NON_EV_ALERT")) return 7;
    if (vehicle->text() != QStringLiteral("GENERAL CAR · 12A3456")) return 8;
    if (occupiedSince->text() == QStringLiteral("-")) return 9;
    if (occupiedTime->text() == QStringLiteral("-")
        || !occupiedTime->text().startsWith(QStringLiteral("00:19:"))) return 10;
    if (vehicleImage->text() != QStringLiteral("No vehicle image")) return 30;
    if (alarm->text() != QStringLiteral("NON-EV")) return 11;
    if (alarmState->text() != QStringLiteral("ACTIVE")) return 12;
    if (summaryTotal->text() != QStringLiteral("8")
        || summaryVacant->text() != QStringLiteral("0")
        || summaryOccupied->text() != QStringLiteral("1")
        || summaryAlert->text() != QStringLiteral("1")) return 31;
    if (overviewTotal->text() != QStringLiteral("8")
        || overviewVacant->text() != QStringLiteral("0")
        || overviewOccupied->text() != QStringLiteral("1")
        || overviewAlert->text() != QStringLiteral("1")) return 34;
    search->setText(QStringLiteral("EV-01"));
    if (filterResult->text() != QStringLiteral("1 of 8 zones")) return 32;
    search->clear();
    stateFilter->setCurrentIndex(4);
    if (filterResult->text() != QStringLiteral("1 of 8 zones")) return 33;
    stateFilter->setCurrentIndex(0);
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
    if (occupiedSince->text() != QStringLiteral("-")
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
    if (occupiedSince->text() != QStringLiteral("-")
        || occupiedTime->text() != QStringLiteral("-")) return 21;
    if (unmapped.evSlots.value(QStringLiteral("slot_01")).plateNumber
        != QStringLiteral("12A3456")) return 22;

    ParkingViewState generalState;
    ParkingSlotInfo general;
    general.slotId = QStringLiteral("P-01");
    general.state = SlotState::Occupied;
    general.visual = deriveSlotVisualState(general.state, true, false);
    general.occupiedTime = QStringLiteral("00:05:10");
    general.occupiedSince = QDateTime::currentDateTime().addSecs(-310);
    general.lastUpdatedAt = QDateTime::currentDateTime();
    generalState.parkingSlots.insert(general.slotId, general);
    generalState.slotPlateNumbers.insert(general.slotId,
                                         QStringLiteral("34C5678"));
    page.render(generalState);
    if (!selectRow(page, 4)) return 23;
    if (requestedSlotId != QStringLiteral("P-01")) return 36;
    if (dataStatus->text() != QStringLiteral("AVAILABLE")) return 24;
    if (vehicle->text() != QStringLiteral("GENERAL CAR · 34C5678")) return 25;
    if (occupiedSince->text() == QStringLiteral("-")
        || !occupiedTime->text().startsWith(QStringLiteral("00:05:"))) return 26;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 27;

    return 0;
}
