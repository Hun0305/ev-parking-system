#include "parkingresponseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace {
QString firstString(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

QJsonObject firstObject(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return {};
}

ParkingImageResource imageFromObject(const QJsonObject &image,
                                     const QString &defaultRole,
                                     const QString &defaultProcessing)
{
    ParkingImageResource resource;
    resource.url = QUrl(image.value(QStringLiteral("url")).toString());
    resource.role = firstString(image, {QStringLiteral("image_role"), QStringLiteral("role")}).toUpper();
    resource.processing = firstString(
        image, {QStringLiteral("processing_type"), QStringLiteral("processing")}).toUpper();
    if (resource.role.isEmpty()) {
        resource.role = defaultRole;
    }
    if (resource.processing.isEmpty()) {
        resource.processing = defaultProcessing;
    }

    const QString timestamp = firstString(
        image,
        {QStringLiteral("captured_at"), QStringLiteral("processed_at"), QStringLiteral("timestamp")});
    resource.timestamp = QDateTime::fromString(timestamp, Qt::ISODate);
    const QJsonValue imageId = image.value(QStringLiteral("image_id"));
    if (!imageId.isNull() && !imageId.isUndefined()) {
        resource.imageId = imageId.toVariant().toLongLong();
    }
    const QJsonValue sessionId = image.value(QStringLiteral("session_id"));
    if (!sessionId.isNull() && !sessionId.isUndefined()) {
        resource.sessionId = sessionId.toVariant().toLongLong();
    }
    resource.enhancementType = firstString(
        image, {QStringLiteral("enhancement_type"), QStringLiteral("enhancementType")});
    resource.ocrResult = firstString(
        image, {QStringLiteral("ocr_result"), QStringLiteral("ocrResult")});
    resource.evidenceReason = firstString(
        image, {QStringLiteral("evidence_reason"), QStringLiteral("capture_reason"),
                QStringLiteral("reason")}).toUpper();
    return resource;
}

ParkingImageResource sessionImageVariant(const QJsonObject &item,
                                         const QString &urlKey,
                                         const QString &processing)
{
    ParkingImageResource resource = imageFromObject(
        item, QStringLiteral("EVIDENCE"), processing);
    // original_url/enhanced_url is the authoritative variant discriminator for
    // the session-images contract. A row-level compatibility field must not
    // relabel both URLs as the same processing variant.
    resource.processing = processing;
    resource.url = QUrl(item.value(urlKey).toString());
    return resource;
}

bool parseSlotObject(const QJsonObject &item,
                     ParkingSlotSnapshot &slot,
                     QString &errorMessage)
{
    ParkingSlotSnapshot parsed;
    parsed.slotId = firstString(item, {QStringLiteral("slot_id"), QStringLiteral("slotId")});
    parsed.state = firstString(
        item,
        {QStringLiteral("parking_status"), QStringLiteral("status"), QStringLiteral("state")});

    if (parsed.slotId.isEmpty() || parsed.state.isEmpty()) {
        errorMessage = QStringLiteral("Parking slot requires an ID and state");
        return false;
    }

    const QJsonObject session = firstObject(
        item, {QStringLiteral("active_session"), QStringLiteral("activeSession")});
    const QJsonObject &details = session.isEmpty() ? item : session;
    QJsonValue sessionId = details.value(QStringLiteral("session_id"));
    if (sessionId.isNull() || sessionId.isUndefined()) {
        sessionId = details.value(QStringLiteral("sessionId"));
    }
    if (!sessionId.isNull() && !sessionId.isUndefined()) {
        parsed.sessionId = sessionId.toVariant().toLongLong();
    }

    parsed.plateNumber = firstString(
        details, {QStringLiteral("plate_number"), QStringLiteral("plateNumber")});
    const QString vehicleType = firstString(
        details, {QStringLiteral("vehicle_type"), QStringLiteral("vehicleType"),
                  QStringLiteral("ev_status")}).toUpper();
    QJsonValue isEvValue = item.value(QStringLiteral("is_ev"));
    if (!isEvValue.isBool()) {
        isEvValue = details.value(QStringLiteral("is_ev"));
    }
    if (isEvValue.isBool()) {
        parsed.isEv = isEvValue.toBool();
        parsed.vehicleTypeKnown = true;
    } else if (vehicleType == QStringLiteral("EV")
               || vehicleType == QStringLiteral("ELECTRIC")
               || vehicleType == QStringLiteral("PHEV")
               || vehicleType == QStringLiteral("HYBRID")) {
        parsed.isEv = true;
        parsed.vehicleTypeKnown = true;
    } else if (vehicleType == QStringLiteral("GENERAL")
               || vehicleType == QStringLiteral("NON_EV")
               || vehicleType == QStringLiteral("ICE")
               || vehicleType == QStringLiteral("GASOLINE")
               || vehicleType == QStringLiteral("DIESEL")) {
        parsed.isEv = false;
        parsed.vehicleTypeKnown = true;
    }
    parsed.occupiedSince = QDateTime::fromString(
        firstString(details, {QStringLiteral("entry_time"), QStringLiteral("entryTime"),
                              QStringLiteral("occupied_since")}),
        Qt::ISODate);
    parsed.elapsedSeconds = details.value(QStringLiteral("elapsed_seconds")).toInt(
        details.value(QStringLiteral("elapsedSeconds")).toInt(0));
    parsed.alarm = firstString(details, {QStringLiteral("alert"), QStringLiteral("alarm")});

    const QJsonValue imageArrayValue = details.value(QStringLiteral("images"));
    if (imageArrayValue.isArray()) {
        for (const QJsonValue &value : imageArrayValue.toArray()) {
            if (!value.isObject()) {
                continue;
            }
            ParkingImageResource image = imageFromObject(value.toObject(), {}, {});
            if (image.sessionId <= 0 && parsed.sessionId > 0) {
                image.sessionId = parsed.sessionId;
            }
            if (!image.url.isEmpty()) {
                parsed.images.append(image);
            }
        }
    } else if (imageArrayValue.isObject()) {
        const QJsonObject images = imageArrayValue.toObject();
        if (images.value(QStringLiteral("before")).isObject()) {
            ParkingImageResource image = imageFromObject(
                images.value(QStringLiteral("before")).toObject(),
                QStringLiteral("VEHICLE"),
                QStringLiteral("ORIGINAL"));
            if (image.sessionId <= 0 && parsed.sessionId > 0) {
                image.sessionId = parsed.sessionId;
            }
            if (!image.url.isEmpty()) {
                parsed.images.append(image);
            }
        }
        if (images.value(QStringLiteral("after")).isObject()) {
            ParkingImageResource image = imageFromObject(
                images.value(QStringLiteral("after")).toObject(),
                QStringLiteral("VEHICLE"),
                QStringLiteral("ORIGINAL"));
            if (image.sessionId <= 0 && parsed.sessionId > 0) {
                image.sessionId = parsed.sessionId;
            }
            if (!image.url.isEmpty()) {
                parsed.images.append(image);
            }
        }
    }

    slot = parsed;
    errorMessage.clear();
    return true;
}
}

