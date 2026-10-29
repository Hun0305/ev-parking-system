#include "parkingcapturegroup.h"

#include <QHash>

#include <algorithm>

QVector<ParkingCaptureGroup> buildParkingCaptureGroups(
    const QList<ParkingImageResource> &images)
{
    QVector<ParkingCaptureGroup> groups;
    QHash<QString, int> groupIndexes;
    for (int index = 0; index < images.size(); ++index) {
        const ParkingImageResource &image = images.at(index);
        QString key;
        if (image.imageId >= 0) {
            key = QStringLiteral("session:%1:id:%2")
                      .arg(image.sessionId)
                      .arg(image.imageId);
        } else {
            // Role and timestamp describe an image but do not prove that two
            // resources are processing variants of the same capture. Keep
            // identity-less legacy resources separate instead of guessing.
            key = QStringLiteral("item:%1").arg(index);
        }

        int groupIndex = groupIndexes.value(key, -1);
        if (groupIndex < 0) {
            ParkingCaptureGroup capture;
            capture.imageId = image.imageId;
            capture.sessionId = image.sessionId;
            capture.timestamp = image.timestamp;
            capture.reason = image.evidenceReason.isEmpty()
                ? image.role.toUpper() : image.evidenceReason;
            capture.ocrResult = image.ocrResult;
            groupIndex = groups.size();
            groups.append(capture);
            groupIndexes.insert(key, groupIndex);
        }

        ParkingCaptureGroup &capture = groups[groupIndex];
        capture.variants.append(image);
        if (capture.sessionId <= 0 && image.sessionId > 0) {
            capture.sessionId = image.sessionId;
        }
        if (!capture.timestamp.isValid() && image.timestamp.isValid()) {
            capture.timestamp = image.timestamp;
        }
        if (capture.ocrResult.isEmpty()) {
            capture.ocrResult = image.ocrResult;
        }
        if ((capture.reason.isEmpty()
             || capture.reason == QStringLiteral("EVIDENCE"))
            && !image.evidenceReason.isEmpty()) {
            capture.reason = image.evidenceReason;
        }
    }

    std::stable_sort(groups.begin(), groups.end(),
                     [](const ParkingCaptureGroup &left,
                        const ParkingCaptureGroup &right) {
        if (left.timestamp.isValid() != right.timestamp.isValid()) {
            return left.timestamp.isValid();
        }
        if (left.timestamp.isValid() && left.timestamp != right.timestamp) {
            return left.timestamp < right.timestamp;
        }
        if (left.imageId >= 0 && right.imageId >= 0) {
            return left.imageId < right.imageId;
        }
        return false;
    });
    return groups;
}

const ParkingImageResource *parkingCaptureVariant(
    const ParkingCaptureGroup &capture,
    const QString &processing)
{
    for (const ParkingImageResource &variant : capture.variants) {
        if (variant.processing.compare(processing, Qt::CaseInsensitive) == 0) {
            return &variant;
        }
    }
    return nullptr;
}

const ParkingImageResource *preferredParkingCaptureVariant(
    const ParkingCaptureGroup &capture)
{
    if (const ParkingImageResource *original = parkingCaptureVariant(
            capture, QStringLiteral("ORIGINAL"))) {
        return original;
    }
    return capture.variants.isEmpty() ? nullptr : &capture.variants.first();
}
