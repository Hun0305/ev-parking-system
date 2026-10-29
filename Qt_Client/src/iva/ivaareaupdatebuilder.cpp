#include "ivaareaupdatebuilder.h"

#include <QJsonArray>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
bool validateSupportedValues(const QStringList &values,
                             const QStringList &supported,
                             const QString &fieldName,
                             QString &errorMessage)
{
    for (const QString &value : values) {
        if (!supported.contains(value)) {
            errorMessage = QStringLiteral("%1 contains unsupported value '%2'")
                               .arg(fieldName, value);
            return false;
        }
    }
    return true;
}

bool validateRange(int value,
                   const IvaIntegerRange &range,
                   const QString &fieldName,
                   QString &errorMessage)
{
    if (!range.contains(value)) {
        errorMessage = QStringLiteral("%1 must be between %2 and %3")
                           .arg(fieldName)
                           .arg(range.minimum)
                           .arg(range.maximum);
        return false;
    }
    return true;
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList normalizedStringArray(const QJsonValue &value)
{
    QStringList values;
    if (!value.isArray()) return values;
    for (const QJsonValue &item : value.toArray()) {
        if (item.isString()) values.append(item.toString());
    }
    values.sort();
    return values;
}

bool polygonMatches(const QJsonValue &expectedValue,
                    const QList<QPointF> &actual,
                    const QString &areaLabel,
                    QString &errorMessage)
{
    if (!expectedValue.isArray()) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 polygon is missing").arg(areaLabel);
        return false;
    }
    const QJsonArray expected = expectedValue.toArray();
    if (expected.size() != actual.size() || expected.isEmpty()) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 polygon point count expected %2, actual %3")
                           .arg(areaLabel)
                           .arg(expected.size())
                           .arg(actual.size());
        return false;
    }

    auto pointMatches = [&](int expectedIndex, int actualIndex) {
        const QJsonObject point = expected.at(expectedIndex).toObject();
        if (!point.contains(QStringLiteral("x"))
            || !point.contains(QStringLiteral("y"))) {
            return false;
        }
        const QPointF actualPoint = actual.at(actualIndex);
        return std::abs(point.value(QStringLiteral("x")).toDouble()
                        - actualPoint.x()) <= 1.0
            && std::abs(point.value(QStringLiteral("y")).toDouble()
                        - actualPoint.y()) <= 1.0;
    };

    // A polygon may be returned with another starting vertex, or in the
    // opposite winding direction, while describing the same camera area.
    for (int offset = 0; offset < actual.size(); ++offset) {
        bool forward = true;
        bool reverse = true;
        for (int index = 0; index < expected.size(); ++index) {
            forward = forward
                && pointMatches(index, (offset + index) % actual.size());
            const int reverseIndex = (offset - index + actual.size())
                % actual.size();
            reverse = reverse && pointMatches(index, reverseIndex);
        }
        if (forward || reverse) return true;
    }

    errorMessage = QStringLiteral(
        "Camera verification mismatch: %1 polygon coordinates differ")
                       .arg(areaLabel);
    return false;
}

