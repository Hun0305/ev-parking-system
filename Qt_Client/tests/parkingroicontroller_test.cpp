#include "controllers/parkingcontroller.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
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

    ParkingRoi stored{0.1, 0.2, 0.3, 0.4};
    QHash<QString, ParkingRoi> storedGeneral;
    for (int slotNumber = 1; slotNumber <= 4; ++slotNumber) {
        storedGeneral.insert(
            QStringLiteral("P%1").arg(slotNumber, 2, 10, QLatin1Char('0')),
            ParkingRoi{0.15, 0.25, 0.35, 0.45});
    }
    QSet<QString> generalPutSlots;
    int putCount = 0;
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
            int status = 200;
            QByteArray responseBody;
            if (requestLine.startsWith("GET /slots ")) {
                responseBody = QByteArrayLiteral("{\"items\":[]}");
            } else if (requestLine.startsWith(
                           "GET /api/v1/settings/parking-slots/roi ")) {
                responseBody = QJsonDocument(QJsonObject{
                    {QStringLiteral("count"), 1},
                    {QStringLiteral("items"), QJsonArray{QJsonObject{
                        {QStringLiteral("slotId"), QStringLiteral("slot_01")},
                        {QStringLiteral("roi"), ParkingRoiCodec::toJson(stored)}}}}})
                                   .toJson(QJsonDocument::Compact);
            } else if (requestLine.startsWith(
                           "GET /api/v1/settings/parking-slots/slot_01/roi ")) {
                responseBody = QJsonDocument(QJsonObject{
                    {QStringLiteral("slotId"), QStringLiteral("slot_01")},
                    {QStringLiteral("roi"), ParkingRoiCodec::toJson(stored)}})
                                   .toJson(QJsonDocument::Compact);
            } else if (requestLine.startsWith(
                           "PUT /api/v1/settings/parking-slots/slot_01/roi ")) {
                ++putCount;
                const QJsonDocument body = QJsonDocument::fromJson(
                    buffer->mid(headerEnd + 4, contentLength));
                if (!body.isObject()) {
                    status = 400;
                    responseBody = QByteArrayLiteral("{\"error\":\"invalid JSON\"}");
                } else {
                    const QJsonObject object = body.object();
                    const ParkingRoi requested{
                        object.value(QStringLiteral("x")).toDouble(),
                        object.value(QStringLiteral("y")).toDouble(),
                        object.value(QStringLiteral("width")).toDouble(),
                        object.value(QStringLiteral("height")).toDouble()};
                    if (putCount == 1) stored = requested;
                    responseBody = QJsonDocument(QJsonObject{
                        {QStringLiteral("success"), true},
                        {QStringLiteral("slotId"), QStringLiteral("slot_01")},
                        {QStringLiteral("appliedImmediately"), true},
                        {QStringLiteral("roi"), ParkingRoiCodec::toJson(requested)}})
                                       .toJson(QJsonDocument::Compact);
                }
            } else if (requestLine.startsWith(
                           "GET /api/v1/settings/parking-slots/EV04/roi ")) {
                status = 404;
                responseBody = QByteArrayLiteral("{\"error\":\"slot not found\"}");
            } else {
                const QString requestText = QString::fromLatin1(requestLine);
                const QRegularExpressionMatch generalGet = QRegularExpression(
                    QStringLiteral("^GET /api/v1/settings/parking-slots/(P0[1-4])/roi "))
                        .match(requestText);
                const QRegularExpressionMatch generalPut = QRegularExpression(
                    QStringLiteral("^PUT /api/v1/settings/parking-slots/(P0[1-4])/roi "))
                        .match(requestText);
                if (generalGet.hasMatch()) {
                    const QString serverSlotId = generalGet.captured(1);
                    responseBody = QJsonDocument(QJsonObject{
                        {QStringLiteral("slotId"), serverSlotId},
                        {QStringLiteral("roi"), ParkingRoiCodec::toJson(
                             storedGeneral.value(serverSlotId))}})
                                       .toJson(QJsonDocument::Compact);
                } else if (generalPut.hasMatch()) {
                    const QString serverSlotId = generalPut.captured(1);
                    ++putCount;
                    generalPutSlots.insert(serverSlotId);
                    const QJsonDocument body = QJsonDocument::fromJson(
                        buffer->mid(headerEnd + 4, contentLength));
                    if (!body.isObject()) {
                        status = 400;
                        responseBody = QByteArrayLiteral("{\"error\":\"invalid JSON\"}");
                    } else {
                        const QJsonObject object = body.object();
                        const ParkingRoi requested{
                            object.value(QStringLiteral("x")).toDouble(),
                            object.value(QStringLiteral("y")).toDouble(),
                            object.value(QStringLiteral("width")).toDouble(),
                            object.value(QStringLiteral("height")).toDouble()};
                        storedGeneral.insert(serverSlotId, requested);
                        responseBody = QJsonDocument(QJsonObject{
                            {QStringLiteral("success"), true},
                            {QStringLiteral("slotId"), serverSlotId},
                            {QStringLiteral("appliedImmediately"), true},
                            {QStringLiteral("roi"), ParkingRoiCodec::toJson(requested)}})
                                           .toJson(QJsonDocument::Compact);
                    }
                } else {
                    status = 404;
                    responseBody = QByteArrayLiteral("{\"error\":\"unexpected path\"}");
                }
            }

            const QByteArray reason = status >= 400 ? "Error" : "OK";
            const QByteArray response = QByteArrayLiteral("HTTP/1.1 ")
                + QByteArray::number(status) + ' ' + reason
                + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
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
    const QString configPath = directory.filePath(QStringLiteral("client_config.ini"));
    QFile config(configPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream stream(&config);
    stream << "[api]\n"
           << "enabled=true\n"
           << "base_url=http://127.0.0.1:" << server.serverPort() << "\n"
           << "slots_path=/slots\n"
           << "slot_detail_path=/slot/{slot_id}\n"
           << "session_images_path=/session/{session_id}/images\n"
           << "overstay_threshold_path=/overstay\n"
           << "parking_roi_list_path=/api/v1/settings/parking-slots/roi\n"
           << "parking_roi_path=/api/v1/settings/parking-slots/{slot_id}/roi\n"
           << "timeout_ms=500\n"
           << "reconnect_interval_ms=10000\n"
           << "max_reconnect_interval_ms=10000\n"
           << "allow_insecure_http=true\n"
           << "[mqtt]\n"
           << "enabled=false\n";
    config.close();

    QFile mapping(directory.filePath(QStringLiteral("slot_mapping.local.json")));
    if (!mapping.open(QIODevice::WriteOnly | QIODevice::Text)) return 4;
    mapping.write(QJsonDocument(QJsonObject{
        {QStringLiteral("mappings"), QJsonArray{
            QJsonObject{
                {QStringLiteral("server_slot_id"), QStringLiteral("slot_01")},
                {QStringLiteral("zone_id"), QStringLiteral("EV-01")}},
            QJsonObject{
                {QStringLiteral("server_slot_id"), QStringLiteral("P01")},
                {QStringLiteral("zone_id"), QStringLiteral("P-01")}},
            QJsonObject{
                {QStringLiteral("server_slot_id"), QStringLiteral("P02")},
                {QStringLiteral("zone_id"), QStringLiteral("P-02")}},
            QJsonObject{
                {QStringLiteral("server_slot_id"), QStringLiteral("P03")},
                {QStringLiteral("zone_id"), QStringLiteral("P-03")}},
            QJsonObject{
                {QStringLiteral("server_slot_id"), QStringLiteral("P04")},
                {QStringLiteral("zone_id"), QStringLiteral("P-04")}}}}})
                      .toJson(QJsonDocument::Compact));
    mapping.close();

    ParkingController controller(
        configPath, directory.filePath(QStringLiteral("client_config.local.ini")));
    bool started = false;
    bool listReceived = false;
    bool singleReceived = false;
    bool saveVerified = false;
    bool mismatchRejected = false;
    bool notFoundReceived = false;
    QStringList verifiedGeneralSlots;
    QObject::connect(&controller, &ParkingController::serverConnectionChanged,
                     &app, [&](const QString &, bool connected) {
        if (connected && !started) {
            started = true;
            controller.requestParkingRois(1);
        }
    });
    QObject::connect(&controller, &ParkingController::parkingRoiListReceived,
                     &app, [&](const ParkingRoiMap &rois, quint64 generation) {
        if (generation != 1 || !rois.contains(QStringLiteral("EV-01"))) {
            app.exit(5);
            return;
        }
        listReceived = true;
        controller.requestParkingRoi(QStringLiteral("EV-01"), 2);
    });
    QObject::connect(&controller, &ParkingController::parkingRoiReceived,
                     &app, [&](const QString &slotId, const ParkingRoi &roi,
                               quint64 generation, bool afterSave,
                               bool appliedImmediately) {
        if (generation >= 7 && generation <= 10 && afterSave) {
            const int slotNumber = static_cast<int>(generation - 6);
            const QString expectedZoneId = QStringLiteral("P-%1").arg(
                slotNumber, 2, 10, QLatin1Char('0'));
            if (slotId != expectedZoneId || !appliedImmediately
                || !roi.nearlyEquals(ParkingRoi{0.15, 0.25, 0.35, 0.45})) {
                app.exit(8);
                return;
            }
            verifiedGeneralSlots.append(slotId);
            if (generation < 10) {
                const QString nextZoneId = QStringLiteral("P-%1").arg(
                    slotNumber + 1, 2, 10, QLatin1Char('0'));
                controller.updateParkingRoi(
                    nextZoneId, ParkingRoi{0.15, 0.25, 0.35, 0.45},
                    generation + 1);
                return;
            }
            app.exit(listReceived && singleReceived && saveVerified
                         && mismatchRejected && notFoundReceived
                         && verifiedGeneralSlots.size() == 4
                         && generalPutSlots == QSet<QString>{
                                QStringLiteral("P01"), QStringLiteral("P02"),
                                QStringLiteral("P03"), QStringLiteral("P04")}
                         && putCount == 6 ? 0 : 8);
            return;
        }
        if (slotId != QStringLiteral("EV-01")) {
            app.exit(6);
            return;
        }
        if (generation == 2 && !afterSave) {
            singleReceived = roi.nearlyEquals(ParkingRoi{0.1, 0.2, 0.3, 0.4});
            controller.updateParkingRoi(
                QStringLiteral("EV-01"), ParkingRoi{0.2, 0.1, 0.4, 0.5}, 3);
        } else if (generation == 3 && afterSave) {
            saveVerified = appliedImmediately
                && roi.nearlyEquals(ParkingRoi{0.2, 0.1, 0.4, 0.5});
            controller.updateParkingRoi(
                QStringLiteral("EV-01"), ParkingRoi{0.05, 0.05, 0.3, 0.3}, 4);
        } else if (generation == 4 && afterSave) {
            app.exit(7);
        }
    });
    QObject::connect(&controller, &ParkingController::parkingRoiRequestFailed,
                     &app, [&](const QString &slotId, const QString &message,
                               quint64 generation, bool saveRequest) {
        if (generation == 4 && slotId == QStringLiteral("EV-01")
            && saveRequest
            && message.contains(QStringLiteral("does not match"))) {
            mismatchRejected = true;
            controller.requestParkingRoi(QStringLiteral("EV-04"), 5);
            return;
        }
        if (generation == 5 && slotId == QStringLiteral("EV-04")
            && !saveRequest && message.contains(QStringLiteral("HTTP 404"))) {
            notFoundReceived = true;
            controller.updateParkingRoi(
                QStringLiteral("EV-01"), ParkingRoi{0.9, 0.0, 0.2, 0.2}, 6);
            return;
        }
        if (generation == 6 && saveRequest
            && message.contains(QStringLiteral("Invalid ROI"))) {
            controller.updateParkingRoi(
                QStringLiteral("P-01"),
                ParkingRoi{0.15, 0.25, 0.35, 0.45}, 7);
            return;
        }
        app.exit(9);
    });

    QTimer::singleShot(7000, &app, [&]() { app.exit(10); });
    controller.start();
    return app.exec();
}
