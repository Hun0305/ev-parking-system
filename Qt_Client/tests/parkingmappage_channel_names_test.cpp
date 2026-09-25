#include "models/parkingzonelayout.h"
#include "pages/parkingmappage.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPushButton>
#include <QTemporaryDir>

namespace {

bool sceneHasText(ParkingMapPage &page, const QString &expected)
{
    QGraphicsView *view = page.findChild<QGraphicsView *>();
    if (!view || !view->scene()) return false;
    for (QGraphicsItem *item : view->scene()->items()) {
        auto *textItem = dynamic_cast<QGraphicsSimpleTextItem *>(item);
        if (textItem && textItem->text() == expected) return true;
    }
    return false;
}

bool writeVersionOneLayout(const QString &path)
{
    QJsonObject zone;
    zone.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
    zone.insert(QStringLiteral("zone_type"), QStringLiteral("EV"));
    zone.insert(QStringLiteral("display_name"), QStringLiteral("EV-01"));
    zone.insert(QStringLiteral("x"), 64);
    zone.insert(QStringLiteral("y"), 96);
    zone.insert(QStringLiteral("width"), 84);
    zone.insert(QStringLiteral("height"), 58);
    zone.insert(QStringLiteral("rotation"), 0);
    zone.insert(QStringLiteral("camera_channel"), QStringLiteral("CH1"));
    zone.insert(QStringLiteral("iva_area_id"), QStringLiteral("IVA1"));
    zone.insert(QStringLiteral("hall_sensor_id"), QStringLiteral("HALL_EV_01"));
    zone.insert(QStringLiteral("enabled"), true);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("canvas_width"), 920);
    root.insert(QStringLiteral("canvas_height"), 560);
    root.insert(QStringLiteral("zones"), QJsonArray{zone});

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    return file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    const QString layoutPath = directory.filePath(QStringLiteral("parking_map_layout.json"));
    ParkingMapPage page(layoutPath);
    if (page.hasUnsavedLayoutChanges()) return 3;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("CH1")) return 4;
    if (!page.channelDisplayName(QStringLiteral("EAST")).isEmpty()) return 5;
    if (!sceneHasText(page, QStringLiteral("CH1 | IVA1-IVA4"))) return 6;

    QPushButton *namesButton = page.findChild<QPushButton *>(QStringLiteral("editChannelNamesButton"));
    if (!namesButton || namesButton->isEnabled()) return 7;
    if (!QMetaObject::invokeMethod(&page, "setEditMode", Qt::DirectConnection,
                                   Q_ARG(bool, true))) return 8;
    if (!namesButton->isEnabled()) return 9;

    if (page.setChannelDisplayName(QStringLiteral("EAST"), QStringLiteral("Invalid"))) return 10;
    if (page.hasUnsavedLayoutChanges()) return 11;
    if (!page.setChannelDisplayName(QStringLiteral(" ch1 "),
                                    QStringLiteral("  East EV Zone  "))) return 12;
    if (!page.hasUnsavedLayoutChanges()) return 13;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("East EV Zone")) return 14;
    if (!sceneHasText(page, QStringLiteral("CH1 | East EV Zone | IVA1-IVA4"))) return 15;

    QString saveError;
    if (!page.saveLayoutNow(&saveError) || !saveError.isEmpty()) return 16;
    if (page.hasUnsavedLayoutChanges()) return 17;

    QFile savedFile(layoutPath);
    if (!savedFile.open(QIODevice::ReadOnly | QIODevice::Text)) return 18;
    const QJsonDocument savedDocument = QJsonDocument::fromJson(savedFile.readAll());
    if (!savedDocument.isObject()) return 19;
    const QJsonObject savedRoot = savedDocument.object();
    if (savedRoot.value(QStringLiteral("version")).toInt() != 2) return 20;
    const QJsonObject savedNames = savedRoot.value(QStringLiteral("channel_display_names")).toObject();
    if (savedNames.value(QStringLiteral("CH1")).toString() != QStringLiteral("East EV Zone")) return 21;
    if (savedNames.contains(QStringLiteral("EAST"))) return 22;
    const QJsonArray savedZones = savedRoot.value(QStringLiteral("zones")).toArray();
    if (savedZones.isEmpty()) return 23;
    const QJsonObject firstZone = savedZones.at(0).toObject();
    if (firstZone.value(QStringLiteral("camera_channel")).toString() != QStringLiteral("CH1")) return 24;
    if (firstZone.value(QStringLiteral("iva_area_id")).toString() != QStringLiteral("IVA1")) return 25;

    ParkingMapPage reloadedPage(layoutPath);
    if (reloadedPage.hasUnsavedLayoutChanges()) return 26;
    if (reloadedPage.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("East EV Zone")) return 27;
    if (!sceneHasText(reloadedPage, QStringLiteral("CH1 | East EV Zone | IVA1-IVA4"))) return 28;
    if (!reloadedPage.setChannelDisplayName(QStringLiteral("CH1"), QStringLiteral("Unsaved Name"))) return 29;
    if (!QMetaObject::invokeMethod(&reloadedPage, "reloadLayout", Qt::DirectConnection)) return 30;
    if (reloadedPage.hasUnsavedLayoutChanges()) return 31;
    if (reloadedPage.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("East EV Zone")) return 32;
    if (!QMetaObject::invokeMethod(&reloadedPage, "resetDefaultLayout", Qt::DirectConnection)) return 33;
    if (!reloadedPage.hasUnsavedLayoutChanges()) return 34;
    if (reloadedPage.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("CH1")) return 35;
    if (!sceneHasText(reloadedPage, QStringLiteral("CH1 | IVA1-IVA4"))) return 36;

    const QString versionOnePath = directory.filePath(QStringLiteral("parking_map_layout_v1.json"));
    if (!writeVersionOneLayout(versionOnePath)) return 37;
    QList<ParkingZoneLayout> versionOneZones;
    ParkingChannelDisplayNames versionOneNames;
    versionOneNames.insert(QStringLiteral("CH4"), QStringLiteral("Stale value"));
    QString loadError;
    if (!loadParkingZoneLayout(versionOnePath, &versionOneZones, &versionOneNames, &loadError)) return 38;
    if (!loadError.isEmpty() || versionOneZones.size() != 1) return 39;
    if (!versionOneNames.isEmpty()) return 40;
    if (versionOneZones.constFirst().cameraChannel != QStringLiteral("CH1")) return 41;
    if (versionOneZones.constFirst().ivaAreaId != QStringLiteral("IVA1")) return 42;

    ParkingMapPage versionOnePage(versionOnePath);
    if (versionOnePage.hasUnsavedLayoutChanges()) return 43;
    if (versionOnePage.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("CH1")) return 44;
    if (!sceneHasText(versionOnePage, QStringLiteral("CH1 | IVA1-IVA4"))) return 45;

    return 0;
}
