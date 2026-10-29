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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 1;

    bool updateReceived = false;
    int overstayGetCount = 0;
    int overstayPutCount = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        auto *buffer = new QByteArray;
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [&, socket, buffer]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QString headers = QString::fromLatin1(buffer->left(headerEnd));
            const QRegularExpressionMatch lengthMatch = QRegularExpression(
                QStringLiteral("Content-Length:\\s*(\\d+)"),
                QRegularExpression::CaseInsensitiveOption).match(headers);
            const int contentLength = lengthMatch.hasMatch()
                ? lengthMatch.captured(1).toInt() : 0;
            if (buffer->size() < headerEnd + 4 + contentLength) return;
            socket->disconnect(socket, nullptr, socket, nullptr);

            const QByteArray requestLine = buffer->left(buffer->indexOf("\r\n"));
            QByteArray responseBody;
            if (requestLine.startsWith("GET /slots ")) {
                responseBody = QByteArrayLiteral("{\"items\":[]}");
            } else if (requestLine.startsWith(
                           "GET /api/v1/settings/overstay-threshold ")) {
                ++overstayGetCount;
                responseBody = updateReceived
                    ? QByteArrayLiteral(
                          "{\"thresholdSeconds\":1800,\"thresholdMinutes\":30,"
                          "\"effectiveSeconds\":1800,\"appliedRevision\":3,"
                          "\"runtimeApplied\":true,\"runtimeHealthy\":true,"
                          "\"applyPolicy\":\"ACTIVE_AND_NEW_SESSIONS\"}")
                    : (overstayGetCount == 1
                           ? QByteArrayLiteral(
                                 "{\"thresholdSeconds\":3600,"
                                 "\"thresholdMinutes\":60,"
                                 "\"applyPolicy\":"
                                 "\"ACTIVE_AND_NEW_SESSIONS\"}")
                           : (overstayGetCount == 2
                                  ? QByteArrayLiteral(
                                        "{\"thresholdSeconds\":3600,"
                                        "\"thresholdMinutes\":60,"
                                        "\"effectiveSeconds\":3600,"
                                        "\"appliedRevision\":1,"
                                        "\"runtimeApplied\":false,"
                                        "\"runtimeHealthy\":false,"
                                        "\"applyPolicy\":"
                                        "\"ACTIVE_AND_NEW_SESSIONS\"}")
                                  : QByteArrayLiteral(
                                        "{\"thresholdSeconds\":3600,"
                                        "\"thresholdMinutes\":60,"
                                        "\"effectiveSeconds\":3600,"
                                        "\"appliedRevision\":1,"
                                        "\"runtimeApplied\":true,"
                                        "\"runtimeHealthy\":true,"
                                        "\"applyPolicy\":"
                                        "\"ACTIVE_AND_NEW_SESSIONS\"}")));
            } else if (requestLine.startsWith(
                           "PUT /api/v1/settings/overstay-threshold ")) {
                const QByteArray body = buffer->mid(headerEnd + 4, contentLength);
                const QJsonDocument requestDocument = QJsonDocument::fromJson(body);
                updateReceived = requestDocument.isObject()
                    && requestDocument.object()
                           .value(QStringLiteral("thresholdSeconds")).toInt() == 1800;
                ++overstayPutCount;
                responseBody = overstayPutCount == 1
                    ? QByteArrayLiteral(
                          "{\"success\":true,\"requestedSeconds\":1800,"
                          "\"effectiveSeconds\":1800,"
                          "\"thresholdSeconds\":1800,"
                          "\"appliedRevision\":2,\"runtimeApplied\":false,"
                          "\"runtimeHealthy\":false,"
                          "\"applyPolicy\":\"ACTIVE_AND_NEW_SESSIONS\"}")
                    : QByteArrayLiteral(
                          "{\"success\":true,\"requestedSeconds\":1800,"
                          "\"effectiveSeconds\":1800,"
                          "\"thresholdSeconds\":1800,"
                          "\"appliedRevision\":3,\"runtimeApplied\":true,"
                          "\"runtimeHealthy\":true,"
                          "\"applyPolicy\":\"ACTIVE_AND_NEW_SESSIONS\"}");
            } else {
                responseBody = QByteArrayLiteral("{\"error\":\"unexpected path\"}");
            }

            const QByteArray response = QByteArrayLiteral(
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                + QByteArray::number(responseBody.size())
                + QByteArrayLiteral("\r\nConnection: close\r\n\r\n")
                + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, &app,
                         [buffer]() { delete buffer; });
    });

    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    const QString sharedPath = directory.filePath(QStringLiteral("client_config.ini"));
    QFile config(sharedPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream stream(&config);
    stream << "[api]\n"
           << "enabled=true\n"
           << "base_url=http://127.0.0.1:" << server.serverPort() << "\n"
           << "slots_path=/slots\n"
           << "slot_detail_path=/slot/{slot_id}\n"
           << "session_images_path=/sessions/{session_id}/images\n"
           << "overstay_threshold_path=/api/v1/settings/overstay-threshold\n"
           << "timeout_ms=500\n"
           << "reconnect_interval_ms=10000\n"
           << "max_reconnect_interval_ms=10000\n"
           << "allow_insecure_http=true\n"
           << "[mqtt]\n"
           << "enabled=false\n";
    config.close();

    ParkingController controller(
        sharedPath, directory.filePath(QStringLiteral("client_config.local.ini")));
    bool requestedInitialSetting = false;
    int rejectedFetchContracts = 0;
    bool rejectedUnappliedRuntime = false;
    bool completed = false;
    QObject::connect(&controller, &ParkingController::serverConnectionChanged,
                     &app, [&](const QString &, bool connected) {
        if (connected && !requestedInitialSetting) {
            requestedInitialSetting = true;
            controller.requestOverstayThreshold();
        }
    });
    QObject::connect(&controller, &ParkingController::overstayThresholdReceived,
                     &app, [&](int seconds, const QString &policy, bool afterUpdate) {
        if (policy != QStringLiteral("ACTIVE_AND_NEW_SESSIONS")) {
            app.exit(4);
            return;
        }
        if (!afterUpdate && seconds == 3600) {
            controller.updateOverstayThreshold(1800);
            return;
        }
        if (afterUpdate && seconds == 1800 && updateReceived
            && overstayGetCount == 3 && overstayPutCount == 2
            && rejectedFetchContracts == 2 && rejectedUnappliedRuntime) {
            completed = true;
            app.exit(0);
            return;
        }
        app.exit(5);
    });
    QObject::connect(&controller,
                     &ParkingController::overstayThresholdRequestFailed,
                     &app, [&](const QString &message, bool updateRequest) {
        if (!updateRequest && overstayGetCount <= 2
            && message.contains(QStringLiteral("unhealthy"))) {
            ++rejectedFetchContracts;
            QTimer::singleShot(0, &controller, [&controller]() {
                controller.requestOverstayThreshold();
            });
            return;
        }
        if (updateRequest && overstayPutCount == 1
            && !rejectedUnappliedRuntime) {
            rejectedUnappliedRuntime = true;
            QTimer::singleShot(0, &controller, [&controller]() {
                controller.updateOverstayThreshold(1800);
            });
            return;
        }
        app.exit(6);
    });
    QTimer::singleShot(5000, &app, [&]() { app.exit(completed ? 0 : 7); });
    controller.start();
    return app.exec();
}
