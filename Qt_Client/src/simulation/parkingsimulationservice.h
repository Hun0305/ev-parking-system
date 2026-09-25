#pragma once

#include <QObject>
#include <QString>

class ParkingController;

class ParkingSimulationService : public QObject
{
    Q_OBJECT

public:
    explicit ParkingSimulationService(ParkingController *controller,
                                      QObject *parent = nullptr);

    void seedInitialState();

public slots:
    void toggleMockEv();
    void triggerNonEvAlert();
    void triggerOvertimeAlert();
    void triggerSensorError();
    void randomizeParkingSlots();
    void runSampleMessages();
    void applyManualMessage(const QString &message);

signals:
    void simulationApplied(const QString &scenario);

private:
    ParkingController *m_controller = nullptr;
    int m_mockStep = 0;
};
