#include "parkingsimulationservice.h"

#include "controllers/parkingcontroller.h"
#include "parkingmockdata.h"

#include <QRandomGenerator>

ParkingSimulationService::ParkingSimulationService(ParkingController *controller,
                                                     QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
}

void ParkingSimulationService::seedInitialState()
{
    if (!m_controller) return;

    m_controller->replaceViewState(ParkingMockData::initialViewState());
    for (const ParkingMockData::EventRecord &event : ParkingMockData::initialEvents()) {
        m_controller->recordEvent(event.zone, event.eventType, event.message, event.status);
    }
    emit simulationApplied(QStringLiteral("Initial mock state"));
}

void ParkingSimulationService::toggleMockEv()
{
    if (!m_controller) return;

    ++m_mockStep;
    const bool occupied = (m_mockStep % 2) == 0;
    m_controller->applyEvSlotUpdate(
        QStringLiteral("EV-03"),
        occupied ? SlotState::Occupied : SlotState::Vacant,
        occupied ? QStringLiteral("56C9012") : QStringLiteral("-"),
        true,
        occupied ? QStringLiteral("00:07") : QStringLiteral("00:00"),
        QStringLiteral("NORMAL"));
    m_controller->recordEvent(
        QStringLiteral("EV-03"),
        occupied ? QStringLiteral("OCCUPIED") : QStringLiteral("VACANT"),
        QStringLiteral("Mock EV-03 state changed"), QStringLiteral("RECORDED"));
    emit simulationApplied(QStringLiteral("Toggle mock EV"));
}

void ParkingSimulationService::triggerNonEvAlert()
{
    if (!m_controller) return;

    m_controller->applyEvSlotUpdate(
        QStringLiteral("EV-01"), SlotState::NonEvAlert, QStringLiteral("12A3456"),
        false, QStringLiteral("00:19"), QStringLiteral("NON_EV_ALERT"));
    m_controller->recordEvent(
        QStringLiteral("EV-01"), QStringLiteral("NON_EV_ALERT"),
        QStringLiteral("Non-EV alert test executed"), QStringLiteral("OPEN"));
    emit simulationApplied(QStringLiteral("Non-EV violation"));
}

void ParkingSimulationService::triggerOvertimeAlert()
{
    if (!m_controller) return;

    m_controller->applyEvSlotUpdate(
        QStringLiteral("EV-02"), SlotState::OvertimeAlert, QStringLiteral("34B7788"),
        true, QStringLiteral("03:43"), QStringLiteral("OVERTIME_ALERT"));
    m_controller->recordEvent(
        QStringLiteral("EV-02"), QStringLiteral("OVERTIME_ALERT"),
        QStringLiteral("Overtime alert test executed"), QStringLiteral("OPEN"));
    emit simulationApplied(QStringLiteral("Overstay warning"));
}

void ParkingSimulationService::triggerSensorError()
{
    if (!m_controller) return;

    m_controller->applyParkingSlotUpdate(QStringLiteral("P-03"), SlotState::SensorError);
    m_controller->recordEvent(
        QStringLiteral("P-03"), QStringLiteral("HALL_SENSOR_ERROR"),
        QStringLiteral("Hall sensor error test executed"), QStringLiteral("OPEN"));
    emit simulationApplied(QStringLiteral("Hall sensor error"));
}

void ParkingSimulationService::randomizeParkingSlots()
{
    if (!m_controller) return;

    for (int number = 1; number <= 16; ++number) {
        const QString slotId = QStringLiteral("P-%1").arg(number, 2, 10, QLatin1Char('0'));
        const bool occupied = QRandomGenerator::global()->bounded(2) == 1;
        m_controller->applyParkingSlotUpdate(
            slotId, occupied ? SlotState::Occupied : SlotState::Vacant);
        m_controller->recordEvent(
            slotId,
            occupied ? QStringLiteral("OCCUPIED") : QStringLiteral("VACANT"),
            QStringLiteral("Parking slot randomized"), QStringLiteral("RECORDED"));
    }
    emit simulationApplied(QStringLiteral("Randomize parking"));
}

void ParkingSimulationService::runSampleMessages()
{
    if (!m_controller) return;

    for (const QString &message : ParkingMockData::sampleIncomingMessages()) {
        m_controller->processIncomingMessage(message);
    }
    emit simulationApplied(QStringLiteral("RX sample messages"));
}

void ParkingSimulationService::applyManualMessage(const QString &message)
{
    if (!m_controller) return;
    m_controller->processIncomingMessage(message);
    emit simulationApplied(QStringLiteral("Manual RX: %1").arg(message.left(120)));
}
