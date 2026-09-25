#include "api/parkingresponseparser.h"

#include <QCoreApplication>
#include <QJsonDocument>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QJsonDocument detail = QJsonDocument::fromJson(R"JSON(
        {
          "slot_id": "EV01",
          "parking_status": "OCCUPIED",
          "active_session": {
            "session_id": 7,
            "plate_number": "12A3456",
            "ev_status": "PHEV",
            "entry_time": "2026-07-27T09:00:00+09:00",
            "images": [
              {
                "image_id": 1,
                "url": "/api/v1/images/1/original",
                "captured_at": "2026-07-27T09:00:00+09:00"
              }
            ]
          }
        }
    )JSON");
    ParkingSlotSnapshot slot;
    QString error;
    if (!ParkingResponseParser::parseSlotDetail(detail, slot, error)) return 1;
    if (slot.slotId != QStringLiteral("EV01")) return 2;
    if (slot.sessionId != 7) return 3;
    if (slot.plateNumber != QStringLiteral("12A3456")) return 4;
    if (!slot.vehicleTypeKnown || !slot.isEv) return 5;
    if (slot.images.size() != 1 || slot.images.constFirst().sessionId != 7) return 18;

    const QJsonDocument sessionImages = QJsonDocument::fromJson(R"JSON(
        {
          "session_id": 7,
          "items": [
            {
              "image_id": 9,
              "session_id": 7,
              "original_url": "/api/v1/images/9/original",
              "enhanced_url": "/api/v1/images/9/enhanced",
              "processing": "ORIGINAL",
              "enhancement_type": "CLAHE",
              "ocr_result": "12A3456",
              "captured_at": "2026-07-27T09:00:01+09:00"
            },
            {
              "image_id": 10,
              "original_url": "/api/v1/images/10/original",
              "enhanced_url": null,
              "evidence_reason": "OVERSTAY_EVIDENCE",
              "captured_at": "2026-07-27T10:00:02+09:00"
            }
          ]
        }
    )JSON");
    QList<ParkingImageResource> images;
    if (!ParkingResponseParser::parseSessionImages(sessionImages, images, error)) return 6;
    if (images.size() != 3) return 7;
    if (images.at(0).imageId != 9 || images.at(1).imageId != 9) return 8;
    if (images.at(0).processing != QStringLiteral("ORIGINAL")) return 9;
    if (images.at(1).processing != QStringLiteral("ENHANCED")) return 10;
    if (images.at(0).ocrResult != QStringLiteral("12A3456")) return 11;
    if (images.at(2).evidenceReason != QStringLiteral("OVERSTAY_EVIDENCE")) return 12;
    if (images.at(2).url != QUrl(QStringLiteral("/api/v1/images/10/original"))) return 13;
    if (images.at(2).sessionId != 7) return 19;

    const QJsonDocument legacyTimeline = QJsonDocument::fromJson(R"JSON(
        {
          "slot_id": "EV02",
          "parking_status": "OCCUPIED",
          "images": {
            "before": {
              "url": "/api/v1/legacy/before",
              "captured_at": "2026-07-27T09:00:00+09:00"
            },
            "after": {
              "url": "/api/v1/legacy/after",
              "captured_at": "2026-07-27T09:01:00+09:00"
            }
          }
        }
    )JSON");
    ParkingSlotSnapshot legacySlot;
    if (!ParkingResponseParser::parseSlotDetail(
            legacyTimeline, legacySlot, error)) return 14;
    if (legacySlot.images.size() != 2) return 15;
    if (legacySlot.images.at(0).processing != QStringLiteral("ORIGINAL")
        || legacySlot.images.at(1).processing != QStringLiteral("ORIGINAL")) {
        return 16;
    }
    if (legacySlot.images.at(0).url
            != QUrl(QStringLiteral("/api/v1/legacy/before"))
        || legacySlot.images.at(1).url
            != QUrl(QStringLiteral("/api/v1/legacy/after"))) {
        return 17;
    }

    return 0;
}
