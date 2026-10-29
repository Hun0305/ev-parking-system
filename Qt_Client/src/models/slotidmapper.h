#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

// Bidirectional mapper between Raspberry Pi server slot IDs (e.g. "slot_01")
// and Qt Parking Map zone IDs (e.g. "EV-01", "P-01").
//
// The mapping is loaded from a JSON configuration file so that the
// relationship is never hardcoded.  When no mapping file is found or a
// server ID has no entry, passthrough mode returns the original ID after
// normalization so that existing EV-01 / P-01 payloads keep working.
class SlotIdMapper
{
public:
    // Load mappings from a JSON file.  Returns false on parse or I/O error.
    bool loadFromFile(const QString &path, QString &errorMessage);

    // Server slot_id -> Qt zoneId.
    // Returns the mapped zoneId, or the normalized input if passthrough is on
    // and no mapping exists, or an empty string if passthrough is off.
    QString toZoneId(const QString &serverSlotId) const;

    // Qt zoneId -> server slot_id.
    // Returns the mapped server ID, or the normalized input if passthrough is
    // on and no mapping exists, or an empty string if passthrough is off.
    QString toServerSlotId(const QString &zoneId) const;

    // When true (default), IDs without a mapping entry are returned as-is
    // after trimming/uppercasing.  This preserves backward compatibility with
    // servers that already send EV-01 / P-01 directly.
    void setPassthroughEnabled(bool enabled);
    bool isPassthroughEnabled() const;

    int mappingCount() const;
    QList<QPair<QString, QString>> allMappings() const;

    // Clear all loaded mappings.
    void clear();

private:
    QHash<QString, QString> m_serverToZone;
    QHash<QString, QString> m_zoneToServer;
    bool m_passthroughEnabled = true;
};
