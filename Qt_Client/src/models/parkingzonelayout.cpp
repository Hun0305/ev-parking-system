#include "parkingzonelayout.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
ParkingZoneLayout makeZone(const QString &zoneId, const QString &zoneType, const QRectF &rect,
                           const QString &cameraChannel, const QString &ivaAreaId,
                           const QString &hallSensorId)
{
    ParkingZoneLayout zone;
    zone.zoneId = zoneId;
    zone.zoneType = zoneType;
    zone.displayName = zoneId;
    zone.rect = rect;
    zone.cameraChannel = cameraChannel;
    zone.ivaAreaId = ivaAreaId;
    zone.hallSensorId = hallSensorId;
    return zone;
}

QRectF miniStallRect(double x, double y)
{
    return QRectF(x, y, 84, 58);
}

QString numberedZoneId(const QString &prefix, int number)
{
    return QStringLiteral("%1-%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
}

QString hallSensorIdFor(const QString &prefix, int number)
{
    return QStringLiteral("HALL_%1_%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
}

void appendChannelTestLayout(QList<ParkingZoneLayout> *zones, int channelNumber,
                             int firstEvNumber, int firstParkingNumber,
                             double panelX, double panelY)
{
    if (!zones) return;
    const QString channel = QStringLiteral("CH%1").arg(channelNumber);
    for (int index = 0; index < 4; ++index) {
        const int evNumber = firstEvNumber + index;
        zones->append(makeZone(
            numberedZoneId(QStringLiteral("EV"), evNumber),
            QStringLiteral("EV"),
            miniStallRect(panelX + 34 + (index * 96), panelY + 60),
            channel,
            QStringLiteral("IVA%1").arg(index + 1),
            hallSensorIdFor(QStringLiteral("EV"), evNumber)));
    }
    for (int index = 0; index < 4; ++index) {
        const int parkingNumber = firstParkingNumber + index;
        zones->append(makeZone(
            numberedZoneId(QStringLiteral("P"), parkingNumber),
            QStringLiteral("GENERAL"),
            miniStallRect(panelX + 34 + (index * 96), panelY + 154),
            channel,
            QString(),
            hallSensorIdFor(QStringLiteral("P"), parkingNumber)));
    }
}

QJsonObject zoneToJson(const ParkingZoneLayout &zone)
{
    QJsonObject object;
    object.insert(QStringLiteral("zone_id"), zone.zoneId);
    object.insert(QStringLiteral("zone_type"), zone.zoneType);
    object.insert(QStringLiteral("display_name"), zone.displayName);
    object.insert(QStringLiteral("x"), zone.rect.x());
    object.insert(QStringLiteral("y"), zone.rect.y());
    object.insert(QStringLiteral("width"), zone.rect.width());
    object.insert(QStringLiteral("height"), zone.rect.height());
    object.insert(QStringLiteral("rotation"), zone.rotation);
    object.insert(QStringLiteral("camera_channel"), zone.cameraChannel);
    object.insert(QStringLiteral("iva_area_id"), zone.ivaAreaId);
    object.insert(QStringLiteral("hall_sensor_id"), zone.hallSensorId);
    object.insert(QStringLiteral("enabled"), zone.enabled);
    return object;
}

ParkingZoneLayout zoneFromJson(const QJsonObject &object)
{
    ParkingZoneLayout zone;
    zone.zoneId = object.value(QStringLiteral("zone_id")).toString().trimmed().toUpper();
    zone.zoneType = object.value(QStringLiteral("zone_type")).toString(QStringLiteral("GENERAL")).trimmed().toUpper();
    zone.displayName = object.value(QStringLiteral("display_name")).toString(zone.zoneId).trimmed();
    zone.rect = QRectF(
        object.value(QStringLiteral("x")).toDouble(),
        object.value(QStringLiteral("y")).toDouble(),
        object.value(QStringLiteral("width")).toDouble(84.0),
        object.value(QStringLiteral("height")).toDouble(58.0));
    zone.rotation = object.value(QStringLiteral("rotation")).toDouble();
    zone.cameraChannel = object.value(QStringLiteral("camera_channel")).toString().trimmed().toUpper();
    zone.ivaAreaId = object.value(QStringLiteral("iva_area_id")).toString().trimmed().toUpper();
    zone.hallSensorId = object.value(QStringLiteral("hall_sensor_id")).toString().trimmed().toUpper();
    zone.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    if (zone.displayName.isEmpty()) {
        zone.displayName = zone.zoneId;
    }
    if (zone.zoneType != QStringLiteral("EV")) {
        zone.zoneType = QStringLiteral("GENERAL");
        zone.ivaAreaId.clear();
    } else if (zone.ivaAreaId != QStringLiteral("IVA1")
               && zone.ivaAreaId != QStringLiteral("IVA2")
               && zone.ivaAreaId != QStringLiteral("IVA3")
               && zone.ivaAreaId != QStringLiteral("IVA4")) {
        zone.ivaAreaId = QStringLiteral("IVA1");
    }
    return zone;
}

bool isCameraChannelId(const QString &value)
{
    return value == QStringLiteral("CH1")
        || value == QStringLiteral("CH2")
        || value == QStringLiteral("CH3")
        || value == QStringLiteral("CH4");
}

ParkingChannelDisplayNames channelDisplayNamesFromJson(const QJsonObject &root)
{
    ParkingChannelDisplayNames names;
    const QJsonObject object = root.value(QStringLiteral("channel_display_names")).toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString channel = it.key().trimmed().toUpper();
        const QString displayName = it.value().toString().trimmed();
        if (isCameraChannelId(channel)
            && !displayName.isEmpty()
            && displayName.compare(channel, Qt::CaseInsensitive) != 0) {
            names.insert(channel, displayName);
        }
    }
    return names;
}

QJsonObject channelDisplayNamesToJson(const ParkingChannelDisplayNames &names)
{
    QJsonObject object;
    for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
        const QString channel = QStringLiteral("CH%1").arg(channelNumber);
        const QString displayName = names.value(channel).trimmed();
        if (!displayName.isEmpty()
            && displayName.compare(channel, Qt::CaseInsensitive) != 0) {
            object.insert(channel, displayName);
        }
    }
    return object;
}
}

