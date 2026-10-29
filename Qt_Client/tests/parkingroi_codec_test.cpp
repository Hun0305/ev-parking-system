#include "api/parkingroi.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const ParkingRoi expected{0.371528, 0.298026, 0.113426, 0.275658};
    QString error;
    if (!require(expected.isValid(&error), "valid normalized ROI must pass")) return 1;
    if (!require(!ParkingRoi{-0.1, 0.0, 0.2, 0.2}.isValid(),
                 "negative x must fail")) return 2;
    if (!require(!ParkingRoi{0.9, 0.0, 0.2, 0.2}.isValid(),
                 "x plus width above one must fail")) return 3;
    if (!require(!ParkingRoi{0.0, 0.0, 0.0, 0.2}.isValid(),
                 "zero width must fail")) return 4;

    const QSize sourceSize(100, 100);
    if (!require(!ParkingRoi{0.0, 0.0, 0.07, 0.08}.isLargeEnough(sourceSize),
                 "a 7x8 source-pixel ROI must fail")) return 8;
    if (!require(!ParkingRoi{0.0, 0.0, 0.08, 0.07}.isLargeEnough(sourceSize),
                 "an 8x7 source-pixel ROI must fail")) return 9;
    if (!require(ParkingRoi{0.0, 0.0, 0.08, 0.08}.isLargeEnough(sourceSize),
                 "an 8x8 source-pixel ROI must pass")) return 10;
    if (!require(!ParkingRoi{0.0, 0.0, 1.0, 1.0}.isLargeEnough(QSize()),
                 "an unavailable source frame size must fail")) return 11;

    QJsonArray items;
    items.append(QJsonObject{
        {QStringLiteral("slotId"), QStringLiteral("EV01")},
        {QStringLiteral("roi"), ParkingRoiCodec::toJson(expected)}});
    items.append(QJsonObject{
        {QStringLiteral("slotId"), QStringLiteral("EV02")},
        {QStringLiteral("roi"), QJsonObject{
            {QStringLiteral("x"), 0.1},
            {QStringLiteral("y"), 0.2},
            {QStringLiteral("width"), 0.3},
            {QStringLiteral("height"), 0.4}}}});
    const QJsonDocument listDocument(
        QJsonObject{{QStringLiteral("items"), items}});
    ParkingRoiMap rois;
    if (!require(ParkingRoiCodec::parseList(listDocument, rois, error)
                     && rois.size() == 2
                     && rois.value(QStringLiteral("EV01")).nearlyEquals(expected),
                 "ROI list JSON must preserve normalized coordinates")) return 5;

    QString slotId;
    ParkingRoi parsed;
    bool applied = false;
    const QJsonDocument saveDocument(QJsonObject{
        {QStringLiteral("success"), true},
        {QStringLiteral("slotId"), QStringLiteral("EV01")},
        {QStringLiteral("appliedImmediately"), true},
        {QStringLiteral("roi"), ParkingRoiCodec::toJson(expected)}});
    if (!require(ParkingRoiCodec::parseSingle(
                     saveDocument, slotId, parsed, &applied, error)
                     && slotId == QStringLiteral("EV01") && applied
                     && parsed.nearlyEquals(expected),
                 "PUT response JSON must parse server ROI and apply flag")) return 6;

    const QJsonObject invalidRoi{
        {QStringLiteral("x"), 0.9}, {QStringLiteral("y"), 0.1},
        {QStringLiteral("width"), 0.2}, {QStringLiteral("height"), 0.2}};
    const QJsonDocument invalidDocument(QJsonObject{
        {QStringLiteral("slotId"), QStringLiteral("EV01")},
        {QStringLiteral("roi"), invalidRoi}});
    if (!require(!ParkingRoiCodec::parseSingle(
                     invalidDocument, slotId, parsed, nullptr, error),
                 "invalid server ROI must be rejected")) return 7;
    std::cout << "PASS: parking ROI validation and JSON codec\n";
    return 0;
}
