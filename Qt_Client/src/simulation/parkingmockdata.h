#pragma once

#include "models/parkingstate.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace ParkingMockData {

struct EventRecord {
    QString zone;
    QString eventType;
    QString message;
    QString status;
};

ParkingViewState initialViewState();
QList<EventRecord> initialEvents();
QStringList sampleIncomingMessages();

} // namespace ParkingMockData
