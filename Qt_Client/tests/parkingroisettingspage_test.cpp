#include "pages/parkingroisettingspage.h"
#include "iva/ivavideocanvas.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

void drag(IvaVideoCanvas *canvas, const QPointF &sourceStart,
          const QPointF &sourceEnd)
{
    const QPoint start = canvas->mapFromScene(sourceStart);
    const QPoint end = canvas->mapFromScene(sourceEnd);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                      QPointF(canvas->viewport()->mapToGlobal(start)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(end),
                     QPointF(canvas->viewport()->mapToGlobal(end)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end),
                        QPointF(canvas->viewport()->mapToGlobal(end)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &release);
    QApplication::processEvents();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ParkingRoiSettingsPage page;
    page.resize(1400, 850);

    quint64 listGeneration = 0;
    quint64 slotGeneration = 0;
    quint64 saveGeneration = 0;
    QString requestedSlot;
    ParkingRoi requestedRoi;
    QObject::connect(&page, &ParkingRoiSettingsPage::roiListRequested,
                     [&](quint64 generation) { listGeneration = generation; });
    QObject::connect(&page, &ParkingRoiSettingsPage::roiRequested,
                     [&](const QString &slotId, quint64 generation) {
        requestedSlot = slotId;
        slotGeneration = generation;
    });
    QObject::connect(&page, &ParkingRoiSettingsPage::roiSaveRequested,
                     [&](const QString &slotId, const ParkingRoi &roi,
                         quint64 generation) {
        requestedSlot = slotId;
        requestedRoi = roi;
        saveGeneration = generation;
    });

    page.show();
    QApplication::processEvents();
    if (!require(listGeneration == 1,
                 "opening the page must request the server ROI list")) return 1;
    page.setPreviewFrame(QImage(1920, 1080, QImage::Format_RGB32));
    ParkingRoiMap rois;
    rois.insert(QStringLiteral("EV-01"), ParkingRoi{0.1, 0.1, 0.2, 0.3});
    rois.insert(QStringLiteral("EV-02"), ParkingRoi{0.5, 0.2, 0.1, 0.2});
    rois.insert(QStringLiteral("EV-03"), ParkingRoi{0.6, 0.3, 0.1, 0.2});
    rois.insert(QStringLiteral("EV-04"), ParkingRoi{0.0, 0.0, 1.0, 1.0});
    page.setRoiList(rois, listGeneration);

    auto *canvas = page.findChild<IvaVideoCanvas *>(
        QStringLiteral("parkingRoiVideoCanvas"));
    auto *combo = page.findChild<QComboBox *>(QStringLiteral("parkingRoiSlotCombo"));
    auto *freeze = page.findChild<QPushButton *>(
        QStringLiteral("freezeParkingRoiFrameButton"));
    auto *refresh = page.findChild<QPushButton *>(
        QStringLiteral("refreshParkingRoiFrameButton"));
    auto *save = page.findChild<QPushButton *>(QStringLiteral("saveParkingRoiButton"));
    auto *current = page.findChild<QLabel *>(QStringLiteral("currentParkingRoiLabel"));
    auto *selected = page.findChild<QLabel *>(QStringLiteral("selectedParkingRoiLabel"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("parkingRoiServerStatusLabel"));
    if (!require(canvas && combo && freeze && refresh && save
                     && current && selected && status,
                 "parking ROI controls must exist")) return 2;
    if (!require(current->text().contains(QStringLiteral("x=0.100000")),
                 "EV01 saved ROI must be displayed")) return 3;

    const QRectF normalized = IvaVideoCanvas::normalizedFromSource(
        QRectF(192, 108, 768, 540), QSize(1920, 1080));
    if (!require(qAbs(normalized.x() - 0.1) < 0.000001
                     && qAbs(normalized.y() - 0.1) < 0.000001
                     && qAbs(normalized.width() - 0.4) < 0.000001
                     && qAbs(normalized.height() - 0.5) < 0.000001,
                 "source coordinates must normalize independently of widget size")) return 4;

    freeze->click();
    drag(canvas, QPointF(192, 108), QPointF(199, 116));
    save->click();
    if (!require(saveGeneration == 0 && !save->isEnabled()
                     && status->text().contains(
                         QStringLiteral("at least 8 × 8 pixels")),
                 "a 7x8 source-pixel ROI must be rejected before PUT")) return 5;
    drag(canvas, QPointF(192, 108), QPointF(960, 648));
    if (!require(save->isEnabled()
                     && selected->text() != QStringLiteral("No selection")
                     && selected->text().contains(QStringLiteral("width=")),
                 "letterboxed canvas drag must produce normalized ROI")) return 6;
    const QString draftedText = selected->text();
    page.resize(1100, 700);
    QApplication::processEvents();
    if (!require(selected->text() != QStringLiteral("No selection"),
                 "resizing must preserve normalized ROI")) return 7;

    save->click();
    if (!require(saveGeneration > 0 && requestedSlot == QStringLiteral("EV-01")
                     && requestedRoi.nearlyEquals(
                         ParkingRoi{0.1, 0.1, 0.4, 0.5}, 0.002),
                 "Save and Apply must emit normalized coordinates once")) return 8;
    page.setRequestError(QStringLiteral("EV-01"), QStringLiteral("HTTP 400"),
                         saveGeneration - 1, true);
    if (!require(!save->isEnabled(),
                 "stale failures must not finish the active save")) return 9;
    page.setRoi(QStringLiteral("EV-01"), requestedRoi, saveGeneration,
                true, true);
    if (!require(status->text().contains(QStringLiteral("applied immediately"))
                     && current->text() == draftedText,
                 "verified save response must replace the saved ROI")) return 10;

    combo->setCurrentText(QStringLiteral("EV-02"));
    QApplication::processEvents();
    if (!require(requestedSlot == QStringLiteral("EV-02") && slotGeneration > saveGeneration
                     && current->text().contains(QStringLiteral("x=0.500000")),
                 "slot switching must show and reload the matching ROI")) return 11;
    page.setRoi(QStringLiteral("EV-01"), ParkingRoi{0.0, 0.0, 1.0, 1.0},
                slotGeneration, false, false);
    if (!require(current->text().contains(QStringLiteral("x=0.500000")),
                 "response for a previous slot must be ignored")) return 12;
    page.setRoi(QStringLiteral("EV-02"), rois.value(QStringLiteral("EV-02")),
                slotGeneration, false, false);

    refresh->click();
    page.setPreviewFrame(QImage(1280, 720, QImage::Format_RGB32));
    drag(canvas, QPointF(128, 72), QPointF(640, 360));
    save->click();
    const quint64 failedSaveGeneration = saveGeneration;
    page.setRequestError(QStringLiteral("EV-02"), QStringLiteral("HTTP 400"),
                         failedSaveGeneration, true);
    if (!require(current->text().contains(QStringLiteral("x=0.500000"))
                     && selected->text() == QStringLiteral("No selection")
                     && status->text().contains(QStringLiteral("rejected")),
                 "failed PUT must restore the last saved ROI")) return 13;

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("parkingRoiHelpButton"));
    if (helpButton) helpButton->click();
    QApplication::processEvents();
    QDialog *helpDialog = page.findChild<QDialog *>(
        QStringLiteral("parkingRoiHelpDialog"));
    QLabel *helpSteps = helpDialog
        ? helpDialog->findChild<QLabel *>(QStringLiteral("parkingRoiHelpSteps"))
        : nullptr;
    if (!require(helpButton && helpDialog && helpSteps
                     && helpSteps->text().contains(QStringLiteral("Save and Apply")),
                 "parking ROI help must explain the save flow")) return 14;
    helpDialog->close();

    std::cout << "PASS: parking ROI page handles letterbox, resize, stale responses, and rollback\n";
    return 0;
}
