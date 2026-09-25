#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRectF>
#include <QSize>
#include <QString>

struct ParkingPixelRoi
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ParkingRoi
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    QRectF rectangle() const { return QRectF(x, y, width, height); }
    bool isValid(QString *errorMessage = nullptr) const;
    bool nearlyEquals(const ParkingRoi &other, double epsilon = 0.000001) const;
    ParkingPixelRoi toSourcePixels(const QSize &sourceFrameSize) const;
    bool isLargeEnough(const QSize &sourceFrameSize,
                       int minimumWidth = 8,
                       int minimumHeight = 8) const;

    static ParkingRoi fromRectangle(const QRectF &rectangle);
};

using ParkingRoiMap = QHash<QString, ParkingRoi>;

class ParkingRoiCodec
{
public:
    static bool parseList(const QJsonDocument &document,
                          ParkingRoiMap &rois,
                          QString &errorMessage);
    static bool parseSingle(const QJsonDocument &document,
                            QString &slotId,
                            ParkingRoi &roi,
                            bool *appliedImmediately,
                            QString &errorMessage);
    static QJsonObject toJson(const ParkingRoi &roi);

private:
    static bool parseRoiObject(const QJsonObject &object,
                               ParkingRoi &roi,
                               QString &errorMessage);
};

Q_DECLARE_METATYPE(ParkingRoi)
Q_DECLARE_METATYPE(ParkingRoiMap)
