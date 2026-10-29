#include "ivaareaparser.h"

#include <QJsonArray>

#include <cmath>
#include <initializer_list>

namespace {
QJsonValue firstValue(const QJsonObject &object,
                      std::initializer_list<QString> keys)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (!value.isUndefined()) {
            return value;
        }
    }
    return {};
}

QString firstString(const QJsonObject &object,
                    std::initializer_list<QString> keys)
{
    const QJsonValue value = firstValue(object, keys);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool parseAreaIndex(const QJsonObject &definition, int &areaIndex)
{
    const QJsonValue value = firstValue(
        definition,
        {QStringLiteral("index"), QStringLiteral("areaIndex")});
    if (value.isUndefined() || value.isNull()) {
        areaIndex = -1;
        return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const int integer = value.toInt(-1);
        if (number == static_cast<double>(integer) && integer >= 0) {
            areaIndex = integer;
            return true;
        }
        return false;
    }
    if (value.isString()) {
        bool ok = false;
        const int integer = value.toString().toInt(&ok);
        if (ok && integer >= 0) {
            areaIndex = integer;
            return true;
        }
    }
    return false;
}

bool parseStringArray(const QJsonObject &definition,
                      std::initializer_list<QString> keys,
                      const QString &fieldName,
                      QStringList &items,
                      QString &errorMessage)
{
    const QJsonValue value = firstValue(definition, keys);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        errorMessage = QStringLiteral("%1 must be an array").arg(fieldName);
        return false;
    }

    const QJsonArray array = value.toArray();
    for (int index = 0; index < array.size(); ++index) {
        if (!array.at(index).isString()
            || array.at(index).toString().trimmed().isEmpty()) {
            errorMessage = QStringLiteral(
                "%1 item %2 must be a non-empty string")
                               .arg(fieldName)
                               .arg(index);
            return false;
        }
        items.append(array.at(index).toString().trimmed());
    }
    return true;
}

bool parseOptionalDuration(const QJsonObject &definition,
                           std::initializer_list<QString> keys,
                           int &duration)
{
    const QJsonValue value = firstValue(definition, keys);
    if (value.isUndefined() || value.isNull()) {
        duration = -1;
        return true;
    }
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    const int integer = value.toInt(-1);
    if (number != static_cast<double>(integer) || integer < 0) {
        return false;
    }
    duration = integer;
    return true;
}

bool parseCoordinates(const QJsonObject &definition,
                      QList<QPointF> &coordinates,
                      QString &errorMessage)
{
    const QJsonValue value = definition.value(QStringLiteral("areaCoordinates"));
    if (!value.isArray()) {
        errorMessage = QStringLiteral("areaCoordinates must be an array");
        return false;
    }

    const QJsonArray points = value.toArray();
    if (points.size() < 3) {
        errorMessage = QStringLiteral("areaCoordinates requires at least three points");
        return false;
    }
    for (int index = 0; index < points.size(); ++index) {
        if (!points.at(index).isObject()) {
            errorMessage = QStringLiteral("areaCoordinates item %1 must be an object")
                               .arg(index);
            return false;
        }
        const QJsonObject point = points.at(index).toObject();
        const QJsonValue xValue = point.value(QStringLiteral("x"));
        const QJsonValue yValue = point.value(QStringLiteral("y"));
        if (!xValue.isDouble() || !yValue.isDouble()
            || !std::isfinite(xValue.toDouble()) || !std::isfinite(yValue.toDouble())) {
            errorMessage = QStringLiteral(
                "areaCoordinates item %1 requires finite numeric x and y")
                               .arg(index);
            return false;
        }
        coordinates.append(QPointF(xValue.toDouble(), yValue.toDouble()));
    }
    return true;
}
}

