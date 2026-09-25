#include "iva/ivaareaoptionsparser.h"
#include "iva/ivaareaupdatebuilder.h"

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

QJsonDocument optionsDocument()
{
    const QJsonObject defined{
        {QStringLiteral("appearanceDuration"),
         QJsonObject{{QStringLiteral("min"), 1}, {QStringLiteral("max"), 60}}},
        {QStringLiteral("areaCoordinates"),
         QJsonObject{{QStringLiteral("min"), 4}, {QStringLiteral("max"), 8}}},
        {QStringLiteral("detectionModes"),
         QJsonArray{QStringLiteral("Intrusion"), QStringLiteral("Loitering")}},
        {QStringLiteral("index"),
         QJsonObject{{QStringLiteral("min"), 1}, {QStringLiteral("max"), 4}}},
        {QStringLiteral("intrusionDuration"),
         QJsonObject{{QStringLiteral("min"), 0}, {QStringLiteral("max"), 5}}},
        {QStringLiteral("loiteringDuration"),
         QJsonObject{{QStringLiteral("min"), 1}, {QStringLiteral("max"), 600}}},
        {QStringLiteral("objectTypeFilter"),
         QJsonArray{QStringLiteral("Person"), QStringLiteral("Vehicle.Car")}}};
    return QJsonDocument(QJsonObject{
        {QStringLiteral("ivaAreaOptions"),
         QJsonArray{QJsonObject{{QStringLiteral("channel"), 2},
                                {QStringLiteral("definedAreaOptions"), defined}}}}});
}

IvaAreaDefinition validArea()
{
    IvaAreaDefinition area;
    area.channel = 2;
    area.areaIndex = 1;
    area.name = QStringLiteral("EV-01");
    area.detectionModes = {QStringLiteral("Intrusion"), QStringLiteral("Loitering")};
    area.objectTypeFilter = {QStringLiteral("Vehicle.Car")};
    area.areaCoordinates = {QPointF(90, 533), QPointF(782, 533),
                            QPointF(782, 1344), QPointF(90, 1344)};
    area.appearanceDuration = 1;
    area.intrusionDuration = 5;
    area.loiteringDuration = 30;
    area.rawDefinition = QJsonObject{
        {QStringLiteral("handoverIndex"), 3},
        {QStringLiteral("vendorExtension"), QStringLiteral("do-not-send")}};
    return area;
}

