#include "iva/ivavideocanvas.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    IvaVideoCanvas canvas;
    canvas.resize(800, 500);
    canvas.setChannel(2, QSize(2592, 1520));

    const QList<QPointF> points = IvaVideoCanvas::rectangleCoordinates(
        QRectF(QPointF(782.4, 1344.4), QPointF(89.6, 532.6)));
    if (!require(points.size() == 4
                     && points.at(0) == QPointF(90, 533)
                     && points.at(1) == QPointF(782, 533)
                     && points.at(2) == QPointF(782, 1344)
                     && points.at(3) == QPointF(90, 1344),
                 "drag rectangles must become clockwise WiseAI coordinates")) return 1;

    bool compatible = false;
    QString message;
    QObject::connect(&canvas, &IvaVideoCanvas::frameCompatibilityChanged,
                     [&](bool value, const QString &text) {
        compatible = value;
        message = text;
    });
    canvas.setFrame(QImage(1296, 760, QImage::Format_RGB32));
    if (!require(compatible && canvas.frameCompatible(),
                 "same-aspect shared frames must enable drawing")) return 1;
    canvas.setFrame(QImage(1920, 1080, QImage::Format_RGB32));
    if (!require(!compatible && !canvas.frameCompatible()
                     && message.contains(QStringLiteral("does not match")),
                 "mismatched RTSP and IVA coordinate ratios must block editing")) return 1;

    IvaAreaDefinition area;
    area.channel = 2;
    area.areaIndex = 1;
    area.name = QStringLiteral("name1");
    area.areaCoordinates = points;
    canvas.setAreas({area});
    canvas.setSelectedAreaIndex(1);
    canvas.setFrame(QImage(1296, 760, QImage::Format_RGB32));
    canvas.setEditMode(true);
    canvas.show();
    app.processEvents();

    QRectF editedRectangle;
    QObject::connect(&canvas, &IvaVideoCanvas::rectangleEdited,
                     [&](const QRectF &rectangle) { editedRectangle = rectangle; });
    const QPoint moveStart = canvas.mapFromScene(QPointF(400, 900));
    const QPoint moveEnd = canvas.mapFromScene(QPointF(500, 950));
    QMouseEvent moveHover(QEvent::MouseMove, QPointF(moveStart), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &moveHover);
    if (!require(canvas.cursor().shape() == Qt::OpenHandCursor,
                 "hovering inside the selected IVA box must show an open hand")) return 1;
    QMouseEvent movePress(QEvent::MouseButtonPress, QPointF(moveStart),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &movePress);
    if (!require(canvas.cursor().shape() == Qt::ClosedHandCursor,
                 "pressing inside the selected IVA box must show a closed hand")) return 1;
    QMouseEvent moveEvent(QEvent::MouseMove, QPointF(moveEnd), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &moveEvent);
    QMouseEvent moveRelease(QEvent::MouseButtonRelease, QPointF(moveEnd),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &moveRelease);
    if (!require(qAbs(editedRectangle.x() - 190.0) < 3.0
                     && qAbs(editedRectangle.y() - 583.0) < 3.0,
                 "dragging the selected IVA box must move the whole rectangle")) return 1;

    area.areaCoordinates = IvaVideoCanvas::rectangleCoordinates(editedRectangle);
    canvas.setAreas({area});
    canvas.setSelectedAreaIndex(1);
    const QPoint vertexStart = canvas.mapFromScene(QPointF(190, 583));
    const QPoint vertexEnd = canvas.mapFromScene(QPointF(220, 620));
    QMouseEvent vertexHover(QEvent::MouseMove, QPointF(vertexStart), Qt::NoButton,
                            Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &vertexHover);
    if (!require(canvas.cursor().shape() == Qt::SizeFDiagCursor,
                 "hovering the top-left IVA corner must show a diagonal resize cursor")) return 1;
    QMouseEvent vertexPress(QEvent::MouseButtonPress, QPointF(vertexStart),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &vertexPress);
    QMouseEvent vertexMove(QEvent::MouseMove, QPointF(vertexEnd), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &vertexMove);
    QMouseEvent vertexRelease(QEvent::MouseButtonRelease, QPointF(vertexEnd),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &vertexRelease);
    if (!require(qAbs(editedRectangle.x() - 220.0) < 3.0
                     && qAbs(editedRectangle.y() - 620.0) < 3.0,
                 "dragging an IVA corner must redraw the rectangle from that vertex")) return 1;

    std::cout << "PASS: IVA video canvas maps rectangles, edits boxes, and blocks unsafe aspect ratios\n";
    return 0;
}