bool IvaAreaParser::parse(const QJsonDocument &document,
                          IvaAreaConfiguration &configuration,
                          QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("WiseAI IVA configuration must be a JSON object");
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue ivaAreaValue = root.value(QStringLiteral("ivaArea"));
    if (!ivaAreaValue.isArray()) {
        errorMessage = QStringLiteral(
            "WiseAI IVA configuration is missing the ivaArea array");
        return false;
    }

    IvaAreaConfiguration parsed;
    parsed.rawDocument = document;
    const QJsonArray containers = ivaAreaValue.toArray();
    for (int containerIndex = 0; containerIndex < containers.size(); ++containerIndex) {
        if (!containers.at(containerIndex).isObject()) {
            errorMessage = QStringLiteral("ivaArea item %1 must be an object")
                               .arg(containerIndex);
            return false;
        }

        const QJsonObject container = containers.at(containerIndex).toObject();
        const QJsonValue channelValue = container.value(QStringLiteral("channel"));
        if (!channelValue.isDouble()
            || channelValue.toDouble() != static_cast<double>(channelValue.toInt(-1))
            || channelValue.toInt(-1) < 0) {
            errorMessage = QStringLiteral(
                "ivaArea item %1 requires a non-negative integer channel")
                               .arg(containerIndex);
            return false;
        }
        const QJsonValue enabledValue = container.value(QStringLiteral("enable"));
        if (!enabledValue.isBool()) {
            errorMessage = QStringLiteral("ivaArea item %1 requires a boolean enable")
                               .arg(containerIndex);
            return false;
        }
        const QJsonValue definedAreaValue = container.value(QStringLiteral("definedArea"));
        if (!definedAreaValue.isArray()) {
            errorMessage = QStringLiteral(
                "ivaArea item %1 is missing the definedArea array")
                               .arg(containerIndex);
            return false;
        }

        IvaChannelDefinition channel;
        channel.containerIndex = containerIndex;
        channel.channel = channelValue.toInt();
        channel.enabled = enabledValue.toBool();
        channel.rawContainer = container;
        parsed.channels.append(channel);

        const QString containerToken = firstString(
            container,
            {QStringLiteral("videoSourceToken"), QStringLiteral("VideoSourceToken"),
             QStringLiteral("video_source_token")});
        const QJsonArray definitions = definedAreaValue.toArray();
        for (int definitionIndex = 0; definitionIndex < definitions.size();
             ++definitionIndex) {
            if (!definitions.at(definitionIndex).isObject()) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2 must be an object")
                                   .arg(containerIndex)
                                   .arg(definitionIndex);
                return false;
            }

            const QJsonObject definition = definitions.at(definitionIndex).toObject();
            IvaAreaDefinition area;
            area.containerIndex = containerIndex;
            area.definitionIndex = definitionIndex;
            area.channel = channelValue.toInt();
            area.channelEnabled = enabledValue.toBool();
            area.name = firstString(definition, {QStringLiteral("name")});
            if (area.name.isEmpty()) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2 requires a non-empty name")
                                   .arg(containerIndex)
                                   .arg(definitionIndex);
                return false;
            }
            if (!parseAreaIndex(definition, area.areaIndex)) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2 has an invalid index")
                                   .arg(containerIndex)
                                   .arg(definitionIndex);
                return false;
            }

            area.videoSourceToken = firstString(
                definition,
                {QStringLiteral("videoSourceToken"), QStringLiteral("VideoSourceToken"),
                 QStringLiteral("video_source_token")});
            if (area.videoSourceToken.isEmpty()) {
                area.videoSourceToken = containerToken;
            }

            QString fieldError;
            if (!parseStringArray(
                    definition,
                    {QStringLiteral("detectionModes"), QStringLiteral("detection_modes")},
                    QStringLiteral("detectionModes"), area.detectionModes, fieldError)) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2: %3")
                                   .arg(containerIndex)
                                   .arg(definitionIndex)
                                   .arg(fieldError);
                return false;
            }
            if (!parseStringArray(
                definition,
                {QStringLiteral("objectTypeFilter"), QStringLiteral("object_type_filter")},
                QStringLiteral("objectTypeFilter"), area.objectTypeFilter, fieldError)) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2: %3")
                                   .arg(containerIndex)
                                   .arg(definitionIndex)
                                   .arg(fieldError);
                return false;
            }
            if (!parseCoordinates(definition, area.areaCoordinates, fieldError)) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2: %3")
                                   .arg(containerIndex)
                                   .arg(definitionIndex)
                                   .arg(fieldError);
                return false;
            }
            if (!parseOptionalDuration(
                definition,
                {QStringLiteral("appearanceDuration"), QStringLiteral("appearance_duration")},
                area.appearanceDuration)
                || !parseOptionalDuration(
                definition,
                {QStringLiteral("intrusionDuration"), QStringLiteral("intrusion_duration")},
                area.intrusionDuration)
                || !parseOptionalDuration(
                definition,
                {QStringLiteral("loiteringDuration"), QStringLiteral("loitering_duration")},
                area.loiteringDuration)) {
                errorMessage = QStringLiteral(
                    "ivaArea item %1 definedArea item %2 has an invalid duration")
                                   .arg(containerIndex)
                                   .arg(definitionIndex);
                return false;
            }
            area.rawContainer = container;
            area.rawDefinition = definition;

            if (area.detectionModes.isEmpty()) {
                parsed.warnings.append(QStringLiteral(
                    "Area '%1' has no active detection modes")
                                           .arg(area.name));
            }
            parsed.areas.append(area);
        }
    }

    configuration = parsed;
    errorMessage.clear();
    return true;
}
