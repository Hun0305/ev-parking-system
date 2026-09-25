#include "ivaareaoptionsparser.h"

#include <QJsonArray>

namespace {
bool parseNonNegativeInteger(const QJsonValue &value, int &result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    const int integer = value.toInt(-1);
    if (number != static_cast<double>(integer) || integer < 0) {
        return false;
    }
    result = integer;
    return true;
}

bool parseRange(const QJsonObject &object,
                const QString &name,
                IvaIntegerRange &range,
                QString &errorMessage)
{
    const QJsonValue value = object.value(name);
    if (!value.isObject()) {
        errorMessage = QStringLiteral("%1 must be an object").arg(name);
        return false;
    }
    const QJsonObject rangeObject = value.toObject();
    if (!parseNonNegativeInteger(rangeObject.value(QStringLiteral("min")),
                                 range.minimum)
        || !parseNonNegativeInteger(rangeObject.value(QStringLiteral("max")),
                                    range.maximum)
        || !range.isValid()) {
        errorMessage = QStringLiteral("%1 requires a valid min/max range").arg(name);
        return false;
    }
    return true;
}

bool parseStringArray(const QJsonObject &object,
                      const QString &name,
                      QStringList &items,
                      QString &errorMessage)
{
    const QJsonValue value = object.value(name);
    if (!value.isArray()) {
        errorMessage = QStringLiteral("%1 must be an array").arg(name);
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString() || item.toString().trimmed().isEmpty()) {
            errorMessage = QStringLiteral("%1 requires non-empty strings").arg(name);
            return false;
        }
        items.append(item.toString().trimmed());
    }
    return true;
}
}

bool IvaAreaOptionsParser::parse(const QJsonDocument &document,
                                 IvaAreaOptions &options,
                                 QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("WiseAI IVA options must be a JSON object");
        return false;
    }
    const QJsonValue optionsValue = document.object().value(
        QStringLiteral("ivaAreaOptions"));
    if (!optionsValue.isArray()) {
        errorMessage = QStringLiteral("WiseAI IVA options are missing ivaAreaOptions");
        return false;
    }

    IvaAreaOptions parsed;
    parsed.rawDocument = document;
    const QJsonArray channelArray = optionsValue.toArray();
    for (int index = 0; index < channelArray.size(); ++index) {
        if (!channelArray.at(index).isObject()) {
            errorMessage = QStringLiteral("ivaAreaOptions item %1 must be an object")
                               .arg(index);
            return false;
        }
        const QJsonObject channelObject = channelArray.at(index).toObject();
        IvaChannelOptions channel;
        if (!parseNonNegativeInteger(channelObject.value(QStringLiteral("channel")),
                                     channel.channel)) {
            errorMessage = QStringLiteral(
                "ivaAreaOptions item %1 requires a non-negative channel")
                               .arg(index);
            return false;
        }
        const QJsonValue definedValue = channelObject.value(
            QStringLiteral("definedAreaOptions"));
        if (!definedValue.isObject()) {
            errorMessage = QStringLiteral(
                "ivaAreaOptions item %1 is missing definedAreaOptions")
                               .arg(index);
            return false;
        }
        const QJsonObject defined = definedValue.toObject();
        QString fieldError;
        if (!parseRange(defined, QStringLiteral("index"), channel.areaIndex,
                        fieldError)
            || !parseRange(defined, QStringLiteral("areaCoordinates"),
                           channel.areaCoordinateCount, fieldError)
            || !parseRange(defined, QStringLiteral("appearanceDuration"),
                           channel.appearanceDuration, fieldError)
            || !parseRange(defined, QStringLiteral("intrusionDuration"),
                           channel.intrusionDuration, fieldError)
            || !parseRange(defined, QStringLiteral("loiteringDuration"),
                           channel.loiteringDuration, fieldError)
            || !parseStringArray(defined, QStringLiteral("detectionModes"),
                                 channel.detectionModes, fieldError)
            || !parseStringArray(defined, QStringLiteral("objectTypeFilter"),
                                 channel.objectTypeFilters, fieldError)) {
            errorMessage = QStringLiteral("ivaAreaOptions item %1: %2")
                               .arg(index)
                               .arg(fieldError);
            return false;
        }
        channel.rawOptions = channelObject;
        parsed.channels.append(channel);
    }

    options = parsed;
    errorMessage.clear();
    return true;
}
