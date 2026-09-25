#include "controllers/parkingcontroller.h"
#include "simulation/parkingsimulationservice.h"

#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingController controller(
        directory.filePath(QStringLiteral("client_config.ini")),
        directory.filePath(QStringLiteral("client_config.local.ini")));
    ParkingSimulationService simulation(&controller);

    int eventCount = 0;
    QObject::connect(&controller, &ParkingController::eventLogged,
                     &app, [&eventCount](const MonitoringEvent &) { ++eventCount; });

    simulation.seedInitialState();
    const ParkingViewState &initialState = controller.state();
    if (initialState.evSlots.size() != 16 || initialState.parkingSlots.size() != 16) return 3;
    if (initialState.evSlots.value(QStringLiteral("EV-01")).state != SlotState::NonEvAlert) return 4;
    if (initialState.evSlots.value(QStringLiteral("EV-02")).state != SlotState::OvertimeAlert) return 5;
    if (initialState.parkingSlots.value(QStringLiteral("P-01")).state != SlotState::Occupied) return 6;
    if (initialState.parkingSlots.value(QStringLiteral("P-02")).state != SlotState::Vacant) return 7;
    if (eventCount != 5) return 8;

    simulation.toggleMockEv();
    simulation.toggleMockEv();
    if (controller.slotState(QStringLiteral("EV-03")) != SlotState::Occupied) return 9;
    if (controller.plateNumber(QStringLiteral("EV-03")) != QStringLiteral("56C9012")) return 10;

    simulation.triggerSensorError();
    if (controller.slotState(QStringLiteral("P-03")) != SlotState::SensorError) return 11;

    simulation.applyManualMessage(QStringLiteral("{\"event_type\": \"SLOT_OCCUPIED\", \"slot_id\": \"P-04\", \"parking_state\": \"OCCUPIED\"}"));
    if (controller.slotState(QStringLiteral("P-04")) != SlotState::Occupied) return 12;

    simulation.runSampleMessages();
    if (controller.slotState(QStringLiteral("EV-01")) != SlotState::NonEvAlert) return 13;
    if (controller.slotState(QStringLiteral("EV-02")) != SlotState::OvertimeAlert) return 14;
    if (controller.slotState(QStringLiteral("P-03")) != SlotState::SensorError) return 15;
    if (controller.state().fireChannels
        != QSet<QString>{QStringLiteral("CH2")}) return 16;

    if (slotStateFromText(QStringLiteral("FIRE_SUSPECTED"))
        != SlotState::Vacant) return 17;
    const SlotVisualState fireVisual = deriveSlotVisualState(
        SlotState::Occupied, false, false, QStringLiteral("FIRE_SUSPECTED"));
    if (fireVisual.alarm != SlotAlarmKind::None) return 18;
    if (slotAlarmText(fireVisual.alarm) != QStringLiteral("NORMAL")) return 19;

    return 0;
}
