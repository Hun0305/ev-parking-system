#include "controllers/parkingcontroller.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

namespace {
QByteArray sessionImages(qint64 sessionId, qint64 imageId)
{
    return QJsonDocument::fromJson(QStringLiteral(R"JSON(
        {
          "session_id": %1,
          "items": [
            {
              "image_id": %2,
              "session_id": %1,
              "original_url": "/api/v1/images/%2/original",
              "captured_at": "2026-08-10T10:00:00+09:00"
            }
          ]
        }
    )JSON").arg(sessionId).arg(imageId).toUtf8()).toJson(QJsonDocument::Compact);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 1;

    int slotDetailRequests = 0;
    int session7Requests = 0;
    int session8Requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            auto *buffer = new QByteArray;
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [&, socket, buffer]() {
                buffer->append(socket->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                socket->disconnect(socket, nullptr, socket, nullptr);

                const QByteArray requestLine =
                    buffer->left(buffer->indexOf("\r\n"));
                int status = 200;
                QByteArray body;
                if (requestLine.startsWith("GET /slots ")) {
                    body = QByteArrayLiteral("{\"items\":[]}");
                } else if (requestLine.startsWith(
                               "GET /api/v1/parking-sessions/7/images ")) {
                    ++session7Requests;
                    body = sessionImages(7, 70);
                } else if (requestLine.startsWith(
                               "GET /api/v1/parking-slots/EV02 ")) {
                    ++slotDetailRequests;
                    body = QByteArrayLiteral(
                        "{\"slot_id\":\"EV02\",\"parking_status\":\"OCCUPIED\","
                        "\"active_session\":{\"session_id\":8,"
                        "\"plate_number\":\"34B7788\"}}");
                } else if (requestLine.startsWith(
                               "GET /api/v1/parking-sessions/8/images ")) {
                    ++session8Requests;
                    body = sessionImages(8, 80);
                } else {
                    status = 404;
                    body = QByteArrayLiteral("{\"error\":\"unexpected path\"}");
                }

                const QByteArray response = QByteArrayLiteral("HTTP/1.1 ")
                    + QByteArray::number(status)
                    + (status == 200 ? QByteArrayLiteral(" OK")
                                     : QByteArrayLiteral(" Error"))
                    + QByteArrayLiteral(
                        "\r\nContent-Type: application/json\r\nContent-Length: ")
                    + QByteArray::number(body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                    + body;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QObject::destroyed, &app,
                             [buffer]() { delete buffer; });
        }
    });

    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    const QString configPath =
        directory.filePath(QStringLiteral("client_config.ini"));
    QFile config(configPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream stream(&config);
    stream << "[api]\n"
           << "enabled=true\n"
           << "base_url=http://127.0.0.1:" << server.serverPort() << "\n"
           << "slots_path=/slots\n"
           << "slot_detail_path=/api/v1/parking-slots/{slot_id}\n"
           << "session_images_path=/api/v1/parking-sessions/{session_id}/images\n"
           << "timeout_ms=1000\n"
           << "reconnect_interval_ms=10000\n"
           << "max_reconnect_interval_ms=10000\n"
           << "allow_insecure_http=true\n"
           << "[mqtt]\n"
           << "enabled=false\n";
    config.close();

    ParkingController controller(
        configPath,
        directory.filePath(QStringLiteral("client_config.local.ini")));
    bool started = false;
    bool directSessionReady = false;
    bool legacySessionReady = false;

    QObject::connect(
        &controller, &ParkingController::serverConnectionChanged, &app,
        [&](const QString &, bool connected) {
            if (!connected || started) return;
            started = true;
            controller.applyManualJsonMessage(QJsonObject{
                {QStringLiteral("event_id"),
                 QStringLiteral("session-7-overstay")},
                {QStringLiteral("event_type"),
                 QStringLiteral("OVERTIME_VIOLATION")},
                {QStringLiteral("slot_id"), QStringLiteral("EV01")},
                {QStringLiteral("session_id"), 7},
                {QStringLiteral("parking_state"),
                 QStringLiteral("OCCUPIED")},
                {QStringLiteral("alarm_state"), QStringLiteral("OPEN")}});
            if (!controller.hasEventEvidence(
                    QStringLiteral("session-7-overstay"))
                || controller.eventEvidenceSlotId(
                       QStringLiteral("session-7-overstay"))
                    != QStringLiteral("EV-01")) {
                app.exit(4);
                return;
            }
            ParkingViewState historicalOnlyState = controller.state();
            historicalOnlyState.evSlots.clear();
            historicalOnlyState.parkingSlots.clear();
            historicalOnlyState.slotImages.clear();
            historicalOnlyState.slotPlateNumbers.clear();
            controller.replaceViewState(historicalOnlyState);
            if (controller.state().evSlots.contains(QStringLiteral("EV-01"))) {
                app.exit(10);
                return;
            }
            controller.requestEventEvidence(
                QStringLiteral("session-7-overstay"));
        });

    QObject::connect(
        &controller, &ParkingController::eventEvidenceReady, &app,
        [&](const QString &eventId, const QString &slotId, qint64 sessionId,
            SlotState, const QString &, const QList<ParkingImageResource> &images) {
            if (eventId == QStringLiteral("session-7-overstay")) {
                if (slotId != QStringLiteral("EV-01") || sessionId != 7
                    || images.size() != 1
                    || images.constFirst().sessionId != 7
                    || images.constFirst().url.path()
                        != QStringLiteral("/api/v1/images/70/original")
                    || slotDetailRequests != 0 || session7Requests != 1) {
                    app.exit(5);
                    return;
                }
                directSessionReady = true;
                controller.applyManualJsonMessage(QJsonObject{
                    {QStringLiteral("event_id"),
                     QStringLiteral("legacy-ev02-occupied")},
                    {QStringLiteral("event_type"),
                     QStringLiteral("SLOT_OCCUPIED")},
                    {QStringLiteral("slot_id"), QStringLiteral("EV02")},
                    {QStringLiteral("parking_state"),
                     QStringLiteral("OCCUPIED")}});
                controller.requestEventEvidence(
                    QStringLiteral("legacy-ev02-occupied"));
                return;
            }

            if (eventId == QStringLiteral("legacy-ev02-occupied")) {
                if (slotId != QStringLiteral("EV-02") || sessionId != 8
                    || images.size() != 1
                    || images.constFirst().sessionId != 8
                    || slotDetailRequests != 1 || session8Requests != 1) {
                    app.exit(6);
                    return;
                }
                legacySessionReady = true;
                controller.requestEventEvidence(
                    QStringLiteral("fire-event-without-parking-session"));
            }
        });

    QObject::connect(
        &controller, &ParkingController::eventEvidenceFailed, &app,
        [&](const QString &eventId, const QString &, const QString &message) {
            if (eventId
                    == QStringLiteral("fire-event-without-parking-session")
                && message.contains(QStringLiteral("not linked"))) {
                app.exit(directSessionReady && legacySessionReady ? 0 : 7);
                return;
            }
            app.exit(8);
        });

    QTimer::singleShot(7000, &app, [&]() { app.exit(9); });
    controller.start();
    return app.exec();
}