bool compareArea(const QJsonObject &expected,
                 const IvaAreaDefinition &actual,
                 QString &errorMessage)
{
    const int expectedIndex = expected.value(QStringLiteral("index")).toInt(-1);
    const QString areaLabel = QStringLiteral("area index %1").arg(expectedIndex);
    if (actual.areaIndex != expectedIndex) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 index expected %2, actual %3")
                           .arg(areaLabel)
                           .arg(expectedIndex)
                           .arg(actual.areaIndex);
        return false;
    }
    if (actual.name.trimmed() != expected.value(QStringLiteral("name"))
                                    .toString().trimmed()) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 rule name differs")
                           .arg(areaLabel);
        return false;
    }

    const auto expectedModes = normalizedStringArray(
        expected.value(QStringLiteral("detectionModes")));
    const auto actualModes = [&actual]() {
        QStringList values = actual.detectionModes;
        values.sort();
        return values;
    }();
    if (expectedModes != actualModes) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 detection modes differ")
                           .arg(areaLabel);
        return false;
    }

    const auto expectedFilters = normalizedStringArray(
        expected.value(QStringLiteral("objectTypeFilter")));
    const auto actualFilters = [&actual]() {
        QStringList values = actual.objectTypeFilter;
        values.sort();
        return values;
    }();
    if (expectedFilters != actualFilters) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: %1 object filters differ")
                           .arg(areaLabel);
        return false;
    }

    const QList<QPair<QString, int>> durations{
        {QStringLiteral("appearanceDuration"), actual.appearanceDuration},
        {QStringLiteral("intrusionDuration"), actual.intrusionDuration},
        {QStringLiteral("loiteringDuration"), actual.loiteringDuration}};
    for (const auto &[field, actualValue] : durations) {
        const int expectedValue = expected.value(field).toInt(-1);
        if (expectedValue != actualValue) {
            errorMessage = QStringLiteral(
                "Camera verification mismatch: %1 %2 expected %3, actual %4")
                               .arg(areaLabel, field)
                               .arg(expectedValue)
                               .arg(actualValue);
            return false;
        }
    }

    return polygonMatches(expected.value(QStringLiteral("areaCoordinates")),
                          actual.areaCoordinates, areaLabel, errorMessage);
}
}

bool IvaAreaUpdateBuilder::buildChannelPayload(
    int channel,
    bool enabled,
    const QList<IvaAreaDefinition> &areas,
    const IvaChannelOptions &options,
    QJsonObject &payload,
    QString &errorMessage,
    const QSize &coordinateResolution)
{
    if (channel < 0 || options.channel != channel) {
        errorMessage = QStringLiteral("Channel options do not match the edited channel");
        return false;
    }

    QSet<int> indexes;
    QJsonArray definitions;
    for (const IvaAreaDefinition &area : areas) {
        if (area.channel != channel) {
            errorMessage = QStringLiteral("Edited areas must belong to one channel");
            return false;
        }
        if (area.name.trimmed().isEmpty()) {
            errorMessage = QStringLiteral("Every IVA rule requires a name");
            return false;
        }
        if (!validateRange(area.areaIndex, options.areaIndex,
                           QStringLiteral("Rule index"), errorMessage)
            || indexes.contains(area.areaIndex)) {
            if (indexes.contains(area.areaIndex)) {
                errorMessage = QStringLiteral("Rule index %1 is duplicated")
                                   .arg(area.areaIndex);
            }
            return false;
        }
        indexes.insert(area.areaIndex);
        if (!validateRange(area.areaCoordinates.size(),
                           options.areaCoordinateCount,
                           QStringLiteral("Polygon point count"), errorMessage)
            || !validateRange(area.appearanceDuration,
                              options.appearanceDuration,
                              QStringLiteral("Appearance duration"), errorMessage)
            || !validateRange(area.intrusionDuration,
                              options.intrusionDuration,
                              QStringLiteral("Intrusion duration"), errorMessage)
            || !validateRange(area.loiteringDuration,
                              options.loiteringDuration,
                              QStringLiteral("Loitering duration"), errorMessage)
            || !validateSupportedValues(area.detectionModes,
                                        options.detectionModes,
                                        QStringLiteral("Detection modes"),
                                        errorMessage)
            || !validateSupportedValues(area.objectTypeFilter,
                                        options.objectTypeFilters,
                                        QStringLiteral("Object filters"),
                                        errorMessage)) {
            return false;
        }

        QJsonArray coordinates;
        for (const QPointF &point : area.areaCoordinates) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())
                || point.x() < 0.0 || point.y() < 0.0
                || std::round(point.x()) != point.x()
                || std::round(point.y()) != point.y()) {
                errorMessage = QStringLiteral(
                    "Polygon coordinates must be finite non-negative integer pixels");
                return false;
            }
            if (coordinateResolution.isValid()
                && (point.x() > coordinateResolution.width()
                    || point.y() > coordinateResolution.height())) {
                errorMessage = QStringLiteral(
                    "Polygon coordinates must stay within the IVA resolution %1x%2")
                                   .arg(coordinateResolution.width())
                                   .arg(coordinateResolution.height());
                return false;
            }
            coordinates.append(QJsonObject{{QStringLiteral("x"), point.x()},
                                           {QStringLiteral("y"), point.y()}});
        }

        QJsonObject definition{
            {QStringLiteral("appearanceDuration"), area.appearanceDuration},
            {QStringLiteral("areaCoordinates"), coordinates},
            {QStringLiteral("detectionModes"), stringArray(area.detectionModes)},
            {QStringLiteral("index"), area.areaIndex},
            {QStringLiteral("intrusionDuration"), area.intrusionDuration},
            {QStringLiteral("loiteringDuration"), area.loiteringDuration},
            {QStringLiteral("name"), area.name.trimmed()},
            {QStringLiteral("objectTypeFilter"), stringArray(area.objectTypeFilter)}};
        const QJsonValue handoverIndex = area.rawDefinition.value(
            QStringLiteral("handoverIndex"));
        if (!handoverIndex.isUndefined()) {
            definition.insert(QStringLiteral("handoverIndex"), handoverIndex);
        }
        definitions.append(definition);
    }

    payload = QJsonObject{{QStringLiteral("channel"), channel},
                          {QStringLiteral("enable"), enabled},
                          {QStringLiteral("definedArea"), definitions}};
    errorMessage.clear();
    return true;
}

