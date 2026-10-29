#include "auth/authclient.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

namespace {
void respondOnce(QTcpServer *server,
                 QByteArray *capturedRequest,
                 int status,
                 const QByteArray &body,
                 int *requestCount = nullptr)
{
    QObject::connect(server, &QTcpServer::newConnection, server,
                     [server, capturedRequest, status, body, requestCount]() {
        QTcpSocket *socket = server->nextPendingConnection();
        auto *buffer = new QByteArray;
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [socket, buffer, capturedRequest, status, body,
                          requestCount]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const QString headers = QString::fromLatin1(buffer->left(headerEnd));
            const auto lengthMatch = QRegularExpression(
                QStringLiteral("Content-Length:\\s*(\\d+)"),
                QRegularExpression::CaseInsensitiveOption).match(headers);
            const int contentLength = lengthMatch.hasMatch()
                ? lengthMatch.captured(1).toInt() : 0;
            if (buffer->size() < headerEnd + 4 + contentLength) {
                return;
            }

            *capturedRequest = *buffer;
            if (requestCount) {
                ++(*requestCount);
            }
            socket->disconnect(socket, nullptr, socket, nullptr);
            const QByteArray reason = status >= 400 ? "Error" : "OK";
            const QByteArray response = "HTTP/1.1 "
                + QByteArray::number(status) + ' ' + reason
                + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size())
                + "\r\nConnection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, socket,
                         [buffer]() { delete buffer; });
    });
}

QUrl localOrigin(const QTcpServer &server)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<AuthSession>();

    QTcpServer successServer;
    if (!successServer.listen(QHostAddress::LocalHost, 0)) return 1;
    QByteArray successRequest;
    int successRequestCount = 0;
    respondOnce(&successServer, &successRequest, 200,
        R"({"success":true,"accessToken":"opaque-test-token","tokenType":"Bearer","expiresAt":"2099-08-20T12:00:00Z","user":{"id":7,"accountId":"operator","displayName":"Operator"}})",
        &successRequestCount);

    AuthClient successClient(localOrigin(successServer), 1500, true);
    QSignalSpy successSpy(&successClient, &AuthClient::loginSucceeded);
    QSignalSpy unexpectedFailureSpy(&successClient, &AuthClient::loginFailed);
    successClient.login(QStringLiteral("  operator  "),
                        QStringLiteral(" pass with spaces "));
    successClient.login(QStringLiteral("second"), QStringLiteral("ignored"));
    if (!successSpy.wait(2000)) return 2;
    if (!unexpectedFailureSpy.isEmpty() || successRequestCount != 1) return 3;
    if (!successRequest.startsWith("POST /api/v1/auth/login HTTP/1.1")) return 4;

    const int bodyOffset = successRequest.indexOf("\r\n\r\n") + 4;
    const QJsonDocument requestDocument = QJsonDocument::fromJson(
        successRequest.mid(bodyOffset));
    if (!requestDocument.isObject()) return 5;
    const QJsonObject requestObject = requestDocument.object();
    if (requestObject.value(QStringLiteral("accountId")).toString()
            != QStringLiteral("operator")) return 6;
    if (requestObject.value(QStringLiteral("password")).toString()
            != QStringLiteral(" pass with spaces ")) return 7;

    const AuthSession session = qvariant_cast<AuthSession>(
        successSpy.takeFirst().at(0));
    if (!session.isValid()
        || session.accessToken != QByteArrayLiteral("opaque-test-token")
        || session.accountId != QStringLiteral("operator")
        || session.userId != 7) return 8;

    QTcpServer failureServer;
    if (!failureServer.listen(QHostAddress::LocalHost, 0)) return 9;
    QByteArray failureRequest;
    respondOnce(&failureServer, &failureRequest, 401,
                R"({"success":false,"error":"invalid account or password"})");
    AuthClient failureClient(localOrigin(failureServer), 1500, true);
    QSignalSpy failureSpy(&failureClient, &AuthClient::loginFailed);
    const QString secret = QStringLiteral("never-echo-this-secret");
    failureClient.login(QStringLiteral("operator"), secret);
    if (!failureSpy.wait(2000)) return 10;
    const QList<QVariant> failure = failureSpy.takeFirst();
    if (failure.at(1).toInt() != 401
        || failure.at(0).toString().contains(secret)) return 11;

    QTcpServer reflectedErrorServer;
    if (!reflectedErrorServer.listen(QHostAddress::LocalHost, 0)) return 12;
    QByteArray reflectedRequest;
    respondOnce(&reflectedErrorServer, &reflectedRequest, 400,
                QByteArrayLiteral(
                    R"({"success":false,"error":"never-echo-this-secret"})"));
    AuthClient reflectedErrorClient(
        localOrigin(reflectedErrorServer), 1500, true);
    QSignalSpy reflectedErrorSpy(
        &reflectedErrorClient, &AuthClient::loginFailed);
    reflectedErrorClient.login(QStringLiteral("operator"), secret);
    if (!reflectedErrorSpy.wait(2000)) return 13;
    if (reflectedErrorSpy.takeFirst().at(0).toString().contains(secret)) {
        return 14;
    }

    AuthClient httpsOnly(QUrl(), 1500, false);
    QString validationError;
    if (httpsOnly.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:8080")),
                                 &validationError)
        || !validationError.contains(QStringLiteral("https://"))) return 15;

    AuthClient loopbackDevelopmentOnly(QUrl(), 1500, true);
    if (loopbackDevelopmentOnly.setBaseUrl(
            QUrl(QStringLiteral("http://192.0.2.1:8080")),
            &validationError)) return 16;

    return 0;
}
