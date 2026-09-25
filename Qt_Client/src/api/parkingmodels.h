#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

struct ParkingImageResource {
    QUrl url;
    QDateTime timestamp;
    QString role;
    QString processing;
    qint64 imageId = -1;
    qint64 sessionId = -1;
    QString enhancementType;
    QString ocrResult;
    QString evidenceReason;
};

struct ParkingSlotSnapshot {
    QString slotId;
    QString state;
    qint64 sessionId = -1;
    QString plateNumber;
    bool isEv = false;
    bool vehicleTypeKnown = false;
    QDateTime occupiedSince;
    int elapsedSeconds = 0;
    QString alarm;
    QList<ParkingImageResource> images;
};

struct ParkingSnapshot {
    QDateTime generatedAt;
    QList<ParkingSlotSnapshot> parkingSlots;
};
