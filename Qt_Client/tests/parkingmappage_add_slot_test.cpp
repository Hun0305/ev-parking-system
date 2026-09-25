#include "pages/parkingmappage.h"

#include <QApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QMetaObject>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>

namespace {

QString slotId(const QString &prefix, int number)
{
    return QStringLiteral("%1-%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
}

QTableWidget *zoneTable(ParkingMapPage &page)
{
    const QList<QTableWidget *> tables = page.findChildren<QTableWidget *>();
    for (QTableWidget *table : tables) {
        if (table->columnCount() == 7
            && table->horizontalHeaderItem(0)
            && table->horizontalHeaderItem(0)->text() == QStringLiteral("Zone")) {
            return table;
        }
    }
    return nullptr;
}

QSlider *sliderWithRange(ParkingMapPage &page, int minimum, int maximum)
{
    const QList<QSlider *> sliders = page.findChildren<QSlider *>();
    for (QSlider *slider : sliders) {
        if (slider->minimum() == minimum && slider->maximum() == maximum) {
            return slider;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingMapPage page(directory.filePath(QStringLiteral("parking_map_layout.json")));
    if (page.hasUnsavedLayoutChanges()) return 11;

    ParkingViewState state;
    for (int number = 1; number <= 16; ++number) {
        EvSlotInfo evSlot;
        evSlot.slotId = slotId(QStringLiteral("EV"), number);
        evSlot.state = (number % 2 == 0) ? SlotState::Occupied : SlotState::Vacant;
        state.evSlots.insert(evSlot.slotId, evSlot);

        ParkingSlotInfo generalSlot;
        generalSlot.slotId = slotId(QStringLiteral("P"), number);
        generalSlot.state = (number % 2 == 0) ? SlotState::Occupied : SlotState::Vacant;
        state.parkingSlots.insert(generalSlot.slotId, generalSlot);
    }
    page.render(state);

    constexpr int additions = 12;
    for (int index = 0; index < additions / 2; ++index) {
        if (!QMetaObject::invokeMethod(&page, "addGeneralZone", Qt::DirectConnection)) return 3;
        if (!QMetaObject::invokeMethod(&page, "addEvZone", Qt::DirectConnection)) return 4;
    }
    if (!page.hasUnsavedLayoutChanges()) return 12;

    QTableWidget *table = zoneTable(page);
    if (!table || table->rowCount() != 32 + additions) return 5;
    for (int row = 32; row < table->rowCount(); ++row) {
        const QTableWidgetItem *stateItem = table->item(row, 5);
        if (!stateItem || stateItem->text() != QStringLiteral("WAITING DATA")) return 6;
    }

    QGraphicsView *mapView = page.findChild<QGraphicsView *>();
    if (!mapView || !mapView->scene() || mapView->scene()->sceneRect().height() <= 560.0) return 7;

    for (int index = 0; index < 20; ++index) {
        if (!QMetaObject::invokeMethod(&page, "deleteSelectedZone", Qt::DirectConnection)) return 8;
        if (!QMetaObject::invokeMethod(&page, "addEvZone", Qt::DirectConnection)) return 9;
    }
    if (table->rowCount() != 32 + additions) return 10;

    QString saveError;
    if (!page.saveLayoutNow(&saveError)) return 13;
    if (page.hasUnsavedLayoutChanges()) return 14;

    if (!QMetaObject::invokeMethod(&page, "setEditMode", Qt::DirectConnection,
                                   Q_ARG(bool, true))) return 25;
    QSlider *widthSlider = sliderWithRange(page, 16, 420);
    QSlider *heightSlider = sliderWithRange(page, 16, 220);
    QSlider *rotationSlider = sliderWithRange(page, -180, 180);
    if (!widthSlider || !heightSlider || !rotationSlider) return 26;
    widthSlider->setValue(132);
    heightSlider->setValue(96);
    rotationSlider->setValue(35);
    if (!page.hasUnsavedLayoutChanges()) return 27;
    if (!page.saveLayoutNow(&saveError)) return 28;
    if (page.hasUnsavedLayoutChanges()) return 29;
    if (!QMetaObject::invokeMethod(&page, "resetSelectedZoneShape",
                                   Qt::DirectConnection)) return 30;
    if (widthSlider->value() != 84
        || heightSlider->value() != 58
        || rotationSlider->value() != 0) return 31;
    if (!page.hasUnsavedLayoutChanges()) return 32;
    if (!page.saveLayoutNow(&saveError)) return 33;
    if (page.hasUnsavedLayoutChanges()) return 34;

    const QList<QGraphicsItem *> selectedItems = mapView->scene()->selectedItems();
    if (selectedItems.size() != 1) return 35;
    QGraphicsItem *selectedItem = selectedItems.constFirst();
    const QString selectedZoneId = selectedItem->data(0).toString();
    if (selectedZoneId.isEmpty()) return 36;

    selectedItem->setRotation(73.0);
    if (!QMetaObject::invokeMethod(&page, "handleZoneItemMoved",
                                   Qt::DirectConnection,
                                   Q_ARG(QString, selectedZoneId))) return 37;
    if (rotationSlider->value() != 73) return 38;
    if (!page.hasUnsavedLayoutChanges()) return 39;
    if (!QMetaObject::invokeMethod(&page, "resetSelectedZoneShape",
                                   Qt::DirectConnection)) return 40;
    if (selectedItem->rotation() != 0.0 || rotationSlider->value() != 0) return 41;
    if (!page.saveLayoutNow(&saveError)) return 42;

    selectedItem->setRotation(47.0);
    if (!QMetaObject::invokeMethod(&page, "resetSelectedZoneShape",
                                   Qt::DirectConnection)) return 43;
    if (selectedItem->rotation() != 0.0 || rotationSlider->value() != 0) return 44;
    if (!page.hasUnsavedLayoutChanges()) return 45;
    if (!page.saveLayoutNow(&saveError)) return 46;

    if (!QMetaObject::invokeMethod(&page, "addEvZone", Qt::DirectConnection)) return 15;
    if (!page.hasUnsavedLayoutChanges()) return 16;
    if (!QMetaObject::invokeMethod(&page, "reloadLayout", Qt::DirectConnection)) return 17;
    if (page.hasUnsavedLayoutChanges()) return 18;
    if (table->rowCount() != 32 + additions) return 19;

    const QString blockedPath = directory.filePath(QStringLiteral("layout_as_directory"));
    if (!QDir().mkpath(blockedPath)) return 20;
    ParkingMapPage failurePage(blockedPath);
    if (!QMetaObject::invokeMethod(&failurePage, "addGeneralZone", Qt::DirectConnection)) return 21;
    QString failureError;
    if (failurePage.saveLayoutNow(&failureError)) return 22;
    if (failureError.isEmpty()) return 23;
    if (!failurePage.hasUnsavedLayoutChanges()) return 24;

    return 0;
}