IvaAreaConfiguration configurationFor(int channel,
                                      bool enabled,
                                      const QList<IvaAreaDefinition> &areas)
{
    IvaAreaConfiguration configuration;
    configuration.channels.append(IvaChannelDefinition{
        -1, channel, enabled, {}});
    configuration.areas = areas;
    return configuration;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    IvaAreaOptions options;
    QString error;
    if (!require(IvaAreaOptionsParser::parse(optionsDocument(), options, error),
                 "actual WiseAI ivaAreaOptions schema must parse")) return 1;
    const IvaChannelOptions *channelOptions = options.forChannel(2);
    if (!require(channelOptions && channelOptions->areaIndex.minimum == 1
                     && channelOptions->areaIndex.maximum == 4
                     && channelOptions->areaCoordinateCount.minimum == 4
                     && channelOptions->areaCoordinateCount.maximum == 8,
                 "camera-provided rule and polygon ranges must normalize")) return 1;

    IvaAreaDefinition area = validArea();
    QJsonObject payload;
    if (!require(IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area}, *channelOptions, payload, error),
                 "valid channel edits must produce a PUT payload")) return 1;
    if (!require(payload.keys()
                     == QStringList{QStringLiteral("channel"),
                                    QStringLiteral("definedArea"),
                                    QStringLiteral("enable")},
                 "PUT root must match the WebViewer channel contract")) return 1;
    const QJsonObject definition = payload.value(QStringLiteral("definedArea"))
                                       .toArray().at(0).toObject();
    if (!require(definition.value(QStringLiteral("handoverIndex")).toInt() == 3
                     && !definition.contains(QStringLiteral("vendorExtension")),
                 "WebViewer whitelist must preserve handoverIndex and omit unknown fields")) return 1;
    if (!require(IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {}, *channelOptions, payload, error)
                     && payload.value(QStringLiteral("definedArea")).toArray().isEmpty(),
                 "an empty channel must produce an empty definedArea verification payload")) return 1;

    area.areaCoordinates.removeLast();
    if (!require(!IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area}, *channelOptions, payload, error)
                     && error.contains(QStringLiteral("point count"),
                                       Qt::CaseInsensitive),
                 "camera-provided polygon limits must reject three points")) return 1;

    area = validArea();
    area.detectionModes = {QStringLiteral("UnknownMode")};
    if (!require(!IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area}, *channelOptions, payload, error)
                     && error.contains(QStringLiteral("unsupported"),
                                       Qt::CaseInsensitive),
                 "unsupported camera detection modes must be rejected")) return 1;

    area = validArea();
    area.areaCoordinates[0].setX(90.5);
    if (!require(!IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area}, *channelOptions, payload, error)
                     && error.contains(QStringLiteral("integer pixels"),
                                       Qt::CaseInsensitive),
                 "fractional coordinates must be rejected before the camera can corrupt them")) return 1;

    area = validArea();
    area.areaCoordinates[0].setX(2593);
    if (!require(!IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area}, *channelOptions, payload, error,
                     QSize(2592, 1520))
                     && error.contains(QStringLiteral("resolution"),
                                       Qt::CaseInsensitive),
                 "coordinates outside the capability resolution must be rejected")) return 1;

    area = validArea();
    IvaAreaDefinition duplicate = area;
    duplicate.name = QStringLiteral("EV-02");
    if (!require(!IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area, duplicate}, *channelOptions, payload, error)
                     && error.contains(QStringLiteral("duplicated")),
                 "duplicate rule indexes must be rejected before PUT")) return 1;

    IvaAreaDefinition second = validArea();
    second.areaIndex = 2;
    second.name = QStringLiteral("EV-02");
    second.areaCoordinates = {QPointF(100, 540), QPointF(700, 540),
                              QPointF(700, 1200), QPointF(100, 1200)};
    second.detectionModes = {QStringLiteral("Intrusion")};
    second.objectTypeFilter = {QStringLiteral("Vehicle.Car")};
    second.rawDefinition = {};

    if (!require(IvaAreaUpdateBuilder::buildChannelPayload(
                     2, true, {area, second}, *channelOptions, payload, error),
                 "two valid areas must produce a verification payload")) return 1;

    IvaAreaDefinition cameraArea = area;
    cameraArea.detectionModes = {QStringLiteral("Loitering"),
                                 QStringLiteral("Intrusion")};
    cameraArea.areaCoordinates = {QPointF(783, 1345), QPointF(91, 1345),
                                  QPointF(91, 534), QPointF(783, 534)};
    IvaAreaDefinition cameraSecond = second;
    cameraSecond.detectionModes = {QStringLiteral("Intrusion")};
    cameraSecond.areaCoordinates = {QPointF(701, 1201), QPointF(101, 1201),
                                    QPointF(101, 541), QPointF(701, 541)};
    const IvaAreaConfiguration normalizedCameraConfiguration =
        configurationFor(2, true, {cameraSecond, cameraArea});
    if (!require(IvaAreaUpdateBuilder::payloadMatchesChannel(
                     payload, normalizedCameraConfiguration, *channelOptions,
                     error),
                 "verification must accept reordered areas, polygon winding, "
                 "mode order, and one-pixel camera normalization")) return 1;

    IvaAreaDefinition changedArea = cameraArea;
    changedArea.name = QStringLiteral("unexpected-camera-rule");
    const IvaAreaConfiguration changedCameraConfiguration =
        configurationFor(2, true, {cameraSecond, changedArea});
    if (!require(!IvaAreaUpdateBuilder::payloadMatchesChannel(
                     payload, changedCameraConfiguration, *channelOptions,
                     error)
                     && error.contains(QStringLiteral("rule name"),
                                       Qt::CaseInsensitive),
                 "verification must reject a real rule-name change")) return 1;

    std::cout << "PASS: WiseAI PUT payload validation and semantic verification\n";
    return 0;
}
