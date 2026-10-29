#include "mainwindow.h"
#include "RtspVideoItem.h"
#include "auth/authclient.h"
#include "dialogs/logindialog.h"
#include "services/uifontscale.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QSettings>
#include <QTimer>
#include <qqml.h>

#include <limits>

namespace {
struct ApiStartupOptions
{
    QUrl baseUrl;
    int timeoutMs = 5000;
    bool allowInsecureHttp = false;
    QString localConfigPath;
};

QString configFilePath(const QString &fileName)
{
#ifdef SMART_PARKING_CONFIG_DIR
    return QDir(QStringLiteral(SMART_PARKING_CONFIG_DIR))
        .absoluteFilePath(fileName);
#else
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../config/%1").arg(fileName));
#endif
}

ApiStartupOptions loadApiStartupOptions()
{
    ApiStartupOptions options;
    const QString sharedPath = configFilePath(QStringLiteral("client_config.ini"));
    options.localConfigPath =
        configFilePath(QStringLiteral("client_config.local.ini"));

    QSettings shared(sharedPath, QSettings::IniFormat);
    QSettings local(options.localConfigPath, QSettings::IniFormat);
    const auto setting = [&shared, &local](const QString &key,
                                           const QVariant &fallback) {
        return local.contains(key) ? local.value(key, fallback)
                                   : shared.value(key, fallback);
    };

    options.baseUrl = QUrl(
        setting(QStringLiteral("api/base_url"), QString()).toString().trimmed());
    options.timeoutMs = qBound(
        1000,
        setting(QStringLiteral("api/timeout_ms"), 5000).toInt(),
        60000);
    options.allowInsecureHttp = setting(
        QStringLiteral("api/allow_insecure_http"), false).toBool();
    return options;
}

void saveSelectedServer(const ApiStartupOptions &options,
                        const QUrl &serverBaseUrl)
{
    if (!serverBaseUrl.isValid() || serverBaseUrl.isEmpty()) {
        return;
    }

    QSettings local(options.localConfigPath, QSettings::IniFormat);
    local.setValue(QStringLiteral("api/enabled"), true);
    local.setValue(QStringLiteral("api/base_url"),
                   serverBaseUrl.toString(QUrl::RemovePath
                                          | QUrl::RemoveQuery
                                          | QUrl::RemoveFragment));
    local.sync();
}

constexpr int kReauthenticationExitCode = 73;

int runMonitoringSession(QApplication &app, const AuthSession &session)
{
    if (!session.isValid()) {
        return kReauthenticationExitCode;
    }

    MainWindow window(session);
    QTimer expiryTimer;
    expiryTimer.setSingleShot(true);
    bool reauthenticationInProgress = false;

    const auto requestReauthentication = [&app, &window, &expiryTimer,
                                           &reauthenticationInProgress]() {
        if (reauthenticationInProgress) {
            return;
        }
        reauthenticationInProgress = true;
        expiryTimer.stop();
        if (!window.prepareForReauthentication()) {
            reauthenticationInProgress = false;
            return;
        }
        window.hide();
        app.exit(kReauthenticationExitCode);
    };
    QObject::connect(&window, &MainWindow::reauthenticationRequested,
                     &app, requestReauthentication);
    QObject::connect(&expiryTimer, &QTimer::timeout,
                     &app, requestReauthentication);

    const qint64 remainingMs = QDateTime::currentDateTimeUtc().msecsTo(
        session.expiresAtUtc);
    if (remainingMs <= 0) {
        return kReauthenticationExitCode;
    }
    expiryTimer.start(static_cast<int>(qMin<qint64>(
        remainingMs, std::numeric_limits<int>::max())));

    window.show();
    return app.exec();
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(
        QIcon(QStringLiteral(":/resources/icons/app-icon.png")));
    qmlRegisterType<RtspVideoItem>("Rtsp", 1, 0, "RtspVideoItem");

    ApiStartupOptions apiOptions = loadApiStartupOptions();
    UiFontScale::initialize(app, apiOptions.localConfigPath);
    QString loginNotice;
    for (;;) {
        AuthSession session;
        {
            AuthClient authClient(apiOptions.baseUrl,
                                  apiOptions.timeoutMs,
                                  apiOptions.allowInsecureHttp);
            LoginDialog loginDialog(&authClient, apiOptions.baseUrl);
            if (!loginNotice.isEmpty()) {
                loginDialog.setNotice(loginNotice);
            }
            if (loginDialog.exec() != QDialog::Accepted) {
                return 0;
            }
            session = loginDialog.authSession();
        }

        saveSelectedServer(apiOptions, session.serverOrigin);
        apiOptions.baseUrl = session.serverOrigin;
        const int result = runMonitoringSession(app, session);
        session.accessToken.fill('\0');
        session.accessToken.clear();
        if (result != kReauthenticationExitCode) {
            return result;
        }
        loginNotice = QStringLiteral(
            "Your session ended. Sign in again to continue.");
    }
}
