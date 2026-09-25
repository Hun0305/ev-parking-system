#include "services/mqttserviceclient.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSettings>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

namespace {
int decodeRemainingLength(const QByteArray &buffer, int startOffset,
                          quint32 &value)
{
    quint32 multiplier = 1;
    value = 0;
    int pos = startOffset;
    for (int i = 0; i < 4; ++i) {
        if (pos >= buffer.size()) return -1;
        const quint8 byte = static_cast<quint8>(buffer.at(pos++));
        value += (byte & 0x7F) * multiplier;
        if ((byte & 0x80) == 0) return pos - startOffset;
        multiplier *= 128;
    }
    return -1;
}

QByteArray subAckPacket(quint16 packetId, quint8 grantedQos)
{
    QByteArray packet;
    packet.append(static_cast<char>(0x90));
    packet.append(static_cast<char>(0x03));
    packet.append(static_cast<char>((packetId >> 8) & 0xFF));
    packet.append(static_cast<char>(packetId & 0xFF));
    packet.append(static_cast<char>(grantedQos));
    return packet;
}

QByteArray pubAckPacket(quint16 packetId)
{
    QByteArray packet;
    packet.append(static_cast<char>(0x40));
    packet.append(static_cast<char>(0x02));
    packet.append(static_cast<char>((packetId >> 8) & 0xFF));
    packet.append(static_cast<char>(packetId & 0xFF));
    return packet;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("settings")) {
        QTemporaryDir directory;
        if (!directory.isValid()) return 10;

        const QString path = directory.filePath(QStringLiteral("mqtt.ini"));
        const QStringList expectedTopics{
            QStringLiteral("parking/fire/+"),
            QStringLiteral("parking/v1/events/+"),
            QStringLiteral("parking/v1/state/+")};
        QSettings writer(path, QSettings::IniFormat);
        writer.setValue(QStringLiteral("mqtt/topics"), expectedTopics);
        writer.sync();

        QSettings reader(path, QSettings::IniFormat);
        const QStringList qSettingsList = MqttSettings::normalizedTopics(
            reader.value(QStringLiteral("mqtt/topics")));
        const QStringList commaSeparated = MqttSettings::normalizedTopics(
            QStringLiteral(" parking/fire/+ , parking/v1/events/+ , "
                           "parking/v1/state/+ , parking/fire/+ "));
        const QStringList empty = MqttSettings::normalizedTopics(QString());
        return qSettingsList == expectedTopics
                && commaSeparated == expectedTopics && empty.isEmpty()
            ? 0 : 11;
    }

    const bool rejectSubscriptions =
        app.arguments().value(1) == QStringLiteral("rejected");

    QTcpServer broker;
    if (!broker.listen(QHostAddress::LocalHost, 0)) return 2;

    const QString expectedClientId = QStringLiteral("qt-client-test01");
    const QStringList expectedTopics = rejectSubscriptions
        ? QStringList{QStringLiteral("parking/v1/state/+")}
        : QStringList{QStringLiteral("parking/fire/+"),
                      QStringLiteral("parking/v1/events/+"),
                      QStringLiteral("parking/v1/state/+")};

    MqttSettings settings;
    settings.enabled = true;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = broker.serverPort();
    settings.clientId = expectedClientId;
    settings.topics = expectedTopics;
    settings.reconnectIntervalMs = 10000;

    MqttServiceClient client(settings);
    QStringList statuses;
    QSet<QString> subscribedTopics;
    QByteArray brokerBuffer;
    bool clientIdMatched = false;
    bool subscriptionsReady = false;
    bool publishRequested = false;
    QString publishedTopic;
    QByteArray publishedPayload;
    int publishPacketCount = 0;
    int exitCode = 1;

