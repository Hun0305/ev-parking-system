#include "pages/settingspage.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SettingsPage page(QStringLiteral("camera_config.ini"), QString());

    int refreshRequests = 0;
    int updateRequests = 0;
    int requestedSeconds = -1;
    QString requestedServerUrl;
    QObject::connect(&page, &SettingsPage::saveServerBaseUrlRequested,
                     &app, [&](const QString &url) { requestedServerUrl = url; });
    QObject::connect(&page, &SettingsPage::overstayThresholdRefreshRequested,
                     &app, [&]() { ++refreshRequests; });
    QObject::connect(&page, &SettingsPage::overstayThresholdUpdateRequested,
                     &app, [&](int seconds) {
                         ++updateRequests;
                         requestedSeconds = seconds;
                     });

    if (SettingsPage::overstaySeconds(1, 0, 0) != 3600) return 1;
    if (SettingsPage::overstaySeconds(0, 30, 0) != 1800) return 2;
    if (SettingsPage::overstaySeconds(2, 0, 0) != 7200) return 3;

    auto *scheme = page.findChild<QComboBox *>(QStringLiteral("serverApiSchemeInput"));
    auto *host = page.findChild<QLineEdit *>(QStringLiteral("serverApiHostInput"));
    auto *port = page.findChild<QSpinBox *>(QStringLiteral("serverApiPortInput"));
    if (!scheme || !host || !port) return 20;
    auto *cameraUsername = page.findChild<QLineEdit *>(
        QStringLiteral("cameraUsernameInput"));
    auto *cameraPassword = page.findChild<QLineEdit *>(
        QStringLiteral("cameraPasswordInput"));
    auto *saveCamera = page.findChild<QPushButton *>(
        QStringLiteral("saveCameraSettingsButton"));
    if (!cameraUsername || !cameraPassword || !saveCamera
        || cameraPassword->echoMode() != QLineEdit::Password) return 26;
    QString requestedCameraIp;
    QString requestedCameraUsername;
    QString requestedCameraPassword;
    QObject::connect(&page, &SettingsPage::saveCameraCredentialsRequested,
                     &app, [&](const QString &ip, const QString &username,
                               const QString &password) {
        requestedCameraIp = ip;
        requestedCameraUsername = username;
        requestedCameraPassword = password;
    });
    page.setCameraIp(QStringLiteral("172.20.32.1"));
    page.setCameraCredentials(QStringLiteral("admin"),
                               QStringLiteral("camera-secret"));
    saveCamera->click();
    if (requestedCameraIp != QStringLiteral("172.20.32.1")
        || requestedCameraUsername != QStringLiteral("admin")
        || requestedCameraPassword != QStringLiteral("camera-secret")) return 27;
    page.setServerBaseUrl(QStringLiteral("http://172.20.32.97:8080"));
    if (scheme->currentText() != QStringLiteral("http")
        || host->text() != QStringLiteral("172.20.32.97")
        || port->value() != 8080) return 21;
    QMetaObject::invokeMethod(host, "returnPressed");
    if (requestedServerUrl != QStringLiteral("http://172.20.32.97:8080")) return 22;
    page.setServerBaseUrl(QStringLiteral("https://pi.example.test:8443"));
    if (scheme->currentText() != QStringLiteral("https")
        || host->text() != QStringLiteral("pi.example.test")
        || port->value() != 8443) return 23;

    page.setServerConnectionStatus(QStringLiteral("Connected"), true);
    page.show();
    app.processEvents();
    if (refreshRequests != 1) return 4;

    auto *hours = page.findChild<QSpinBox *>(QStringLiteral("overstayHoursInput"));
    auto *minutes = page.findChild<QSpinBox *>(QStringLiteral("overstayMinutesInput"));
    auto *seconds = page.findChild<QSpinBox *>(QStringLiteral("overstaySecondsInput"));
    auto *current = page.findChild<QLabel *>(QStringLiteral("currentOverstayThresholdLabel"));
    auto *policy = page.findChild<QLabel *>(QStringLiteral("overstayApplyPolicyLabel"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("overstayStatusLabel"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyOverstayThresholdButton"));
    if (!hours || !minutes || !seconds || !current || !policy || !status || !apply) return 5;

    page.setOverstayThreshold(3600, QStringLiteral("ACTIVE_AND_NEW_SESSIONS"), false);
    if (hours->value() != 1 || minutes->value() != 0 || seconds->value() != 0) return 6;
    if (current->text() != QStringLiteral("01h 00m 00s")) return 7;
    if (!policy->text().contains(QStringLiteral("ACTIVE_AND_NEW_SESSIONS"))) return 8;

    hours->setValue(0);
    minutes->setValue(30);
    seconds->setValue(0);
    apply->click();
    apply->click();
    if (updateRequests != 1 || requestedSeconds != 1800) return 9;
    if (apply->isEnabled()) return 10;

    page.setOverstayThresholdError(QStringLiteral("HTTP 500"), true);
    if (current->text() != QStringLiteral("01h 00m 00s")) return 11;
    if (hours->value() != 0 || minutes->value() != 30 || seconds->value() != 0) return 12;
    if (!status->text().contains(QStringLiteral("HTTP 500"))) return 13;

    apply->click();
    page.setOverstayThreshold(1800, QStringLiteral("ACTIVE_AND_NEW_SESSIONS"), true);
    if (current->text() != QStringLiteral("00h 30m 00s")) return 14;
    if (!status->text().contains(QStringLiteral("changed"))) return 15;

    hours->setValue(0);
    minutes->setValue(0);
    seconds->setValue(59);
    const int requestsBeforeInvalid = updateRequests;
    apply->click();
    if (updateRequests != requestsBeforeInvalid) return 16;
    if (!status->text().contains(QStringLiteral("between"))) return 17;

    page.setServerConnectionStatus(QStringLiteral("Disconnected"), false);
    if (apply->isEnabled()) return 18;
    hours->setValue(24);
    minutes->setValue(10);
    hours->setValue(23);
    hours->setValue(24);
    if (minutes->value() != 0 || seconds->value() != 0) return 19;

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("settingsHelpButton"));
    if (!helpButton || helpButton->icon().isNull()) return 24;
    helpButton->click();
    QApplication::processEvents();
    QDialog *helpDialog = page.findChild<QDialog *>(
        QStringLiteral("settingsHelpDialog"));
    QLabel *helpSteps = helpDialog
        ? helpDialog->findChild<QLabel *>(QStringLiteral("settingsHelpSteps"))
        : nullptr;
    if (!helpDialog || !helpSteps
        || !helpSteps->text().contains(QStringLiteral("Save and reconnect"))) {
        return 25;
    }
    helpDialog->close();
    return 0;
}