bool ParkingResponseParser::parseSnapshot(const QJsonDocument &document,
                                          ParkingSnapshot &snapshot,
                                          QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("Parking response must be a JSON object");
        return false;
    }

    const QJsonObject root = document.object();
    QJsonValue slotsValue = root.value(QStringLiteral("items"));
    if (!slotsValue.isArray()) {
        slotsValue = root.value(QStringLiteral("data"));
    }
    if (!slotsValue.isArray()) {
        slotsValue = root.value(QStringLiteral("slots"));
    }
    if (!slotsValue.isArray()) {
        errorMessage = QStringLiteral("Parking response is missing items, data, or slots array");
        return false;
    }

    ParkingSnapshot parsed;
    parsed.generatedAt = QDateTime::fromString(
        firstString(root, {QStringLiteral("generated_at"), QStringLiteral("generatedAt")}),
        Qt::ISODate);

    const QJsonArray items = slotsValue.toArray();
    for (int index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            errorMessage = QStringLiteral("Parking item %1 must be an object").arg(index);
            return false;
        }

        ParkingSlotSnapshot slot;
        QString slotError;
        if (!parseSlotObject(items.at(index).toObject(), slot, slotError)) {
            errorMessage = QStringLiteral("Parking item %1: %2").arg(index).arg(slotError);
            return false;
        }
        parsed.parkingSlots.append(slot);
    }

    snapshot = parsed;
    errorMessage.clear();
    return true;
}

bool ParkingResponseParser::parseSlotDetail(const QJsonDocument &document,
                                            ParkingSlotSnapshot &slot,
                                            QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("Parking slot detail must be a JSON object");
        return false;
    }
    return parseSlotObject(document.object(), slot, errorMessage);
}

bool ParkingResponseParser::parseSessionImages(
    const QJsonDocument &document,
    QList<ParkingImageResource> &images,
    QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("Parking session images must be a JSON object");
        return false;
    }

    const QJsonObject response = document.object();
    const QJsonValue itemsValue = response.value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        errorMessage = QStringLiteral("Parking session images response is missing items array");
        return false;
    }

    QJsonValue responseSessionValue = response.value(QStringLiteral("session_id"));
    if (responseSessionValue.isNull() || responseSessionValue.isUndefined()) {
        responseSessionValue = response.value(QStringLiteral("sessionId"));
    }
    const qint64 responseSessionId = responseSessionValue.toVariant().toLongLong();

    QList<ParkingImageResource> parsed;
    for (const QJsonValue &value : itemsValue.toArray()) {
        if (!value.isObject()) {
            errorMessage = QStringLiteral("Parking session image item must be an object");
            return false;
        }
        const QJsonObject item = value.toObject();
        ParkingImageResource original = sessionImageVariant(
            item, QStringLiteral("original_url"), QStringLiteral("ORIGINAL"));
        if (original.sessionId <= 0 && responseSessionId > 0) {
            original.sessionId = responseSessionId;
        }
        if (!original.url.isEmpty()) {
            parsed.append(original);
        }
        ParkingImageResource enhanced = sessionImageVariant(
            item, QStringLiteral("enhanced_url"), QStringLiteral("ENHANCED"));
        if (enhanced.sessionId <= 0 && responseSessionId > 0) {
            enhanced.sessionId = responseSessionId;
        }
        if (!enhanced.url.isEmpty()) {
            parsed.append(enhanced);
        }
    }

    images = parsed;
    errorMessage.clear();
    return true;
}
