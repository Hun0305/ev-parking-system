#pragma once

#include "parkingmodels.h"

class QJsonDocument;

class ParkingResponseParser
{
public:
    static bool parseSnapshot(const QJsonDocument &document,
                              ParkingSnapshot &snapshot,
                              QString &errorMessage);
    static bool parseSlotDetail(const QJsonDocument &document,
                                ParkingSlotSnapshot &slot,
                                QString &errorMessage);
    static bool parseSessionImages(const QJsonDocument &document,
                                   QList<ParkingImageResource> &images,
                                   QString &errorMessage);
};
