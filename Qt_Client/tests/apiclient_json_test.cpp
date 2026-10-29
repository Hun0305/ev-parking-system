#include "api/apiclient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
struct Result
{
    bool received = false;
    bool failed = false;
    QString error;
    int status = 0;
    QByteArray request;
};

Result runScenario(int httpStatus, const QByteArray &responseBody,
                   bool putRequest, bool respond, int clientTimeoutMs)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return {};
    ApiClient client(QUrl(QStringLiteral("http://127.0.0.1:%1")
                              .arg(server.serverPort())),
                     clientTimeoutMs, true);
    Result result;
    QEventLoop loop;

    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&]() {
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
            result.request = *buffer;
            socket->disconnect(socket, nullptr, socket, nullptr);
            if (!respond) return;
            const QByteArray reason = httpStatus >= 400 ? "Error" : "OK";
            const QByteArray response = "HTTP/1.1 "
                + QByteArray::number(httpStatus) + ' ' + reason
                + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(responseBody.size())
                + "\r\nConnection: close\r\n\r\n" + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, &loop,
                         [buffer]() { delete buffer; });
    });
    QObject::connect(&client, &ApiClient::jsonReceived, &loop,
                     [&](const QString &, const QJsonDocument &, int, int status) {
        result.received = true;
        result.status = status;
        loop.quit();
    });
    QObject::connect(&client, &ApiClient::requestFailed, &loop,
                     [&](const QString &, const QString &error, int, int status) {
        result.failed = true;
        result.error = error;
        result.status = status;
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    if (putRequest) {
        client.putJson(QStringLiteral("/settings"),
                       QJsonObject{{QStringLiteral("thresholdSeconds"), 1800}});
    } else {
        client.getJson(QStringLiteral("/settings"));
    }
    loop.exec();
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const Result put = runScenario(
        200, R"({"success":true,"thresholdSeconds":1800})", true, true, 500);
    if (!put.received || put.failed || put.status != 200) return 1;
    if (!put.request.startsWith("PUT /settings HTTP/1.1")) return 2;
    if (!put.request.contains("\"thresholdSeconds\":1800")) return 3;
    if (!put.request.toLower().contains("content-type: application/json")) return 4;

    const Result badRequest = runScenario(
        400, R"({"success":false,"error":"thresholdSeconds must be between 60 and 86400"})",
        true, true, 500);
    if (!badRequest.failed || badRequest.status != 400
        || !badRequest.error.contains(QStringLiteral("between 60 and 86400"))) return 5;

    const Result serverError = runScenario(
        500, R"({"success":false,"error":"database unavailable"})",
        false, true, 500);
    if (!serverError.failed || serverError.status != 500
        || !serverError.error.contains(QStringLiteral("database unavailable"))) return 6;

    const Result invalidJson = runScenario(200, QByteArrayLiteral("not-json"),
                                           false, true, 500);
    if (!invalidJson.failed
        || !invalidJson.error.contains(QStringLiteral("Invalid JSON"))) return 7;

    const Result timeout = runScenario(200, QByteArray(), false, false, 80);
    if (!timeout.failed
        || !timeout.error.contains(QStringLiteral("timed out"))) return 8;

    QTcpServer reservedPort;
    if (!reservedPort.listen(QHostAddress::LocalHost, 0)) return 9;
    const quint16 unusedPort = reservedPort.serverPort();
    reservedPort.close();
    ApiClient unavailable(QUrl(QStringLiteral("http://127.0.0.1:%1")
                                   .arg(unusedPort)), 200, true);
    QEventLoop failureLoop;
    bool connectionFailed = false;
    QObject::connect(&unavailable, &ApiClient::requestFailed, &failureLoop,
                     [&](const QString &, const QString &message, int, int) {
        connectionFailed = !message.contains(QStringLiteral("HTTP 0"));
        failureLoop.quit();
    });
    QTimer::singleShot(1500, &failureLoop, &QEventLoop::quit);
    unavailable.getJson(QStringLiteral("/settings"));
    failureLoop.exec();
    return connectionFailed ? 0 : 10;
}
