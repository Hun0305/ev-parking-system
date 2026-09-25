#include "mqttserviceclient.h"

#include <QTcpSocket>
#include <QTimer>

namespace {

// 우리가 요청하는 QoS. 1이면 브로커가 PUBLISH 에 packet id 를 붙여 보내고,
// 우리는 PUBACK 으로 응답해야 브로커가 재전송을 멈춘다 (재연결 구간에서
// 화재 알림을 조용히 잃지 않기 위함). QoS2 는 이 프로젝트에서 쓰지 않는다.
constexpr quint8 kRequestedQos = 1;
constexpr int kKeepAliveSeconds = 60;

QByteArray encodeRemainingLength(quint32 length)
{
    QByteArray out;
    do {
        quint8 digit = static_cast<quint8>(length % 128);
        length /= 128;
        if (length > 0) digit |= 0x80;
        out.append(static_cast<char>(digit));
    } while (length > 0);
    return out;
}

// 반환값 -1 이면 아직 버퍼에 다 안 들어온 것(더 읽어야 함).
// 그 외엔 소비한 바이트 수를 돌려주고 value 에 디코딩된 길이를 채운다.
int decodeRemainingLength(const QByteArray &buffer, int startOffset, quint32 *value)
{
    quint32 multiplier = 1;
    quint32 result = 0;
    int pos = startOffset;
    for (int i = 0; i < 4; ++i) {
        if (pos >= buffer.size()) return -1;
        const quint8 byte = static_cast<quint8>(buffer.at(pos));
        ++pos;
        result += (byte & 0x7F) * multiplier;
        if ((byte & 0x80) == 0) {
            *value = result;
            return pos - startOffset;
        }
        multiplier *= 128;
    }
    return -1;  // malformed (4바이트를 넘김) — 상위에서 버퍼 리셋으로 방어
}

QByteArray encodeUtf8String(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    const quint16 len = static_cast<quint16>(utf8.size());
    QByteArray out;
    out.append(static_cast<char>((len >> 8) & 0xFF));
    out.append(static_cast<char>(len & 0xFF));
    out.append(utf8);
    return out;
}

QByteArray buildConnectPacket(const QString &clientId, int keepAliveSeconds)
{
    QByteArray variableHeader;
    variableHeader.append(encodeUtf8String(QStringLiteral("MQTT")));
    variableHeader.append(static_cast<char>(0x04));  // protocol level: 3.1.1
    variableHeader.append(static_cast<char>(0x02));  // connect flags: clean session, 인증/Will 없음
    variableHeader.append(static_cast<char>((keepAliveSeconds >> 8) & 0xFF));
    variableHeader.append(static_cast<char>(keepAliveSeconds & 0xFF));

    const QByteArray payload = encodeUtf8String(clientId);
    const QByteArray remaining = variableHeader + payload;

    QByteArray packet;
    packet.append(static_cast<char>(0x10));  // CONNECT
    packet.append(encodeRemainingLength(static_cast<quint32>(remaining.size())));
    packet.append(remaining);
    return packet;
}

QByteArray buildSubscribePacket(quint16 packetId, const QString &topicFilter, quint8 qos)
{
    QByteArray variableHeader;
    variableHeader.append(static_cast<char>((packetId >> 8) & 0xFF));
    variableHeader.append(static_cast<char>(packetId & 0xFF));

    QByteArray payload = encodeUtf8String(topicFilter);
    payload.append(static_cast<char>(qos & 0x03));

    const QByteArray remaining = variableHeader + payload;

    QByteArray packet;
    packet.append(static_cast<char>(0x82));  // SUBSCRIBE, 스펙상 flags 는 0010 고정
    packet.append(encodeRemainingLength(static_cast<quint32>(remaining.size())));
    packet.append(remaining);
    return packet;
}

QByteArray buildPubAckPacket(quint16 packetId)
{
    QByteArray packet;
    packet.append(static_cast<char>(0x40));  // PUBACK
    packet.append(static_cast<char>(0x02));  // remaining length 는 항상 2
    packet.append(static_cast<char>((packetId >> 8) & 0xFF));
    packet.append(static_cast<char>(packetId & 0xFF));
    return packet;
}

QByteArray buildPublishPacket(quint16 packetId, const QString &topic,
                              const QByteArray &payload)
{
    QByteArray variableHeader = encodeUtf8String(topic);
    variableHeader.append(static_cast<char>((packetId >> 8) & 0xFF));
    variableHeader.append(static_cast<char>(packetId & 0xFF));
    const QByteArray remaining = variableHeader + payload;

    QByteArray packet;
    packet.append(static_cast<char>(0x32));  // PUBLISH, QoS 1, retain=false
    packet.append(encodeRemainingLength(static_cast<quint32>(remaining.size())));
    packet.append(remaining);
    return packet;
}

QByteArray buildPingReqPacket()
{
    QByteArray packet;
    packet.append(static_cast<char>(0xC0));
    packet.append(static_cast<char>(0x00));
    return packet;
}

}  // namespace

