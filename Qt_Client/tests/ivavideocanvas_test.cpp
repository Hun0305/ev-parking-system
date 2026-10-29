#include "iva/ivavideocanvas.h"

#include <QApplication>
#include <QImage>

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

    std::cout << "PASS: IVA video canvas maps rectangle coordinates and blocks unsafe aspect ratios\n";
    return 0;
}