    QObject::connect(&broker, &QTcpServer::newConnection, &app, [&]() {
        QTcpSocket *socket = broker.nextPendingConnection();
        if (!socket) {
            exitCode = 3;
            app.quit();
            return;
        }

        QObject::connect(socket, &QTcpSocket::readyRead, &app,
                         [&, socket]() {
            brokerBuffer.append(socket->readAll());
            while (true) {
                if (brokerBuffer.size() < 2) return;
                quint32 remainingLength = 0;
                const int lengthBytes = decodeRemainingLength(
                    brokerBuffer, 1, remainingLength);
                if (lengthBytes < 0) return;
                const int headerSize = 1 + lengthBytes;
                if (brokerBuffer.size()
                    < headerSize + static_cast<int>(remainingLength)) {
                    return;
                }

                const quint8 fixedHeader =
                    static_cast<quint8>(brokerBuffer.at(0));
                const quint8 packetType = fixedHeader >> 4;
                const QByteArray body = brokerBuffer.mid(
                    headerSize, static_cast<int>(remainingLength));
                brokerBuffer.remove(
                    0, headerSize + static_cast<int>(remainingLength));

                if (packetType == 1) {
                    if (body.size() < 12) {
                        exitCode = 4;
                        app.quit();
                        return;
                    }
                    const quint16 clientIdLength =
                        (static_cast<quint8>(body.at(10)) << 8)
                        | static_cast<quint8>(body.at(11));
                    clientIdMatched =
                        QString::fromUtf8(body.mid(12, clientIdLength))
                        == expectedClientId;
                    socket->write(QByteArray::fromHex("20020000"));
                    continue;
                }

                if (packetType == 3) {
                    ++publishPacketCount;
                    if (fixedHeader != 0x32 || body.size() < 4) {
                        exitCode = 5;
                        app.quit();
                        return;
                    }
                    const quint16 topicLength =
                        (static_cast<quint8>(body.at(0)) << 8)
                        | static_cast<quint8>(body.at(1));
                    if (body.size() < 4 + topicLength) {
                        exitCode = 6;
                        app.quit();
                        return;
                    }
                    publishedTopic = QString::fromUtf8(
                        body.mid(2, topicLength));
                    const int packetIdOffset = 2 + topicLength;
                    const quint16 packetId =
                        (static_cast<quint8>(body.at(packetIdOffset)) << 8)
                        | static_cast<quint8>(body.at(packetIdOffset + 1));
                    publishedPayload = body.mid(packetIdOffset + 2);
                    socket->write(pubAckPacket(packetId));
                    continue;
                }

                if (packetType != 8 || body.size() < 5) {
                    exitCode = 5;
                    app.quit();
                    return;
                }
                const quint16 packetId =
                    (static_cast<quint8>(body.at(0)) << 8)
                    | static_cast<quint8>(body.at(1));
                const quint16 topicLength =
                    (static_cast<quint8>(body.at(2)) << 8)
                    | static_cast<quint8>(body.at(3));
                if (body.size() < 5 + topicLength) {
                    exitCode = 6;
                    app.quit();
                    return;
                }
                subscribedTopics.insert(
                    QString::fromUtf8(body.mid(4, topicLength)));
                socket->write(subAckPacket(
                    packetId, rejectSubscriptions ? 0x80 : 1));
            }
        });
    });

    QObject::connect(&client, &MqttServiceClient::connectionChanged,
                     &app, [&](const QString &status, bool connected) {
        statuses.append(status);
        if (rejectSubscriptions) {
            if (!connected
                && status == QStringLiteral(
                    "MQTT subscription rejected: parking/v1/state/+")) {
                exitCode = clientIdMatched ? 0 : 9;
                app.quit();
            }
            return;
        }
        if (!connected
            || !status.startsWith(QStringLiteral("MQTT ready: 3/3"))) {
            return;
        }

        const bool subscribedStatusPresent =
            std::all_of(expectedTopics.cbegin(), expectedTopics.cend(),
                        [&](const QString &topic) {
            return statuses.contains(
                QStringLiteral("MQTT subscribed: %1 (QoS 1)").arg(topic));
        });
        subscriptionsReady = clientIdMatched
            && subscribedTopics == QSet<QString>(expectedTopics.cbegin(),
                                                  expectedTopics.cend())
            && subscribedStatusPresent
            && status.contains(expectedClientId);
        if (!subscriptionsReady || publishRequested) {
            if (!subscriptionsReady) {
                exitCode = 7;
                app.quit();
            }
            return;
        }
        publishRequested = true;
        const QByteArray command =
            R"JSON({"command":"ALARM_ACK","channel_id":"ch01","alarm_id":"alarm-01"})JSON";
        if (!client.publish(
                QStringLiteral("parking/v1/commands/fire/ch01"), command)) {
            exitCode = 10;
            app.quit();
        }
    });

    QObject::connect(&client, &MqttServiceClient::publishConfirmed,
                     &app, [&](const QString &topic) {
        const QByteArray expectedPayload =
            R"JSON({"command":"ALARM_ACK","channel_id":"ch01","alarm_id":"alarm-01"})JSON";
        exitCode = subscriptionsReady
                && topic == QStringLiteral("parking/v1/commands/fire/ch01")
                && publishedTopic == topic
                && publishedPayload == expectedPayload
                && publishPacketCount == 1
            ? 0 : 11;
        app.quit();
    });

    QTimer::singleShot(5000, &app, [&]() {
        exitCode = 8;
        app.quit();
    });
    client.start();
    app.exec();
    client.stop();
    return exitCode;
}
