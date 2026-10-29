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
    // One-based position in the channel's fixed operator row. Scene geometry
    // is derived from this value so configuration does not need x/y values.
    int slotOrder = 0;
    QRectF rect;
    double rotation = 0.0;
    QString cameraChannel;
    QString ivaAreaId;
    QString hallSensorId;
    bool enabled = true;
};

using ParkingChannelDisplayNames = QHash<QString, QString>;

struct ParkingOverviewChannelLayout {
    QString role;
    int slotCount = 0;
};

struct ParkingOverviewCameraLayout {
    int cameraNumber = 0;
    int gridRow = 0;
    int gridColumn = 0;
    QHash<QString, ParkingOverviewChannelLayout> channels;
};

using ParkingOverviewLayouts = QList<ParkingOverviewCameraLayout>;

QList<ParkingZoneLayout> defaultParkingZoneLayout();
ParkingOverviewLayouts defaultParkingOverviewLayouts();
bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones, QString *errorMessage);
bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones,
                           ParkingChannelDisplayNames *channelDisplayNames,
                           QString *errorMessage);
bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones,
                           ParkingChannelDisplayNames *channelDisplayNames,
                           ParkingOverviewLayouts *overviewLayouts,
                           QString *errorMessage);
bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones, QString *errorMessage);
bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones,
                           const ParkingChannelDisplayNames &channelDisplayNames,
                           QString *errorMessage);
bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones,
                           const ParkingChannelDisplayNames &channelDisplayNames,
                           const ParkingOverviewLayouts &overviewLayouts,
                           QString *errorMessage);

#endif
