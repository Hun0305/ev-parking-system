#include "slotidmapper.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {
QString normalizeId(const QString &id)
{
    return id.trimmed().toUpper();
}
}

bool SlotIdMapper::loadFromFile(const QString &path, QString &errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = QStringLiteral("Cannot open slot mapping file: %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        errorMessage = QStringLiteral("Slot mapping JSON parse error: %1")
                           .arg(parseError.errorString());
        return false;
    }

    if (!document.isObject()) {
        errorMessage = QStringLiteral("Slot mapping file must contain a JSON object");
        return false;
    }

    const QJsonArray mappings = document.object()
                                    .value(QStringLiteral("mappings"))
                                    .toArray();
    if (mappings.isEmpty()) {
        errorMessage = QStringLiteral("Slot mapping file has no \"mappings\" array or it is empty");
        return false;
    }

    QHash<QString, QString> serverToZone;
    QHash<QString, QString> zoneToServer;

    for (int i = 0; i < mappings.size(); ++i) {
        const QJsonValue entry = mappings.at(i);
        if (!entry.isObject()) {
            errorMessage = QStringLiteral("Slot mapping entry %1 is not an object").arg(i);
            return false;
        }
        const QJsonObject obj = entry.toObject();
        const QString serverSlotId =
            obj.value(QStringLiteral("server_slot_id")).toString().trimmed();
        const QString zoneId =
            obj.value(QStringLiteral("zone_id")).toString().trimmed().toUpper();
        const QString serverKey = normalizeId(serverSlotId);
        const QString zoneKey = normalizeId(zoneId);

        if (serverSlotId.isEmpty() || zoneId.isEmpty()) {
            errorMessage = QStringLiteral(
                "Slot mapping entry %1 is missing server_slot_id or zone_id").arg(i);
            return false;
        }
        if (serverToZone.contains(serverKey)) {
            errorMessage = QStringLiteral(
                "Duplicate server_slot_id \"%1\" in mapping entry %2")
                               .arg(serverSlotId)
                               .arg(i);
            return false;
        }
        if (zoneToServer.contains(zoneKey)) {
            errorMessage = QStringLiteral(
                "Duplicate zone_id \"%1\" in mapping entry %2")
                               .arg(zoneId)
                               .arg(i);
            return false;
        }
        serverToZone.insert(serverKey, zoneId);
        zoneToServer.insert(zoneKey, serverSlotId);
    }

    m_serverToZone = serverToZone;
    m_zoneToServer = zoneToServer;
    errorMessage.clear();
    return true;
}

QString SlotIdMapper::toZoneId(const QString &serverSlotId) const
{
    const QString normalized = normalizeId(serverSlotId);
    const auto it = m_serverToZone.constFind(normalized);
    if (it != m_serverToZone.constEnd()) {
        return it.value();
    }
    return m_passthroughEnabled ? normalized : QString();
}

QString SlotIdMapper::toServerSlotId(const QString &zoneId) const
{
    const QString normalized = normalizeId(zoneId);
    const auto it = m_zoneToServer.constFind(normalized);
    if (it != m_zoneToServer.constEnd()) {
        return it.value();
    }
    return m_passthroughEnabled ? normalized : QString();
}

void SlotIdMapper::setPassthroughEnabled(bool enabled)
{
    m_passthroughEnabled = enabled;
}

bool SlotIdMapper::isPassthroughEnabled() const
{
    return m_passthroughEnabled;
}

int SlotIdMapper::mappingCount() const
{
    return m_serverToZone.size();
}

QList<QPair<QString, QString>> SlotIdMapper::allMappings() const
{
    QList<QPair<QString, QString>> result;
    result.reserve(m_zoneToServer.size());
    for (auto it = m_zoneToServer.constBegin(); it != m_zoneToServer.constEnd(); ++it) {
        result.append({it.value(), it.key()});
    }
    return result;
}

void SlotIdMapper::clear()
{
    m_serverToZone.clear();
    m_zoneToServer.clear();
}
