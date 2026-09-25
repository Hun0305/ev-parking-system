#include "api/imageloader.h"

#include <QBuffer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {

enum class AuthenticationMode {
    SameOrigin,
    CrossOrigin
};

struct Result
{
    bool loaded = false;
    bool failed = false;
    bool dataLoaded = false;
    bool dataFailed = false;
    bool authenticationRequired = false;
    QByteArray data;
    QString contentType;
    QByteArray request;
};

Result runScenario(AuthenticationMode authenticationMode,
                   int statusCode = 200,
                   bool downloadData = false)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return {};

    const QUrl imageUrl(QStringLiteral("http://127.0.0.1:%1/evidence.png")
                            .arg(server.serverPort()));
    const QUrl loginOrigin = authenticationMode == AuthenticationMode::SameOrigin
        ? QUrl(QStringLiteral("http://127.0.0.1:%1")
                   .arg(server.serverPort()))
        : QUrl(QStringLiteral("http://localhost:%1")
                   .arg(server.serverPort()));

    ImageLoader loader(500, true);
    loader.setBearerAuthentication(
        loginOrigin, QByteArrayLiteral("image-bearer-token"));

    QImage image(1, 1, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer pngBuffer(&png);
    if (!pngBuffer.open(QIODevice::WriteOnly) || !image.save(&pngBuffer, "PNG")) {
        return {};
    }

    Result result;
    QEventLoop loop;
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        const auto requestBuffer = QSharedPointer<QByteArray>::create();
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [&, socket, requestBuffer]() {
            requestBuffer->append(socket->readAll());
            if (!requestBuffer->contains("\r\n\r\n")
                || socket->property("responseSent").toBool()) {
                return;
            }
            socket->setProperty("responseSent", true);
            result.request = *requestBuffer;
            const QByteArray responseBody = statusCode == 200 ? png
                : QByteArrayLiteral("{\"error\":\"authentication required\"}");
            const QByteArray response = "HTTP/1.1 "
                + QByteArray::number(statusCode)
                + (statusCode == 200 ? " OK" : " Unauthorized")
                + "\r\nContent-Type: "
                + (statusCode == 200 ? "image/png" : "application/json")
                + "\r\nContent-Length: "
                + QByteArray::number(responseBody.size())
                + "\r\nConnection: close\r\n\r\n" + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
    QObject::connect(&loader, &ImageLoader::imageLoaded, &loop,
                     [&](const QString &, const QPixmap &) {
        result.loaded = true;
        loop.quit();
    });
    QObject::connect(&loader, &ImageLoader::imageFailed, &loop,
                     [&](const QString &, const QString &) {
        result.failed = true;
        loop.quit();
    });
    QObject::connect(
        &loader, &ImageLoader::imageDataLoaded, &loop,
        [&](const QString &, const QByteArray &data,
            const QString &contentType) {
            result.dataLoaded = true;
            result.data = data;
            result.contentType = contentType;
            loop.quit();
        });
    QObject::connect(&loader, &ImageLoader::imageDataFailed, &loop,
                     [&](const QString &, const QString &) {
        result.dataFailed = true;
        loop.quit();
    });
    QObject::connect(&loader, &ImageLoader::authenticationRequired, &loop,
                     [&]() {
        result.authenticationRequired = true;
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    if (downloadData) {
        loader.download(QStringLiteral("evidence-data"), imageUrl);
    } else {
        loader.load(QStringLiteral("evidence"), imageUrl);
    }
    loop.exec();
    return result;
}

}  // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    const Result sameOrigin = runScenario(AuthenticationMode::SameOrigin);
    if (!sameOrigin.loaded || sameOrigin.failed) return 1;
    if (!sameOrigin.request.toLower().contains(
            "authorization: bearer image-bearer-token")) return 2;

    const Result crossOrigin = runScenario(AuthenticationMode::CrossOrigin);
    if (!crossOrigin.loaded || crossOrigin.failed) return 3;
    if (crossOrigin.request.toLower().contains("authorization:")) return 4;

    const Result unauthorized = runScenario(AuthenticationMode::SameOrigin, 401);
    if (!unauthorized.authenticationRequired
        || unauthorized.loaded || unauthorized.failed) return 5;

    const Result crossOriginUnauthorized = runScenario(
        AuthenticationMode::CrossOrigin, 401);
    if (crossOriginUnauthorized.authenticationRequired
        || crossOriginUnauthorized.loaded
        || !crossOriginUnauthorized.failed) return 6;

    const Result dataDownload = runScenario(
        AuthenticationMode::SameOrigin, 200, true);
    if (!dataDownload.dataLoaded || dataDownload.dataFailed
        || dataDownload.data.isEmpty()
        || dataDownload.contentType != QStringLiteral("image/png")) return 7;
    if (!dataDownload.request.toLower().contains(
            "authorization: bearer image-bearer-token")) return 8;

    const Result crossOriginDataDownload = runScenario(
        AuthenticationMode::CrossOrigin, 200, true);
    if (!crossOriginDataDownload.dataLoaded
        || crossOriginDataDownload.dataFailed
        || crossOriginDataDownload.request.toLower().contains(
            "authorization:")) return 9;

    const Result unauthorizedDataDownload = runScenario(
        AuthenticationMode::SameOrigin, 401, true);
    if (!unauthorizedDataDownload.authenticationRequired
        || unauthorizedDataDownload.dataLoaded
        || !unauthorizedDataDownload.dataFailed) return 10;

    return 0;
}
