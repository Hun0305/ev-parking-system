#include "iva/ivaareaparser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

QJsonDocument documentedConfiguration()
{
    const QJsonObject firstArea{
        {QStringLiteral("name"), QStringLiteral("parking-a")},
        {QStringLiteral("index"), 7},
        {QStringLiteral("detectionModes"),
         QJsonArray{QStringLiteral("Intrusion"), QStringLiteral("Loitering")}},
        {QStringLiteral("intrusionDuration"), 5},
        {QStringLiteral("loiteringDuration"), 15},
        {QStringLiteral("appearanceDuration"), 10},
        {QStringLiteral("objectTypeFilter"),
         QJsonArray{QStringLiteral("Vehicle.Car"), QStringLiteral("Vehicle.Truck")}},
        {QStringLiteral("areaCoordinates"),
         QJsonArray{QJsonObject{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20}},
                    QJsonObject{{QStringLiteral("x"), 90}, {QStringLiteral("y"), 20}},
                    QJsonObject{{QStringLiteral("x"), 90}, {QStringLiteral("y"), 80}}}},
        {QStringLiteral("vendorExtension"), QStringLiteral("must-survive")}};
    const QJsonObject secondArea{
        {QStringLiteral("name"), QStringLiteral("parking-b")},
        {QStringLiteral("videoSourceToken"), QStringLiteral("vs-1")},
        {QStringLiteral("detectionModes"), QJsonArray{QStringLiteral("Enter")}},
        {QStringLiteral("objectTypeFilter"), QJsonArray{QStringLiteral("Person")}},
        {QStringLiteral("areaCoordinates"),
         QJsonArray{QJsonObject{{QStringLiteral("x"), 5}, {QStringLiteral("y"), 5}},
                    QJsonObject{{QStringLiteral("x"), 50}, {QStringLiteral("y"), 5}},
                    QJsonObject{{QStringLiteral("x"), 50}, {QStringLiteral("y"), 50}}}}};
    const QJsonObject container{
        {QStringLiteral("channel"), 0},
        {QStringLiteral("enable"), true},
        {QStringLiteral("videoSourceToken"), QStringLiteral("vs-0")},
        {QStringLiteral("definedArea"), QJsonArray{firstArea, secondArea}},
        {QStringLiteral("containerExtension"), 123}};
    return QJsonDocument(QJsonObject{
        {QStringLiteral("ivaArea"), QJsonArray{container}},
        {QStringLiteral("rootExtension"), QStringLiteral("keep-me")}});
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QJsonDocument source = documentedConfiguration();
    IvaAreaConfiguration configuration;
    QString error;
    if (!require(IvaAreaParser::parse(source, configuration, error),
                 "documented ivaArea/definedArea structure must parse")) return 1;
    if (!require(error.isEmpty(), "successful parse must clear the error")) return 1;
    if (!require(configuration.areas.size() == 2,
                 "all definedArea entries must be normalized")) return 1;
    if (!require(configuration.channels.size() == 1
                     && configuration.channels.at(0).channel == 0
                     && configuration.channels.at(0).enabled,
                 "ivaArea channel containers must be normalized")) return 1;
    if (!require(configuration.areas.at(0).videoSourceToken == QStringLiteral("vs-0"),
                 "container VideoSourceToken must be inherited")) return 1;
    if (!require(configuration.areas.at(1).videoSourceToken == QStringLiteral("vs-1"),
                 "definition VideoSourceToken must override the container")) return 1;
    if (!require(configuration.areas.at(0).areaIndex == 7,
                 "numeric area index must be normalized")) return 1;
    if (!require(configuration.areas.at(0).channel == 0
                     && configuration.areas.at(0).channelEnabled,
                 "container channel state must be normalized")) return 1;
    if (!require(configuration.areas.at(0).detectionModes
                     == QStringList{QStringLiteral("Intrusion"), QStringLiteral("Loitering")},
                 "detection modes must retain order")) return 1;
    if (!require(configuration.areas.at(0).objectTypeFilter
                     == QStringList{QStringLiteral("Vehicle.Car"),
                                    QStringLiteral("Vehicle.Truck")},
                 "object filters must be normalized as strings")) return 1;
    if (!require(configuration.areas.at(0).areaCoordinates.size() == 3
                     && configuration.areas.at(0).areaCoordinates.at(1)
                            == QPointF(90.0, 20.0),
                 "areaCoordinates x/y points must be normalized")) return 1;
    if (!require(configuration.areas.at(0).appearanceDuration == 10
                     && configuration.areas.at(0).intrusionDuration == 5
                     && configuration.areas.at(0).loiteringDuration == 15,
                 "non-negative integer durations must be normalized")) return 1;
    if (!require(configuration.areas.at(0).rawDefinition
                         .value(QStringLiteral("vendorExtension"))
                         .toString()
                     == QStringLiteral("must-survive"),
                 "unknown definition fields must be preserved")) return 1;
    if (!require(configuration.rawDocument.toJson(QJsonDocument::Compact)
                     == source.toJson(QJsonDocument::Compact),
                 "the complete source document must be preserved without reconstruction")) return 1;
    if (!require(configuration.warnings.isEmpty(),
                 "areas with a token and polygon must not emit warnings")) return 1;

    IvaAreaConfiguration unchanged = configuration;
    const QJsonDocument missingRootArray(QJsonObject{{QStringLiteral("ivaArea"), true}});
    if (!require(!IvaAreaParser::parse(missingRootArray, unchanged, error),
                 "non-array ivaArea must be rejected")) return 1;
    if (!require(unchanged.areas.size() == 2,
                 "a failed parse must not replace the last-good result")) return 1;

    const QJsonDocument missingDefinedArea(QJsonObject{
        {QStringLiteral("ivaArea"),
         QJsonArray{QJsonObject{{QStringLiteral("channel"), 0},
                                {QStringLiteral("enable"), true}}}}});
    if (!require(!IvaAreaParser::parse(missingDefinedArea, unchanged, error),
                 "missing definedArea array must be rejected")) return 1;

    const QJsonDocument missingName(QJsonObject{
        {QStringLiteral("ivaArea"),
         QJsonArray{QJsonObject{{QStringLiteral("definedArea"),
                                 QJsonArray{QJsonObject{{QStringLiteral("areaCoordinates"),
                                                         QJsonArray{}}}}},
                                {QStringLiteral("channel"), 0},
                                {QStringLiteral("enable"), true}}}}});
    if (!require(!IvaAreaParser::parse(missingName, unchanged, error),
                 "an unnamed defined area must be rejected")) return 1;

    const QJsonArray validCoordinates{
        QJsonObject{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}},
        QJsonObject{{QStringLiteral("x"), 1}, {QStringLiteral("y"), 0}},
        QJsonObject{{QStringLiteral("x"), 1}, {QStringLiteral("y"), 1}}};
    const QJsonObject invalidModeArea{
        {QStringLiteral("name"), QStringLiteral("bad-modes")},
        {QStringLiteral("detectionModes"), QStringLiteral("Loitering")},
        {QStringLiteral("areaCoordinates"), validCoordinates}};
    const QJsonObject invalidModeContainer{
        {QStringLiteral("channel"), 0},
        {QStringLiteral("enable"), true},
        {QStringLiteral("definedArea"), QJsonArray{invalidModeArea}}};
    const QJsonDocument invalidModes(QJsonObject{
        {QStringLiteral("ivaArea"), QJsonArray{invalidModeContainer}}});
    if (!require(!IvaAreaParser::parse(invalidModes, unchanged, error),
                 "a scalar detectionModes value must be rejected")) return 1;

    const QJsonObject invalidCoordinateArea{
        {QStringLiteral("name"), QStringLiteral("bad-polygon")},
        {QStringLiteral("areaCoordinates"),
         QJsonArray{QJsonObject{{QStringLiteral("x"), 0}}}}};
    const QJsonObject invalidCoordinateContainer{
        {QStringLiteral("channel"), 0},
        {QStringLiteral("enable"), true},
        {QStringLiteral("definedArea"), QJsonArray{invalidCoordinateArea}}};
    const QJsonDocument invalidCoordinates(QJsonObject{
        {QStringLiteral("ivaArea"), QJsonArray{invalidCoordinateContainer}}});
    if (!require(!IvaAreaParser::parse(invalidCoordinates, unchanged, error),
                 "an invalid coordinate list must be rejected")) return 1;

    const QJsonDocument emptyChannel(QJsonObject{
        {QStringLiteral("ivaArea"),
         QJsonArray{QJsonObject{{QStringLiteral("channel"), 1},
                                {QStringLiteral("enable"), false},
                                {QStringLiteral("definedArea"), QJsonArray{}}}}}});
    IvaAreaConfiguration emptyConfiguration;
    if (!require(IvaAreaParser::parse(emptyChannel, emptyConfiguration, error),
                 "a disabled channel with no areas must be valid")) return 1;
    if (!require(emptyConfiguration.areas.isEmpty(),
                 "an empty channel must not invent area definitions")) return 1;
    if (!require(emptyConfiguration.channels.size() == 1
                     && !emptyConfiguration.channels.at(0).enabled,
                 "an empty disabled channel must remain visible")) return 1;

    std::cout << "PASS: WiseAI IVA JSON is validated, normalized, and preserved for safe editing\n";
    return 0;
}
