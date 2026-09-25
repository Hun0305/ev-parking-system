#include "ivaareacapabilityparser.h"

#include <QJsonArray>

namespace {
bool parsePositiveInteger(const QJsonValue &value, int &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    const int integer = value.toInt(-1);
    if (number != static_cast<double>(integer) || integer <= 0) return false;
    result = integer;
    return true;
}

bool parseChannel(const QJsonValue &value, int &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    const int integer = value.toInt(-1);
    if (number != static_cast<double>(integer) || integer < 0) return false;
    result = integer;
    return true;
}
}

bool IvaAreaCapabilityParser::parse(const QJsonDocument &document,
                                    WiseAiCapabilities &capabilities,
                                    QString &errorMessage)
{
    if (!document.isObject()) {
        errorMessage = QStringLiteral("WiseAI capability must be a JSON object");
        return false;
    }
    const QJsonValue arrayValue = document.object().value(
        QStringLiteral("capabilities"));
    if (!arrayValue.isArray()) {
        errorMessage = QStringLiteral("WiseAI capability is missing capabilities");
        return false;
    }

    WiseAiCapabilities parsed;
    parsed.rawDocument = document;
    const QJsonArray array = arrayValue.toArray();
    for (int index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            errorMessage = QStringLiteral("capabilities item %1 must be an object")
                               .arg(index);
            return false;
        }
        const QJsonObject object = array.at(index).toObject();
        IvaChannelCapability capability;
        if (!parseChannel(object.value(QStringLiteral("channel")),
                          capability.channel)) {
            errorMessage = QStringLiteral("capabilities item %1 has an invalid channel")
                               .arg(index);
            return false;
        }
        const QJsonValue ivaValue = object.value(QStringLiteral("ivaarea"));
        if (!ivaValue.isBool()) {
            errorMessage = QStringLiteral("capabilities item %1 is missing ivaarea")
                               .arg(index);
            return false;
        }
        capability.ivaAreaSupported = ivaValue.toBool();
        const QJsonValue resolutionValue = object.value(
            QStringLiteral("maxResolution"));
        if (!resolutionValue.isObject()) {
            errorMessage = QStringLiteral(
                "capabilities item %1 is missing maxResolution")
                               .arg(index);
            return false;
        }
        int width = -1;
        int height = -1;
        const QJsonObject resolution = resolutionValue.toObject();
        if (!parsePositiveInteger(resolution.value(QStringLiteral("width")), width)
            || !parsePositiveInteger(resolution.value(QStringLiteral("height")),
                                     height)) {
            errorMessage = QStringLiteral(
                "capabilities item %1 has an invalid maxResolution")
                               .arg(index);
            return false;
        }
        capability.maxResolution = QSize(width, height);
        capability.rawCapability = object;
        parsed.channels.append(capability);
    }

    capabilities = parsed;
    errorMessage.clear();
    return true;
}