QStringList MqttSettings::normalizedTopics(const QVariant &value)
{
    QStringList serializedValues = value.toStringList();
    if (serializedValues.isEmpty() && !value.toString().isEmpty()) {
        serializedValues.append(value.toString());
    }

    QStringList topics;
    for (const QString &serializedValue : serializedValues) {
        const QStringList candidates =
            serializedValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &candidate : candidates) {
            const QString topic = candidate.trimmed();
            if (!topic.isEmpty() && !topics.contains(topic)) {
                topics.append(topic);
            }
        }
    }
    return topics;
}

MqttServiceClient::MqttServiceClient(const MqttSettings &settings,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttServiceClient::start);
}

void MqttServiceClient::start()
{
    if (!m_settings.enabled) {
        emit connectionChanged(QStringLiteral("MQTT disabled"), false);
        return;
    }
    if (m_settings.host.isEmpty()) {
        emit connectionChanged(QStringLiteral("MQTT host is not configured"), false);
        return;
    }

    if (!m_socket) {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &MqttServiceClient::onSocketConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &MqttServiceClient::onSocketDisconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &MqttServiceClient::onReadyRead);
        connect(m_socket, &QTcpSocket::errorOccurred, this, &MqttServiceClient::onSocketError);
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;  // 이미 연결 중이거나 연결됨
    }

    m_readBuffer.clear();
    emit connectionChanged(
        QStringLiteral("MQTT connecting: %1:%2").arg(m_settings.host).arg(m_settings.port),
        false);
    m_socket->connectToHost(m_settings.host, m_settings.port);
}

void MqttServiceClient::onSocketConnected()
{
    const QString clientId =
        m_settings.clientId.isEmpty() ? QStringLiteral("qt-client") : m_settings.clientId;
    m_socket->write(buildConnectPacket(clientId, kKeepAliveSeconds));
}

void MqttServiceClient::onSocketDisconnected()
{
    if (m_keepAliveTimer) m_keepAliveTimer->stop();
    m_sessionReady = false;
    m_pendingSubscriptions.clear();
    if (!m_pendingPublishes.isEmpty()) {
        const QStringList topics = m_pendingPublishes.values();
        m_pendingPublishes.clear();
        for (const QString &topic : topics) {
            emit publishFailed(topic, QStringLiteral("MQTT disconnected before PUBACK"));
        }
    }
    m_expectedSubscriptionCount = 0;
    m_activeSubscriptionCount = 0;
    emit connectionChanged(QStringLiteral("MQTT disconnected"), false);
    scheduleReconnect();
}

void MqttServiceClient::onSocketError()
{
    emit connectionChanged(
        QStringLiteral("MQTT error: %1").arg(m_socket->errorString()), false);
    scheduleReconnect();
}

void MqttServiceClient::onReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    while (true) {
        if (m_readBuffer.size() < 2) return;  // 최소 fixed header 1B + length 1B

        quint32 remaining = 0;
        const int lengthBytes = decodeRemainingLength(m_readBuffer, 1, &remaining);
        if (lengthBytes < 0) {
            if (m_readBuffer.size() > 5) {
                // 4바이트 넘겨도 길이가 안 끝남 = 프레이밍이 깨진 것.
                // 계속 잘못 읽히는 걸 막기 위해 재연결로 복구한다.
                m_socket->abort();
            }
            return;  // 더 받아야 판단 가능
        }

        const int headerSize = 1 + lengthBytes;
        if (static_cast<quint32>(m_readBuffer.size()) < static_cast<quint32>(headerSize) + remaining) {
            return;  // 본문이 아직 다 안 옴
        }

        const quint8 fixedHeaderByte = static_cast<quint8>(m_readBuffer.at(0));
        const QByteArray body = m_readBuffer.mid(headerSize, static_cast<int>(remaining));
        m_readBuffer.remove(0, headerSize + static_cast<int>(remaining));

        handlePacket(fixedHeaderByte, body);
        // 한 번의 TCP 수신에 패킷이 여러 개 붙어 올 수 있어 루프를 돈다.
    }
}

