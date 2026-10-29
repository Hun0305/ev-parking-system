#include "parkingroi.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>

namespace {
bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}
}

bool ParkingRoi::isValid(QString *errorMessage) const
{
    QString error;
    if (!std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(width) || !std::isfinite(height)) {
        error = QStringLiteral("ROI coordinates must be finite numbers.");
    } else if (x < 0.0 || y < 0.0 || width <= 0.0 || height <= 0.0
               || x >= 1.0 || y >= 1.0
               || x + width > 1.0 + 0.0000001
               || y + height > 1.0 + 0.0000001) {
        error = QStringLiteral("Invalid ROI coordinates.");
    }
    if (errorMessage) *errorMessage = error;
    return error.isEmpty();
}

bool ParkingRoi::nearlyEquals(const ParkingRoi &other, double epsilon) const
{
    return std::abs(x - other.x) <= epsilon
        && std::abs(y - other.y) <= epsilon
        && std::abs(width - other.width) <= epsilon
        && std::abs(height - other.height) <= epsilon;
}

ParkingPixelRoi ParkingRoi::toSourcePixels(
    const QSize &sourceFrameSize) const
{
    if (!sourceFrameSize.isValid()) return {};
    return {qRound(x * sourceFrameSize.width()),
            qRound(y * sourceFrameSize.height()),
            qRound(width * sourceFrameSize.width()),
            qRound(height * sourceFrameSize.height())};
}

bool ParkingRoi::isLargeEnough(const QSize &sourceFrameSize,
                               int minimumWidth,
                               int minimumHeight) const
{
    if (!sourceFrameSize.isValid() || minimumWidth <= 0 || minimumHeight <= 0) {
        return false;
    }
    const ParkingPixelRoi pixels = toSourcePixels(sourceFrameSize);
    return pixels.width >= minimumWidth && pixels.height >= minimumHeight;
}

ParkingRoi ParkingRoi::fromRectangle(const QRectF &rectangle)
{
    const QRectF normalized = rectangle.normalized();
    return {normalized.x(), normalized.y(),
            normalized.width(), normalized.height()};
}

bool ParkingRoiCodec::parseList(const QJsonDocument &document,
                                ParkingRoiMap &rois,
                                QString &errorMessage)
{
    rois.clear();
    if (!document.isObject()) {
        errorMessage = QStringLiteral("ROI list response must be a JSON object.");
        return false;
    }
    const QJsonValue itemsValue = document.object().value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        errorMessage = QStringLiteral("ROI list response is missing items.");
        return false;
    }
    const QJsonArray items = itemsValue.toArray();
    for (const QJsonValue &itemValue : items) {
        if (!itemValue.isObject()) {
            errorMessage = QStringLiteral("ROI list contains an invalid item.");
            return false;
        }
        const QJsonObject item = itemValue.toObject();
        const QString slotId = item.value(QStringLiteral("slotId"))
                                   .toString().trimmed().toUpper();
        if (slotId.isEmpty() || !item.value(QStringLiteral("roi")).isObject()) {
            errorMessage = QStringLiteral("ROI item is missing slotId or roi.");
            return false;
        }
        ParkingRoi roi;
        if (!parseRoiObject(item.value(QStringLiteral("roi")).toObject(),
                            roi, errorMessage)) {
            return false;
        }
        rois.insert(slotId, roi);
    }
    return true;
}

bool ParkingRoiCodec::parseSingle(const QJsonDocument &document,
                                  QString &slotId,
                                  ParkingRoi &roi,
                                  bool *appliedImmediately,
                                  QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("ROI response must be a JSON object.");
        return false;
    }
    const QJsonObject object = document.object();
    slotId = object.value(QStringLiteral("slotId")).toString().trimmed().toUpper();
    if (slotId.isEmpty() || !object.value(QStringLiteral("roi")).isObject()) {
        errorMessage = QStringLiteral("ROI response is missing slotId or roi.");
        return false;
    }
    if (object.contains(QStringLiteral("success"))
        && !object.value(QStringLiteral("success")).toBool()) {
        errorMessage = object.value(QStringLiteral("error")).toString(
            QStringLiteral("The server rejected the ROI settings."));
        return false;
    }
    if (!parseRoiObject(object.value(QStringLiteral("roi")).toObject(),
                        roi, errorMessage)) {
        return false;
    }
    if (appliedImmediately) {
        *appliedImmediately = object.value(
            QStringLiteral("appliedImmediately")).toBool(false);
    }
    return true;
}

QJsonObject ParkingRoiCodec::toJson(const ParkingRoi &roi)
{
    return {{QStringLiteral("x"), roi.x},
            {QStringLiteral("y"), roi.y},
            {QStringLiteral("width"), roi.width},
            {QStringLiteral("height"), roi.height}};
}

bool ParkingRoiCodec::parseRoiObject(const QJsonObject &object,
                                     ParkingRoi &roi,
                                     QString &errorMessage)
{
    const QJsonValue x = object.value(QStringLiteral("x"));
    const QJsonValue y = object.value(QStringLiteral("y"));
    const QJsonValue width = object.value(QStringLiteral("width"));
    const QJsonValue height = object.value(QStringLiteral("height"));
    if (!finiteNumber(x) || !finiteNumber(y)
        || !finiteNumber(width) || !finiteNumber(height)) {
        errorMessage = QStringLiteral("ROI response contains non-numeric coordinates.");
        return false;
    }
    roi = {x.toDouble(), y.toDouble(), width.toDouble(), height.toDouble()};
    return roi.isValid(&errorMessage);
}
