#include "models/slotidmapper.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

class SlotIdMapperTest : public QObject
{
    Q_OBJECT

private:
    QString writeTempJson(QTemporaryDir &dir, const QString &name,
                          const QByteArray &content)
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        QVERIFY2_RETURN(file.open(QIODevice::WriteOnly), "Cannot create temp file", {});
        file.write(content);
        file.close();
        return path;
    }

    // Helper that provides a return value so QVERIFY2 can be used outside
    // test methods when the helper itself needs to return a value.
    static const char *QVERIFY2_RETURN(bool condition, const char *message,
                                        const QString &fallback)
    {
        Q_UNUSED(fallback);
        if (!condition) qFatal("%s", message);
        return nullptr;
    }

private slots:
    void loadValidMapping()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        QJsonArray mappings;
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_01"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
            mappings.append(m);
        }
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_02"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("P-01"));
            mappings.append(m);
        }
        root.insert(QStringLiteral("mappings"), mappings);
        const QByteArray json = QJsonDocument(root).toJson();
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(json);
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY2(mapper.loadFromFile(path, error), qPrintable(error));
        QCOMPARE(mapper.mappingCount(), 2);

        // Forward lookup
        QCOMPARE(mapper.toZoneId(QStringLiteral("slot_01")), QStringLiteral("EV-01"));
        QCOMPARE(mapper.toZoneId(QStringLiteral("slot_02")), QStringLiteral("P-01"));

        // Reverse lookup
        QCOMPARE(mapper.toServerSlotId(QStringLiteral("EV-01")), QStringLiteral("slot_01"));
        QCOMPARE(mapper.toServerSlotId(QStringLiteral("P-01")), QStringLiteral("slot_02"));
    }

    void caseInsensitiveLookup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        QJsonArray mappings;
        QJsonObject m;
        m.insert(QStringLiteral("server_slot_id"), QStringLiteral("Slot_03"));
        m.insert(QStringLiteral("zone_id"), QStringLiteral("ev-05"));
        mappings.append(m);
        root.insert(QStringLiteral("mappings"), mappings);
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(mapper.loadFromFile(path, error));
        QCOMPARE(mapper.toZoneId(QStringLiteral("SLOT_03")), QStringLiteral("EV-05"));
        QCOMPARE(mapper.toZoneId(QStringLiteral("slot_03")), QStringLiteral("EV-05"));
        QCOMPARE(mapper.toServerSlotId(QStringLiteral("EV-05")), QStringLiteral("Slot_03"));
    }

    void passthroughMode()
    {
        SlotIdMapper mapper;
        // No mappings loaded, passthrough on by default
        QVERIFY(mapper.isPassthroughEnabled());
        QCOMPARE(mapper.toZoneId(QStringLiteral("EV-01")), QStringLiteral("EV-01"));
        QCOMPARE(mapper.toZoneId(QStringLiteral("P-03")), QStringLiteral("P-03"));
        QCOMPARE(mapper.toServerSlotId(QStringLiteral("EV-01")), QStringLiteral("EV-01"));
    }

    void passthroughDisabled()
    {
        SlotIdMapper mapper;
        mapper.setPassthroughEnabled(false);
        QVERIFY(mapper.toZoneId(QStringLiteral("unknown_id")).isEmpty());
        QVERIFY(mapper.toServerSlotId(QStringLiteral("unknown_id")).isEmpty());
    }

    void duplicateServerSlotId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        QJsonArray mappings;
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_01"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
            mappings.append(m);
        }
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_01"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-02"));
            mappings.append(m);
        }
        root.insert(QStringLiteral("mappings"), mappings);
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(!mapper.loadFromFile(path, error));
        QVERIFY(error.contains(QStringLiteral("Duplicate server_slot_id")));
    }

    void duplicateZoneId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        QJsonArray mappings;
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_01"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
            mappings.append(m);
        }
        {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_02"));
            m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
            mappings.append(m);
        }
        root.insert(QStringLiteral("mappings"), mappings);
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(!mapper.loadFromFile(path, error));
        QVERIFY(error.contains(QStringLiteral("Duplicate zone_id")));
    }

    void missingFile()
    {
        SlotIdMapper mapper;
        QString error;
        QVERIFY(!mapper.loadFromFile(QStringLiteral("/nonexistent/path.json"), error));
        QVERIFY(error.contains(QStringLiteral("Cannot open")));
    }

    void invalidJson()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bad.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not valid json }}}");
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(!mapper.loadFromFile(path, error));
        QVERIFY(error.contains(QStringLiteral("parse error")));
    }

    void emptyMappingsArray()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        root.insert(QStringLiteral("mappings"), QJsonArray());
        const QString path = dir.filePath(QStringLiteral("empty.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(!mapper.loadFromFile(path, error));
        QVERIFY(error.contains(QStringLiteral("empty")));
    }

    void clearMappings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        QJsonArray mappings;
        QJsonObject m;
        m.insert(QStringLiteral("server_slot_id"), QStringLiteral("slot_01"));
        m.insert(QStringLiteral("zone_id"), QStringLiteral("EV-01"));
        mappings.append(m);
        root.insert(QStringLiteral("mappings"), mappings);
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(mapper.loadFromFile(path, error));
        QCOMPARE(mapper.mappingCount(), 1);
        mapper.clear();
        QCOMPARE(mapper.mappingCount(), 0);
        // Passthrough still works after clear
        QCOMPARE(mapper.toZoneId(QStringLiteral("EV-01")), QStringLiteral("EV-01"));
    }

    void allMappingsReturnsCorrectPairs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        QJsonArray mappings;
        for (int i = 1; i <= 3; ++i) {
            QJsonObject m;
            m.insert(QStringLiteral("server_slot_id"),
                     QStringLiteral("slot_%1").arg(i, 2, 10, QLatin1Char('0')));
            m.insert(QStringLiteral("zone_id"),
                     QStringLiteral("EV-%1").arg(i, 2, 10, QLatin1Char('0')));
            mappings.append(m);
        }
        root.insert(QStringLiteral("mappings"), mappings);
        const QString path = dir.filePath(QStringLiteral("mapping.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        SlotIdMapper mapper;
        QString error;
        QVERIFY(mapper.loadFromFile(path, error));
        const auto all = mapper.allMappings();
        QCOMPARE(all.size(), 3);
        // Each pair is server->zone
        for (const auto &pair : all) {
            QVERIFY(pair.first.startsWith(QStringLiteral("slot_")));
            QVERIFY(pair.second.startsWith(QStringLiteral("EV-")));
        }
    }
};

QTEST_GUILESS_MAIN(SlotIdMapperTest)
#include "slotidmapper_test.moc"