void MqttServiceClient::handlePacket(quint8 fixedHeaderByte, const QByteArray &body)
{
    const quint8 packetType = fixedHeaderByte >> 4;
    switch (packetType) {
    case 2:  // CONNACK
        handleConnAck(body);
        break;
    case 3:  // PUBLISH
        handlePublish(fixedHeaderByte, body);
        break;
    case 4:  // PUBACK
        handlePubAck(body);
        break;
    case 9:   // SUBACK
        handleSubAck(body);
        break;
    case 13:  // PINGRESP
    default:
        break;  // 우리 흐름에선 내용을 더 볼 필요가 없다
    }
}

void MqttServiceClient::handleConnAck(const QByteArray &body)
{
    if (body.size() < 2) {
        m_socket->abort();
        return;
    }

    const quint8 returnCode = static_cast<quint8>(body.at(1));
    if (returnCode != 0) {
        emit connectionChanged(
            QStringLiteral("MQTT CONNACK rejected: code %1").arg(returnCode), false);
        m_socket->abort();
        return;
    }

    m_sessionReady = true;

    emit connectionChanged(
        QStringLiteral("MQTT connected: %1:%2 | client_id=%3")
            .arg(m_settings.host).arg(m_settings.port).arg(m_settings.clientId),
        true);
    subscribeAll();
    startKeepAlive();
}

void MqttServiceClient::handlePubAck(const QByteArray &body)
{
    if (body.size() != 2) return;
    const quint16 packetId =
        (static_cast<quint8>(body.at(0)) << 8)
        | static_cast<quint8>(body.at(1));
    const auto pending = m_pendingPublishes.find(packetId);
    if (pending == m_pendingPublishes.end()) return;
    const QString topic = pending.value();
    m_pendingPublishes.erase(pending);
    emit publishConfirmed(topic);
}

void MqttServiceClient::handleSubAck(const QByteArray &body)
{
    if (body.size() != 3) {
        emit connectionChanged(QStringLiteral("MQTT SUBACK malformed"), false);
        m_socket->abort();
        return;
    }

    const quint16 packetId =
        (static_cast<quint8>(body.at(0)) << 8)
        | static_cast<quint8>(body.at(1));
    const auto pending = m_pendingSubscriptions.find(packetId);
    if (pending == m_pendingSubscriptions.end()) {
        emit connectionChanged(
            QStringLiteral("MQTT SUBACK has unknown packet id: %1").arg(packetId),
            false);
        return;
    }

    const QString topic = pending.value();
    m_pendingSubscriptions.erase(pending);
    const quint8 grantedQos = static_cast<quint8>(body.at(2));
    if (grantedQos == 0x80) {
        emit connectionChanged(
            QStringLiteral("MQTT subscription rejected: %1").arg(topic), false);
        m_socket->abort();
        return;
    }
    if (grantedQos > 2) {
        emit connectionChanged(
            QStringLiteral("MQTT SUBACK invalid QoS %1: %2")
                .arg(grantedQos).arg(topic),
            false);
        m_socket->abort();
        return;
    }

    ++m_activeSubscriptionCount;
    emit connectionChanged(
        QStringLiteral("MQTT subscribed: %1 (QoS %2)")
            .arg(topic).arg(grantedQos),
        true);
    if (m_pendingSubscriptions.isEmpty()
        && m_activeSubscriptionCount == m_expectedSubscriptionCount) {
        emit connectionChanged(
            QStringLiteral("MQTT ready: %1/%2 subscriptions | client_id=%3")
                .arg(m_activeSubscriptionCount)
                .arg(m_expectedSubscriptionCount)
                .arg(m_settings.clientId),
            true);
    }
}

