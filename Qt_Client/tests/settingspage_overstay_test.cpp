#include "pages/settingspage.h"
#include "services/uifontscale.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir uiConfigDirectory(
        QDir::current().filePath(QStringLiteral("settings-ui-test-XXXXXX")));
    if (!uiConfigDirectory.isValid()) return 40;
    UiFontScale::initialize(
        app, uiConfigDirectory.filePath(QStringLiteral("client_config.local.ini")));
    QString resetError;
    SettingsPage page(QStringLiteral("camera_config.ini"), QString());

    QTabWidget *systemTabs = page.findChild<QTabWidget *>(
        QStringLiteral("systemTabWidget"));
    QScrollArea *connectionsScroll = page.findChild<QScrollArea *>(
        QStringLiteral("systemConnectionsScrollArea"));
    QGroupBox *policyGroup = page.findChild<QGroupBox *>(
        QStringLiteral("parkingPolicyGroup"));
    if (!systemTabs || systemTabs->count() != 1
        || systemTabs->tabText(0) != QStringLiteral("Configuration")
        || !connectionsScroll || !connectionsScroll->widgetResizable()
        || !connectionsScroll->widget() || !connectionsScroll->widget()->layout()
        || connectionsScroll->widget()->layout()->contentsMargins().left() != 16
        || connectionsScroll->widget()->maximumWidth() != 960
        || !policyGroup || policyGroup->parentWidget() != connectionsScroll->widget()
        || !page.styleSheet().contains(QStringLiteral("uiActionRole"))) {
        return 28;
    }

    int refreshRequests = 0;
    int updateRequests = 0;
    int requestedSeconds = -1;
    QString requestedServerUrl;
    int reconnectRequests = 0;
    QObject::connect(&page, &SettingsPage::saveServerBaseUrlRequested,
                     &app, [&](const QString &url) { requestedServerUrl = url; });
    QObject::connect(&page, &SettingsPage::overstayThresholdRefreshRequested,
                     &app, [&]() { ++refreshRequests; });
    QObject::connect(&page, &SettingsPage::overstayThresholdUpdateRequested,
                     &app, [&](int seconds) {
                         ++updateRequests;
                         requestedSeconds = seconds;
                     });
    QObject::connect(&page, &SettingsPage::reconnectServerRequested,
                     &app, [&]() { ++reconnectRequests; });
    if (SettingsPage::overstaySeconds(1, 0, 0) != 3600) return 1;
    if (SettingsPage::overstaySeconds(0, 30, 0) != 1800) return 2;
    if (SettingsPage::overstaySeconds(2, 0, 0) != 7200) return 3;

    auto *httpRadio = page.findChild<QRadioButton *>(QStringLiteral("serverApiHttpRadio"));
    auto *httpsRadio = page.findChild<QRadioButton *>(QStringLiteral("serverApiHttpsRadio"));
    auto *host = page.findChild<QLineEdit *>(QStringLiteral("serverApiHostInput"));
    auto *port = page.findChild<QSpinBox *>(QStringLiteral("serverApiPortInput"));
    if (!httpRadio || !httpsRadio || !host || !port
        || !httpRadio->isChecked() || httpsRadio->isChecked()
        || httpRadio->text() != QStringLiteral("HTTP")
        || httpsRadio->text() != QStringLiteral("HTTPS")) return 20;
    auto *cameraUsername = page.findChild<QLineEdit *>(
        QStringLiteral("cameraUsernameInput"));
    auto *cameraPassword = page.findChild<QLineEdit *>(
        QStringLiteral("cameraPasswordInput"));
    auto *saveCamera = page.findChild<QPushButton *>(
        QStringLiteral("saveCameraSettingsButton"));
    auto *showPassword = page.findChild<QCheckBox *>(
        QStringLiteral("showCameraPasswordCheck"));
    if (!cameraUsername || !cameraPassword || !saveCamera || !showPassword
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
    showPassword->setChecked(true);
    if (cameraPassword->echoMode() != QLineEdit::Normal) return 29;
    showPassword->setChecked(false);
    if (cameraPassword->echoMode() != QLineEdit::Password) return 30;
    saveCamera->click();
    if (requestedCameraIp != QStringLiteral("172.20.32.1")
        || requestedCameraUsername != QStringLiteral("admin")
        || requestedCameraPassword != QStringLiteral("camera-secret")) return 27;
    page.setServerBaseUrl(QStringLiteral("http://172.20.32.97:8080"));
    if (!httpRadio->isChecked() || httpsRadio->isChecked()
        || host->text() != QStringLiteral("172.20.32.97")
        || port->value() != 8080) return 21;
    QMetaObject::invokeMethod(host, "returnPressed");
    if (requestedServerUrl != QStringLiteral("http://172.20.32.97:8080")) return 22;
    page.setServerBaseUrl(QStringLiteral("https://pi.example.test:8443"));
    if (!httpsRadio->isChecked() || httpRadio->isChecked()
        || host->text() != QStringLiteral("pi.example.test")
        || port->value() != 8443) return 23;
    auto *reconnect = page.findChild<QPushButton *>(
        QStringLiteral("reconnectServerButton"));
    auto *applyEndpoint = page.findChild<QPushButton *>(
        QStringLiteral("saveServerSettingsButton"));
    if (!reconnect || !applyEndpoint
        || reconnect->text() != QStringLiteral("Retry connection")
        || applyEndpoint->text() != QStringLiteral("Apply & reconnect")
        || reconnect->property("uiActionRole").toString()
            != QStringLiteral("secondary")
        || applyEndpoint->property("uiActionRole").toString()
            != QStringLiteral("primary")) return 31;
    reconnect->click();
    if (reconnectRequests != 1) return 32;

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
    if (hours->suffix() != QStringLiteral("h")
        || minutes->suffix() != QStringLiteral("m")
        || seconds->suffix() != QStringLiteral("s")) return 44;
    for (QSpinBox *input : {hours, minutes, seconds}) {
        if (!input->property("uiDurationInput").toBool()
            || input->sizePolicy().horizontalPolicy() != QSizePolicy::Fixed
            || input->alignment() != Qt::AlignCenter) return 45;

        QStyleOptionSpinBox option;
        option.initFrom(input);
        option.rect = input->rect();
        option.subControls = QStyle::SC_All;
        option.buttonSymbols = input->buttonSymbols();
        option.stepEnabled = QAbstractSpinBox::StepUpEnabled
                           | QAbstractSpinBox::StepDownEnabled;
        option.frame = input->hasFrame();
        const QRect editRect = input->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, input);
        const QRect upRect = input->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, input);
        const QRect downRect = input->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, input);
        if (editRect.isEmpty() || upRect.isEmpty() || downRect.isEmpty()
            || input->fontMetrics().horizontalAdvance(input->text()) + 8
                   > editRect.width()
            || editRect.intersects(upRect) || editRect.intersects(downRect)
            || input->style()->hitTestComplexControl(
                   QStyle::CC_SpinBox, &option, upRect.center(), input)
                   != QStyle::SC_SpinBoxUp
            || input->style()->hitTestComplexControl(
                   QStyle::CC_SpinBox, &option, downRect.center(), input)
                   != QStyle::SC_SpinBoxDown) return 46;
    }
    if (hours->geometry().right() >= minutes->geometry().left()
        || minutes->geometry().right() >= seconds->geometry().left()) return 47;

    page.setOverstayThreshold(3600, QStringLiteral("ACTIVE_AND_NEW_SESSIONS"), false);
    if (hours->value() != 1 || minutes->value() != 0 || seconds->value() != 0) return 6;
    if (current->text() != QStringLiteral("01h 00m 00s")) return 7;
    if (!policy->text().contains(QStringLiteral("ACTIVE_AND_NEW_SESSIONS"))) return 8;
    for (QSpinBox *input : {hours, minutes, seconds}) {
        QStyleOptionSpinBox option;
        option.initFrom(input);
        option.rect = input->rect();
        option.subControls = QStyle::SC_All;
        option.buttonSymbols = input->buttonSymbols();
        option.stepEnabled = QAbstractSpinBox::StepUpEnabled
                           | QAbstractSpinBox::StepDownEnabled;
        option.frame = input->hasFrame();
        const QRect upRect = input->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, input);
        const QRect downRect = input->style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, input);
        const int originalValue = input->value();
        QTest::mouseClick(input, Qt::LeftButton, Qt::NoModifier,
                          upRect.center());
        if (input->value() != originalValue + 1) return 48;
        QTest::mouseClick(input, Qt::LeftButton, Qt::NoModifier,
                          downRect.center());
        if (input->value() != originalValue) return 49;
    }

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
    if (!status->text().contains(QStringLiteral("updated"))) return 15;

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

    const QString captureDir = qEnvironmentVariable("SYSTEM_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        page.resize(900, 720);
        page.show();
        QApplication::processEvents();
        QString largeError;
        if (!UiFontScale::setPercent(200, &largeError)) return 42;
        systemTabs->setCurrentIndex(0);
        QApplication::processEvents();
        connectionsScroll->verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        if (!page.grab().save(
                QDir(captureDir).filePath(QStringLiteral("system-configuration-200.png")))) {
            return 33;
        }
        connectionsScroll->verticalScrollBar()->setValue(
            connectionsScroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        if (!page.grab().save(
                QDir(captureDir).filePath(QStringLiteral("system-configuration-policy-200.png")))) {
            return 34;
        }
        if (!UiFontScale::setPercent(100, &resetError)) return 43;
    }

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
        || !helpSteps->text().contains(QStringLiteral("Configuration"))) {
        return 25;
    }
    helpDialog->close();
    return 0;
}
