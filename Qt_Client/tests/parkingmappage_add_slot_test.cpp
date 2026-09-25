#include "pages/parkingmappage.h"

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtGlobal>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingMapPage page(directory.filePath(QStringLiteral("parking_map_layout.json")));
    QGraphicsView *detailMapView = page.findChild<QGraphicsView *>(
        QStringLiteral("parkingMapDetailView"));
    QGraphicsView *overviewMapView = page.findChild<QGraphicsView *>(
        QStringLiteral("parkingOverviewMapView"));
    QWidget *adminToolbar = page.findChild<QWidget *>(QStringLiteral("parkingMapAdminToolbar"));
    QGroupBox *searchGroup = page.findChild<QGroupBox *>(QStringLiteral("parkingZoneSearchGroup"));
    QGroupBox *editorGroup = page.findChild<QGroupBox *>(QStringLiteral("parkingMapLayoutEditor"));
    QPushButton *cameraButton = page.findChild<QPushButton *>(QStringLiteral("parkingOpenCameraButton"));
    QPushButton *evidenceButton = page.findChild<QPushButton *>(QStringLiteral("parkingOpenEvidenceButton"));

    if (!detailMapView || !detailMapView->scene() || !overviewMapView
        || !overviewMapView->scene() || !adminToolbar || !searchGroup || !editorGroup
        || !cameraButton) return 3;
    if (detailMapView->dragMode() != QGraphicsView::NoDrag) return 4;
    if (overviewMapView->dragMode() != QGraphicsView::ScrollHandDrag) return 5;
    if (!qFuzzyCompare(overviewMapView->transform().m11(), 0.75)
        || !qFuzzyCompare(overviewMapView->property("overviewZoom").toReal(), 1.0)) return 10;
    if (!adminToolbar->isHidden() || !searchGroup->isHidden() || !editorGroup->isHidden()) return 6;
    if (evidenceButton) return 7;

    int slotCount = 0;
    for (QGraphicsItem *item : detailMapView->scene()->items()) {
        if (item->data(0).toString().isEmpty()) continue;
        ++slotCount;
        if (item->flags().testFlag(QGraphicsItem::ItemIsMovable)) return 8;
    }
    if (slotCount != 8) return 9;
    return 0;
}
