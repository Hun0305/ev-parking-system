#include "auth/authclient.h"
#include "dialogs/logindialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QHostAddress>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {
QUrl localOrigin(const QTcpServer &server)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
}

void respondOnce(QTcpServer *server, int status, const QByteArray &body)
{
    QObject::connect(server, &QTcpServer::newConnection, server,
                     [server, status, body]() {
        QTcpSocket *socket = server->nextPendingConnection();
        auto *buffer = new QByteArray;
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [socket, buffer, status, body]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QString headers = QString::fromLatin1(buffer->left(headerEnd));
            const auto lengthMatch = QRegularExpression(
                QStringLiteral("Content-Length:\\s*(\\d+)"),
                QRegularExpression::CaseInsensitiveOption).match(headers);
            const int contentLength = lengthMatch.hasMatch()
                ? lengthMatch.captured(1).toInt() : 0;
            if (buffer->size() < headerEnd + 4 + contentLength) return;

            socket->disconnect(socket, nullptr, socket, nullptr);
            const QByteArray response = "HTTP/1.1 "
                + QByteArray::number(status)
                + (status >= 400 ? " Error" : " OK")
                + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size())
                + "\r\nConnection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QObject::destroyed, server,
                         [buffer]() { delete buffer; });
    });
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qRegisterMetaType<AuthSession>();

    QTcpServer successServer;
    if (!successServer.listen(QHostAddress::LocalHost, 0)) return 1;
    respondOnce(&successServer, 200,
        R"({"success":true,"accessToken":"dialog-token","tokenType":"Bearer","expiresAt":"2099-08-20T12:00:00Z","user":{"id":3,"accountId":"operator"}})");
    AuthClient successClient(localOrigin(successServer), 1500, true);
    LoginDialog successDialog(&successClient, localOrigin(successServer));

    auto *serverInput = successDialog.findChild<QLineEdit *>(
        QStringLiteral("loginServerInput"));
    auto *accountInput = successDialog.findChild<QLineEdit *>(
        QStringLiteral("loginAccountInput"));
    auto *passwordInput = successDialog.findChild<QLineEdit *>(
        QStringLiteral("loginPasswordInput"));
    auto *showPassword = successDialog.findChild<QCheckBox *>(
        QStringLiteral("loginShowPasswordCheck"));
    auto *submit = successDialog.findChild<QPushButton *>(
        QStringLiteral("loginSubmitButton"));
    if (!serverInput || !accountInput || !passwordInput
        || !showPassword || !submit) return 2;
    if (passwordInput->echoMode() != QLineEdit::Password) return 3;
    showPassword->setChecked(true);
    if (passwordInput->echoMode() != QLineEdit::Normal) return 4;
    showPassword->setChecked(false);

    accountInput->setText(QStringLiteral("operator"));
    passwordInput->setText(QStringLiteral("application-password"));
    QSignalSpy acceptedSpy(&successDialog, &QDialog::accepted);
    successDialog.show();
    QTest::mouseClick(submit, Qt::LeftButton);
    if (!acceptedSpy.wait(2000)) return 5;
    if (!successDialog.authSession().isValid()
        || successDialog.authSession().accessToken
            != QByteArrayLiteral("dialog-token")) return 6;
    if (!passwordInput->text().isEmpty()) return 7;

    QTcpServer failureServer;
    if (!failureServer.listen(QHostAddress::LocalHost, 0)) return 8;
    respondOnce(&failureServer, 401,
                R"({"success":false,"error":"invalid account or password"})");
    AuthClient failureClient(localOrigin(failureServer), 1500, true);
    LoginDialog failureDialog(&failureClient, localOrigin(failureServer));
    auto *failureAccount = failureDialog.findChild<QLineEdit *>(
        QStringLiteral("loginAccountInput"));
    auto *failurePassword = failureDialog.findChild<QLineEdit *>(
        QStringLiteral("loginPasswordInput"));
    auto *failureSubmit = failureDialog.findChild<QPushButton *>(
        QStringLiteral("loginSubmitButton"));
    if (!failureAccount || !failurePassword || !failureSubmit) return 9;

    failureAccount->setText(QStringLiteral("operator"));
    failurePassword->setText(QStringLiteral("wrong-password"));
    QSignalSpy failedSpy(&failureClient, &AuthClient::loginFailed);
    failureDialog.show();
    QTest::mouseClick(failureSubmit, Qt::LeftButton);
    if (!failedSpy.wait(2000)) return 10;
    QCoreApplication::processEvents();
    if (failureDialog.result() == QDialog::Accepted
        || !failureSubmit->isEnabled()
        || !failurePassword->text().isEmpty()) return 11;

    return 0;
}
