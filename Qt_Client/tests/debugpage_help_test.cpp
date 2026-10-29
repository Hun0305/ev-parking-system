#include "pages/debugpage.h"
#include "services/uifontscale.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir uiConfigDirectory(
        QDir::current().filePath(QStringLiteral("debug-ui-test-XXXXXX")));
    if (!uiConfigDirectory.isValid()) return 14;
    UiFontScale::initialize(
        app, uiConfigDirectory.filePath(QStringLiteral("client_config.local.ini")));
    QString fontScaleError;
    if (!UiFontScale::setPercent(200, &fontScaleError)) return 15;
    DebugPage page;

    QLabel *environmentBanner = page.findChild<QLabel *>(
        QStringLiteral("debugEnvironmentBanner"));
    QTabWidget *tabs = page.findChild<QTabWidget *>(
        QStringLiteral("debugTabWidget"));
    QScrollArea *overviewScroll = page.findChild<QScrollArea *>(
        QStringLiteral("debugOverviewScrollArea"));
    QScrollArea *logsScroll = page.findChild<QScrollArea *>(
        QStringLiteral("debugLogsScrollArea"));
    QScrollArea *toolsScroll = page.findChild<QScrollArea *>(
        QStringLiteral("debugTestToolsScrollArea"));
    if (!environmentBanner || !tabs || !overviewScroll || !logsScroll || !toolsScroll
        || tabs->count() != 3
        || !environmentBanner->text().contains(QStringLiteral("DEVELOPMENT"))
        || !overviewScroll->widgetResizable() || !toolsScroll->widgetResizable()
        || !overviewScroll->widget() || !overviewScroll->widget()->layout()
        || overviewScroll->widget()->layout()->contentsMargins().left() != 16
        || overviewScroll->widget()->maximumWidth() != 1080
        || !logsScroll->widgetResizable() || !logsScroll->widget()
        || !logsScroll->widget()->layout()
        || logsScroll->widget()->layout()->contentsMargins().left() != 16
        || logsScroll->widget()->maximumWidth() != 1080
        || !toolsScroll->widget() || !toolsScroll->widget()->layout()
        || toolsScroll->widget()->layout()->contentsMargins().left() != 16
        || toolsScroll->widget()->maximumWidth() != 900
        || !page.styleSheet().contains(QStringLiteral("uiActionRole"))) {
        return 3;
    }

    QPushButton *reconnectButton = page.findChild<QPushButton *>(
        QStringLiteral("debugReconnectApiButton"));
    QPushButton *ackButton = page.findChild<QPushButton *>(
        QStringLiteral("debugAcknowledgeAlarmsButton"));
    QPushButton *mockButton = page.findChild<QPushButton *>(
        QStringLiteral("debugToggleMockEvButton"));
    QPushButton *randomButton = page.findChild<QPushButton *>(
        QStringLiteral("debugRandomizeParkingButton"));
    QPushButton *nonEvButton = page.findChild<QPushButton *>(
        QStringLiteral("debugNonEvAlertButton"));
    QPushButton *overstayButton = page.findChild<QPushButton *>(
        QStringLiteral("debugOverstayAlertButton"));
    QPushButton *sensorButton = page.findChild<QPushButton *>(
        QStringLiteral("debugSensorErrorButton"));
    QPushButton *sampleButton = page.findChild<QPushButton *>(
        QStringLiteral("debugSampleMessagesButton"));
    QPushButton *injectButton = page.findChild<QPushButton *>(
        QStringLiteral("debugManualInjectButton"));
    QLineEdit *messageEdit = page.findChild<QLineEdit *>(
        QStringLiteral("debugManualMessageEdit"));
    if (!reconnectButton || !ackButton || !mockButton || !randomButton
        || !nonEvButton || !overstayButton || !sensorButton || !sampleButton
        || !injectButton || !messageEdit) return 4;
    if (reconnectButton->property("impactScope").toString()
            != QStringLiteral("SERVER_API")
        || reconnectButton->property("uiActionRole").toString()
            != QStringLiteral("primary")
        || ackButton->property("impactScope").toString()
            != QStringLiteral("LOCAL_QT_STATE")
        || ackButton->property("uiActionRole").toString()
            != QStringLiteral("secondary")
        || nonEvButton->property("impactScope").toString()
            != QStringLiteral("LOCAL_TEST_EVENT")
        || nonEvButton->property("uiActionRole").toString()
            != QStringLiteral("warning")
        || injectButton->property("impactScope").toString()
            != QStringLiteral("LOCAL_TEST_EVENT")
        || injectButton->property("uiActionRole").toString()
            != QStringLiteral("primary")) return 5;

    int reconnectCount = 0;
    int ackCount = 0;
    int mockCount = 0;
    int randomCount = 0;
    int nonEvCount = 0;
    int overstayCount = 0;
    int sensorCount = 0;
    int sampleCount = 0;
    QString manualMessage;
    QObject::connect(&page, &DebugPage::reconnectApiRequested,
                     [&]() { ++reconnectCount; });
    QObject::connect(&page, &DebugPage::clearAlarmsRequested,
                     [&]() { ++ackCount; });
    QObject::connect(&page, &DebugPage::toggleMockEvRequested,
                     [&]() { ++mockCount; });
    QObject::connect(&page, &DebugPage::randomizeParkingRequested,
                     [&]() { ++randomCount; });
    QObject::connect(&page, &DebugPage::nonEvAlertRequested,
                     [&]() { ++nonEvCount; });
    QObject::connect(&page, &DebugPage::overtimeAlertRequested,
                     [&]() { ++overstayCount; });
    QObject::connect(&page, &DebugPage::sensorErrorRequested,
                     [&]() { ++sensorCount; });
    QObject::connect(&page, &DebugPage::sampleMessagesRequested,
                     [&]() { ++sampleCount; });
    QObject::connect(&page, &DebugPage::manualMessageRequested,
                     [&](const QString &message) { manualMessage = message; });
    reconnectButton->click();
    ackButton->click();
    mockButton->click();
    randomButton->click();
    nonEvButton->click();
    overstayButton->click();
    sensorButton->click();
    sampleButton->click();
    messageEdit->setText(QStringLiteral("PARKING_SLOT,P01,OCCUPIED"));
    injectButton->click();
    if (reconnectCount != 1 || ackCount != 1 || mockCount != 1
        || randomCount != 1 || nonEvCount != 1 || overstayCount != 1
        || sensorCount != 1 || sampleCount != 1
        || manualMessage != QStringLiteral("PARKING_SLOT,P01,OCCUPIED")) {
        return 6;
    }

    QTableWidget *logTable = page.findChild<QTableWidget *>(
        QStringLiteral("debugLogTable"));
    QLabel *logState = page.findChild<QLabel *>(
        QStringLiteral("debugLogStateLabel"));
    QComboBox *levelFilter = page.findChild<QComboBox *>(
        QStringLiteral("debugLogLevelFilter"));
    QPushButton *clearLogButton = page.findChild<QPushButton *>(
        QStringLiteral("debugLogClearButton"));
    if (!logTable || !logState || !levelFilter || !clearLogButton
        || logState->isHidden()) return 7;

    DiagnosticLogRecord infoRecord;
    infoRecord.occurredAt = QDateTime::currentDateTime();
    infoRecord.level = QStringLiteral("INFO");
    infoRecord.module = QStringLiteral("API");
    infoRecord.code = QStringLiteral("CONNECTED");
    infoRecord.message = QStringLiteral("Server connected");
    page.appendDiagnosticLog(infoRecord);
    DiagnosticLogRecord errorRecord;
    errorRecord.occurredAt = QDateTime::currentDateTime();
    errorRecord.level = QStringLiteral("ERROR");
    errorRecord.module = QStringLiteral("RTSP");
    errorRecord.code = QStringLiteral("FRAME_STALE");
    errorRecord.message = QStringLiteral("No recent frame");
    page.appendDiagnosticLog(errorRecord);
    if (logTable->rowCount() != 2 || !logState->isHidden()
        || logTable->item(0, 1)->background().color().name()
            != QStringLiteral("#ffebee")) return 8;
    levelFilter->setCurrentText(QStringLiteral("ERROR"));
    if (logTable->isRowHidden(0) || !logTable->isRowHidden(1)) return 9;
    clearLogButton->click();
    if (logTable->rowCount() != 0 || logState->isHidden()) return 10;

    page.resize(820, 620);
    page.show();
    QApplication::processEvents();
    if (overviewScroll->horizontalScrollBarPolicy() != Qt::ScrollBarAsNeeded) {
        return 11;
    }

    const QString captureDir = qEnvironmentVariable("SYSTEM_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        page.resize(900, 720);
        const QStringList fileNames = {
            QStringLiteral("system-diagnostics-200.png"),
            QStringLiteral("system-live-logs-200.png"),
            QStringLiteral("system-test-tools-200.png"),
        };
        for (int index = 0; index < tabs->count(); ++index) {
            tabs->setCurrentIndex(index);
            QApplication::processEvents();
            if (!page.grab().save(QDir(captureDir).filePath(fileNames.at(index)))) {
                return 13;
            }
        }
    }

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("debugHelpButton"));
    if (!helpButton || helpButton->icon().isNull()) return 1;

    helpButton->click();
    QApplication::processEvents();
    QDialog *dialog = page.findChild<QDialog *>(
        QStringLiteral("debugHelpDialog"));
    QLabel *steps = dialog
        ? dialog->findChild<QLabel *>(QStringLiteral("debugHelpSteps"))
        : nullptr;
    QLabel *note = dialog
        ? dialog->findChild<QLabel *>(QStringLiteral("debugHelpNote"))
        : nullptr;
    if (!dialog || !steps || !note
        || !steps->text().contains(QStringLiteral("Test Tools"))
        || !note->text().contains(QStringLiteral("simulation sandbox"))) {
        return 2;
    }

    dialog->close();
    QApplication::processEvents();

    QTabWidget systemTabs;
    systemTabs.addTab(new QWidget(&systemTabs), QStringLiteral("Configuration"));
    DebugPage embeddedPage(&systemTabs, &systemTabs);
    QLabel *embeddedApiImpact = systemTabs.findChild<QLabel *>(
        QStringLiteral("debugApiImpactLabel"));
    if (systemTabs.count() != 4
        || systemTabs.tabText(1) != QStringLiteral("Diagnostics")
        || systemTabs.tabText(2) != QStringLiteral("Live Logs")
        || systemTabs.tabText(3) != QStringLiteral("Test Tools")
        || systemTabs.findChild<QPushButton *>(
               QStringLiteral("debugReconnectApiButton"))
        || !embeddedApiImpact
        || !embeddedApiImpact->text().contains(QStringLiteral("Configuration"))) {
        return 12;
    }
    return 0;
}