bool IvaAreaUpdateBuilder::payloadMatchesChannel(
    const QJsonObject &expectedPayload,
    const IvaAreaConfiguration &configuration,
    const IvaChannelOptions &options,
    QString &errorMessage,
    const QSize &coordinateResolution)
{
    Q_UNUSED(options);
    Q_UNUSED(coordinateResolution);
    const int channel = expectedPayload.value(QStringLiteral("channel")).toInt(-1);
    const auto channelIt = std::find_if(
        configuration.channels.cbegin(), configuration.channels.cend(),
        [channel](const IvaChannelDefinition &item) {
            return item.channel == channel;
        });
    if (channelIt == configuration.channels.cend()) {
        errorMessage = QStringLiteral("The verification response is missing channel %1")
                           .arg(channel);
        return false;
    }
    const bool expectedEnabled = expectedPayload.value(
        QStringLiteral("enable")).toBool();
    if (channelIt->enabled != expectedEnabled) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: channel %1 enabled state differs")
                           .arg(channel);
        return false;
    }

    QHash<int, QJsonObject> expectedAreas;
    const QJsonValue expectedDefinitions = expectedPayload.value(
        QStringLiteral("definedArea"));
    if (!expectedDefinitions.isArray()) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: definedArea is missing");
        return false;
    }
    for (const QJsonValue &value : expectedDefinitions.toArray()) {
        const QJsonObject area = value.toObject();
        const int index = area.value(QStringLiteral("index")).toInt(-1);
        if (index < 0 || expectedAreas.contains(index)) {
            errorMessage = QStringLiteral(
                "Camera verification mismatch: invalid or duplicate expected area index");
            return false;
        }
        expectedAreas.insert(index, area);
    }

    QHash<int, const IvaAreaDefinition *> actualAreas;
    for (const IvaAreaDefinition &area : configuration.areas) {
        if (area.channel == channel) {
            if (actualAreas.contains(area.areaIndex)) {
                errorMessage = QStringLiteral(
                    "Camera verification mismatch: duplicate actual area index %1")
                                   .arg(area.areaIndex);
                return false;
            }
            actualAreas.insert(area.areaIndex, &area);
        }
    }

    if (expectedAreas.size() != actualAreas.size()) {
        errorMessage = QStringLiteral(
            "Camera verification mismatch: area count expected %1, actual %2")
                           .arg(expectedAreas.size())
                           .arg(actualAreas.size());
        return false;
    }

    for (auto it = expectedAreas.cbegin(); it != expectedAreas.cend(); ++it) {
        const auto actual = actualAreas.constFind(it.key());
        if (actual == actualAreas.cend()) {
            errorMessage = QStringLiteral(
                "Camera verification mismatch: area index %1 is missing")
                               .arg(it.key());
            return false;
        }
        if (!compareArea(it.value(), *actual.value(), errorMessage)) return false;
    }

    errorMessage.clear();
    return true;
}
