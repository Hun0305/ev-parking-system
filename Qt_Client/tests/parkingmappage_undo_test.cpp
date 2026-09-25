#include "pages/parkingmappage.h"

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QKeySequence>
#include <QMetaObject>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>

namespace {

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

QSlider *rotationSlider(ParkingMapPage &page)
{
    const QList<QSlider *> sliders = page.findChildren<QSlider *>();
    for (QSlider *slider : sliders) {
        if (slider->minimum() == -180 && slider->maximum() == 180) {
            return slider;
        }
    }
    return nullptr;
}

bool invoke(ParkingMapPage &page, const char *method)
{
    return QMetaObject::invokeMethod(&page, method, Qt::DirectConnection);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingMapPage page(directory.filePath(QStringLiteral("parking_map_layout.json")));
    QTableWidget *table = zoneTable(page);
    QPushButton *undoButton = page.findChild<QPushButton *>(QStringLiteral("undoLayoutButton"));
    if (!table || !undoButton || undoButton->isEnabled()) return 3;
    if (undoButton->shortcut() != QKeySequence(QKeySequence::Undo)) return 4;
    if (page.hasUnsavedLayoutChanges() || table->rowCount() != 32) return 5;

    if (!invoke(page, "addGeneralZone")) return 6;
    if (!page.hasUnsavedLayoutChanges() || !undoButton->isEnabled()
        || table->rowCount() != 33) return 7;
    if (!invoke(page, "undoLastLayoutChange")) return 8;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()
        || table->rowCount() != 32) return 9;

    if (!page.setChannelDisplayName(QStringLiteral("CH1"), QStringLiteral("Entrance"))) return 10;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("Entrance")) return 11;
    if (!invoke(page, "resetDefaultLayout")) return 12;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("CH1")) return 13;
    if (!invoke(page, "undoLastLayoutChange")) return 14;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("Entrance")) return 15;
    if (!invoke(page, "undoLastLayoutChange")) return 16;
    if (page.channelDisplayName(QStringLiteral("CH1")) != QStringLiteral("CH1")
        || page.hasUnsavedLayoutChanges()) return 17;

    if (!invoke(page, "addEvZone")) return 18;
    if (!invoke(page, "deleteSelectedZone") || table->rowCount() != 32) return 19;
    if (!invoke(page, "undoLastLayoutChange") || table->rowCount() != 33) return 20;
    if (!invoke(page, "undoLastLayoutChange") || table->rowCount() != 32) return 21;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 22;

    if (!QMetaObject::invokeMethod(&page, "setEditMode", Qt::DirectConnection,
                                   Q_ARG(bool, true))) return 23;
    if (!QMetaObject::invokeMethod(&page, "handleZoneTableClicked", Qt::DirectConnection,
                                   Q_ARG(int, 0), Q_ARG(int, 0))) return 24;
    QSlider *rotation = rotationSlider(page);
    if (!rotation || rotation->value() != 0) return 25;
    rotation->setValue(35);
    if (!page.hasUnsavedLayoutChanges() || !undoButton->isEnabled()) return 26;
    if (!invoke(page, "resetSelectedZoneShape") || rotation->value() != 0) return 27;
    if (!invoke(page, "undoLastLayoutChange")) return 28;
    if (rotation->value() != 35) return 46;
    if (!invoke(page, "undoLastLayoutChange")) return 29;
    if (rotation->value() != 0 || page.hasUnsavedLayoutChanges()) return 30;

    rotation->setSliderDown(true);
    rotation->setValue(10);
    rotation->setValue(20);
    rotation->setValue(30);
    rotation->setSliderDown(false);
    if (!invoke(page, "undoLastLayoutChange")) return 31;
    if (rotation->value() != 0 || page.hasUnsavedLayoutChanges()) return 32;

    QGraphicsView *mapView = page.findChild<QGraphicsView *>();
    if (!mapView || !mapView->scene() || mapView->scene()->selectedItems().size() != 1) return 33;
    QGraphicsItem *selectedItem = mapView->scene()->selectedItems().constFirst();
    const QString selectedZoneId = selectedItem->data(0).toString();
    selectedItem->setRotation(73.0);
    if (!QMetaObject::invokeMethod(&page, "handleZoneItemMoved", Qt::DirectConnection,
                                   Q_ARG(QString, selectedZoneId))) return 34;
    if (!invoke(page, "undoLastLayoutChange") || rotation->value() != 0
        || page.hasUnsavedLayoutChanges()) return 35;

    for (int value = 1; value <= 35; ++value) {
        rotation->setValue(value);
    }
    for (int count = 0; count < 30; ++count) {
        if (!invoke(page, "undoLastLayoutChange")) return 36;
    }
    if (rotation->value() != 5 || !page.hasUnsavedLayoutChanges()
        || undoButton->isEnabled()) return 37;
    if (!invoke(page, "undoLastLayoutChange") || rotation->value() != 5) return 38;

    if (!invoke(page, "reloadLayout")) return 39;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 40;
    if (!QMetaObject::invokeMethod(&page, "handleZoneTableClicked", Qt::DirectConnection,
                                   Q_ARG(int, 0), Q_ARG(int, 0))) return 41;
    if (rotation->value() != 0) return 42;

    rotation->setValue(25);
    QString saveError;
    if (!page.saveLayoutNow(&saveError) || !saveError.isEmpty()) return 43;
    if (page.hasUnsavedLayoutChanges() || undoButton->isEnabled()) return 44;
    if (!invoke(page, "undoLastLayoutChange") || rotation->value() != 25) return 45;

    return 0;
}