void MqttServiceClient::handlePublish(quint8 fixedHeaderByte, const QByteArray &body)
{
    const quint8 qos = (fixedHeaderByte >> 1) & 0x03;
    const bool retained = (fixedHeaderByte & 0x01) != 0;

    if (body.size() < 2) return;
    const quint16 topicLen = (static_cast<quint8>(body.at(0)) << 8) |
                              static_cast<quint8>(body.at(1));
    int pos = 2;
    if (body.size() < pos + topicLen) return;
    const QString topic = QString::fromUtf8(body.mid(pos, topicLen));
    pos += topicLen;

    quint16 packetId = 0;
    if (qos > 0) {
        if (body.size() < pos + 2) return;
        packetId = static_cast<quint16>((static_cast<quint8>(body.at(pos)) << 8) |
                                        static_cast<quint8>(body.at(pos + 1)));
        pos += 2;
    }

    const QByteArray payload = body.mid(pos);

    if (qos == 1) {
        m_socket->write(buildPubAckPacket(packetId));
    }

    emit messageReceived(topic, payload, retained);
}

void MqttServiceClient::subscribeAll()
{
    m_pendingSubscriptions.clear();
    m_expectedSubscriptionCount = 0;
    m_activeSubscriptionCount = 0;
    for (const QString &topic : m_settings.topics) {
        const QString trimmed = topic.trimmed();
        if (trimmed.isEmpty()) continue;

        const quint16 packetId = m_nextPacketId++;
        if (m_nextPacketId == 0) m_nextPacketId = 1;  // 0은 예약값, wraparound 방지

        m_pendingSubscriptions.insert(packetId, trimmed);
        ++m_expectedSubscriptionCount;
        m_socket->write(buildSubscribePacket(packetId, trimmed, kRequestedQos));
        emit connectionChanged(QStringLiteral("MQTT subscribing: %1").arg(trimmed), true);
    }
    if (m_expectedSubscriptionCount == 0) {
        emit connectionChanged(
            QStringLiteral("MQTT connected but no subscriptions are configured"),
            false);
    }
}

void MqttServiceClient::startKeepAlive()
{
    if (!m_keepAliveTimer) {
        m_keepAliveTimer = new QTimer(this);
        connect(m_keepAliveTimer, &QTimer::timeout, this, [this]() {
            if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
                m_socket->write(buildPingReqPacket());
            }
        });
    }
    // 브로커가 1.5배 keep-alive 안에 응답이 없으면 끊는다는 스펙 여유를 두고
    // 그보다 짧은 주기로 PINGREQ 를 보낸다.
    m_keepAliveTimer->start(kKeepAliveSeconds * 1000 * 3 / 4);
}

void MqttServiceClient::stop()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
    if (m_keepAliveTimer) m_keepAliveTimer->stop();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
    m_sessionReady = false;
}

bool MqttServiceClient::publish(const QString &topic, const QByteArray &payload)
{
    const QString trimmedTopic = topic.trimmed();
    if (trimmedTopic.isEmpty()
        || trimmedTopic.contains(QLatin1Char('+'))
        || trimmedTopic.contains(QLatin1Char('#'))) {
        emit publishFailed(trimmedTopic, QStringLiteral("Invalid MQTT publish topic"));
        return false;
    }
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState
        || !m_sessionReady) {
        emit publishFailed(trimmedTopic, QStringLiteral("MQTT is not ready"));
        return false;
    }

    const quint16 packetId = m_nextPacketId++;
    if (m_nextPacketId == 0) m_nextPacketId = 1;
    m_pendingPublishes.insert(packetId, trimmedTopic);
    if (m_socket->write(buildPublishPacket(packetId, trimmedTopic, payload)) < 0) {
        m_pendingPublishes.remove(packetId);
        emit publishFailed(trimmedTopic, m_socket->errorString());
        return false;
    }
    return true;
}

void MqttServiceClient::scheduleReconnect()
{
    if (!m_settings.enabled || !m_reconnectTimer) return;
    if (m_reconnectTimer->isActive()) return;
    m_reconnectTimer->start(qMax(1000, m_settings.reconnectIntervalMs));
}