QList<ParkingZoneLayout> defaultParkingZoneLayout()
{
    QList<ParkingZoneLayout> zones;
    appendChannelTestLayout(&zones, 1, 1, 1, 30, 36);
    appendChannelTestLayout(&zones, 2, 5, 5, 470, 36);
    appendChannelTestLayout(&zones, 3, 9, 9, 30, 304);
    appendChannelTestLayout(&zones, 4, 13, 13, 470, 304);
    return zones;
}

bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones, QString *errorMessage)
{
    return loadParkingZoneLayout(path, zones, nullptr, errorMessage);
}

bool loadParkingZoneLayout(const QString &path, QList<ParkingZoneLayout> *zones,
                           ParkingChannelDisplayNames *channelDisplayNames,
                           QString *errorMessage)
{
    if (!zones) {
        if (errorMessage) *errorMessage = QStringLiteral("Output zone list is null.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not open parking map layout: ") + path;
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid parking map layout JSON: ") + path;
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonArray zoneArray = root.value(QStringLiteral("zones")).toArray();
    QList<ParkingZoneLayout> loadedZones;
    for (const QJsonValue &value : zoneArray) {
        if (!value.isObject()) continue;
        ParkingZoneLayout zone = zoneFromJson(value.toObject());
        if (!zone.zoneId.isEmpty() && zone.rect.width() > 0.0 && zone.rect.height() > 0.0) {
            loadedZones.append(zone);
        }
    }

    if (loadedZones.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Parking map layout has no valid zones: ") + path;
        return false;
    }

    *zones = loadedZones;
    if (channelDisplayNames) {
        *channelDisplayNames = channelDisplayNamesFromJson(root);
    }
    return true;
}

bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones, QString *errorMessage)
{
    return saveParkingZoneLayout(path, zones, ParkingChannelDisplayNames(), errorMessage);
}

bool saveParkingZoneLayout(const QString &path, const QList<ParkingZoneLayout> &zones,
                           const ParkingChannelDisplayNames &channelDisplayNames,
                           QString *errorMessage)
{
    const QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not create layout directory: ") + dir.absolutePath();
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("canvas_width"), 920);
    root.insert(QStringLiteral("canvas_height"), 560);
    root.insert(QStringLiteral("channel_display_names"),
                channelDisplayNamesToJson(channelDisplayNames));
    QJsonArray zoneArray;
    for (const ParkingZoneLayout &zone : zones) {
        zoneArray.append(zoneToJson(zone));
    }
    root.insert(QStringLiteral("zones"), zoneArray);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not write parking map layout: ") + path;
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
