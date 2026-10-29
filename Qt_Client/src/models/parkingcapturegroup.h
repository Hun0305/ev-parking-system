#pragma once

#include "api/parkingmodels.h"

#include <QList>
#include <QString>
#include <QVector>

struct ParkingCaptureGroup
{
    qint64 imageId = -1;
    qint64 sessionId = -1;
    QDateTime timestamp;
    QString reason;
    QString ocrResult;
    QList<ParkingImageResource> variants;
};

QVector<ParkingCaptureGroup> buildParkingCaptureGroups(
    const QList<ParkingImageResource> &images);

const ParkingImageResource *parkingCaptureVariant(
    const ParkingCaptureGroup &capture,
    const QString &processing);

const ParkingImageResource *preferredParkingCaptureVariant(
    const ParkingCaptureGroup &capture);
