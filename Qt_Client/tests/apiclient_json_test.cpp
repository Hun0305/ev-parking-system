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
    bool authenticationRequired = false;
    QString error;
    int status = 0;
    QByteArray request;
};

enum class AuthenticationMode {
    None,
    SameOrigin,
    CrossOrigin
};

Result runScenario(int httpStatus, const QByteArray &responseBody,
                   bool putRequest, bool respond, int clientTimeoutMs,
                   AuthenticationMode authenticationMode =
                       AuthenticationMode::None)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return {};
    const QUrl serverOrigin(QStringLiteral("http://127.0.0.1:%1")
                                .arg(server.serverPort()));
    ApiClient client(serverOrigin, clientTimeoutMs, true);
    if (authenticationMode == AuthenticationMode::SameOrigin) {
        client.setBearerAuthentication(
            serverOrigin, QByteArrayLiteral("same-origin-token"));
    } else if (authenticationMode == AuthenticationMode::CrossOrigin) {
        client.setBearerAuthentication(
            QUrl(QStringLiteral("http://localhost:%1")
                     .arg(server.serverPort())),
            QByteArrayLiteral("cross-origin-token"));
    }
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
    QObject::connect(&client, &ApiClient::authenticationRequired, &loop,
                     [&]() {
        result.authenticationRequired = true;
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
    if (!connectionFailed) return 10;

    const Result sameOrigin = runScenario(
        200, R"({"success":true})", false, true, 500,
        AuthenticationMode::SameOrigin);
    if (!sameOrigin.received || sameOrigin.failed) return 11;
    if (!sameOrigin.request.toLower().contains(
            "authorization: bearer same-origin-token")) return 12;

    const Result crossOrigin = runScenario(
        200, R"({"success":true})", false, true, 500,
        AuthenticationMode::CrossOrigin);
    if (!crossOrigin.received || crossOrigin.failed) return 13;
    if (crossOrigin.request.toLower().contains("authorization:")) return 14;

    const Result unauthorized = runScenario(
        401, R"({"success":false,"error":"authentication required"})",
        false, true, 500, AuthenticationMode::SameOrigin);
    if (!unauthorized.authenticationRequired
        || unauthorized.received || unauthorized.failed) return 15;

    const Result crossOriginUnauthorized = runScenario(
        401, R"({"success":false,"error":"authentication required"})",
        false, true, 500, AuthenticationMode::CrossOrigin);
    if (crossOriginUnauthorized.authenticationRequired
        || crossOriginUnauthorized.received
        || !crossOriginUnauthorized.failed
        || crossOriginUnauthorized.status != 401) return 16;

    return 0;
}
