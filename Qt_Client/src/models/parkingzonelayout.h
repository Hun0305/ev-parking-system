#ifndef PARKINGZONELAYOUT_H
#define PARKINGZONELAYOUT_H

#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>

struct ParkingZoneLayout {
    QString zoneId;
    QString zoneType;
    QString displayName;
    QRectF rect;
    double rotation = 0.0;
    QString cameraChannel;
    QString ivaAreaId;
    QString hallSensorId;
    bool enabled = true;
};

using ParkingChannelDisplayNames = QHash<QString, QString>;

QList<ParkingZoneLayout> defaultParkingZoneLayout();
bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones, QString *errorMessage);
bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones,
                           ParkingChannelDisplayNames *channelDisplayNames,
                           QString *errorMessage);
bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones, QString *errorMessage);
bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones,
                           const ParkingChannelDisplayNames &channelDisplayNames,
                           QString *errorMessage);

#endif
