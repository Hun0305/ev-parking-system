#include "mainwindow.h"

#include "controllers/parkingcontroller.h"
#include "diagnostics/diagnosticsservice.h"
#include "dialogs/firealarmpopup.h"
#include "pages/dashboardpage.h"
#include "pages/debugpage.h"
#include "pages/evidencepage.h"
#include "pages/eventspage.h"
#include "pages/imagecomparepage.h"
#include "pages/ivasettingspage.h"
#include "pages/parkingmappage.h"
#include "pages/parkingroisettingspage.h"
#include "pages/settingspage.h"
#include "services/camerasettings.h"
#include "services/notificationcenter.h"
#include "simulation/parkingsimulationservice.h"
#include "iva/wiseaiconfigclient.h"


#include <QButtonGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString formatNotificationBrief(const NotificationRecord &notification)
{
    return QStringLiteral("[%1] %2 | %3 | %4")
        .arg(notification.severity,
             notification.sourceId,
             notification.eventType,
             notification.message);
}

QString formatNotificationDetail(const NotificationRecord &notification)
{
    return QStringLiteral(
        "Time: %1\nSource: %2\nSeverity: %3\nEvent: %4\nStatus: %5\nMessage: %6")
        .arg(notification.time,
             notification.sourceId,
             notification.severity,
             notification.eventType,
             notification.status,
             notification.message);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_cameraSettings(cameraConfigPath())
    , m_notificationCenter(new NotificationCenter(this))
{
    m_diagnosticsService = new DiagnosticsService(this);
    buildUi();
    WiseAiConnectionOptions wiseAiOptions;
    wiseAiOptions.baseUrl = QUrl(QStringLiteral("https://%1")
                                     .arg(m_cameraSettings.cameraIp()));
    wiseAiOptions.username = m_cameraSettings.cameraUsername();
    wiseAiOptions.password = m_cameraSettings.cameraPassword();
    wiseAiOptions.pinnedCertificateSha256 =
        m_cameraSettings.httpsCertificateSha256();
    m_wiseAiConfigClient = new WiseAiConfigClient(wiseAiOptions, this);
    m_parkingController = new ParkingController(clientConfigPath(), clientLocalConfigPath(), this);
    m_parkingSimulationService = new ParkingSimulationService(m_parkingController, this);
    connectPages();
    m_parkingController->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_parkingMapPage || !m_parkingMapPage->hasUnsavedLayoutChanges()) {
        QMainWindow::closeEvent(event);
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved parking map layout"),
        QStringLiteral("The parking map layout has unsaved changes.\n\nSave before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel) {
        event->ignore();
        return;
    }

    if (choice == QMessageBox::Save) {
        QString error;
        if (!m_parkingMapPage->saveLayoutNow(&error)) {
            QMessageBox::warning(this, QStringLiteral("Parking map layout"), error);
            event->ignore();
            return;
        }
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Smart Parking Integrated Monitoring System"));
    resize(1360, 860);
    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto *sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14, 18, 14, 18);
    sideLayout->setSpacing(8);
    auto *brand = new QLabel(QStringLiteral("Smart Parking"), sidebar);
    brand->setObjectName(QStringLiteral("brandLabel"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(16);

    m_pages = new QStackedWidget(central);
    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    auto addNavButton = [&](const QString &text, int index) {
        auto *button = new QPushButton(text, sidebar);
        button->setCheckable(true);
        button->setProperty("nav", true);
        navGroup->addButton(button, index);
        sideLayout->addWidget(button);
        connect(button, &QPushButton::clicked, m_pages,
                [this, index]() { m_pages->setCurrentIndex(index); });
        return button;
    };
    auto *dashboardButton = addNavButton(QStringLiteral("Dashboard"), 0);
    addNavButton(QStringLiteral("Parking Map"), 1);
    m_eventsNavButton = addNavButton(QStringLiteral("Events"), 2);
    m_evidenceNavButton = addNavButton(QStringLiteral("Evidence"), 3);
    m_imageCompareNavButton = addNavButton(
        QStringLiteral("Image Compare"), 4);
    addNavButton(QStringLiteral("Settings"), 5);
    addNavButton(QStringLiteral("IVA Setup"), 6);
    addNavButton(QStringLiteral("Parking ROI"), 7);
    addNavButton(QStringLiteral("Debug"), 8);
    connect(m_evidenceNavButton, &QPushButton::clicked, this, [this]() {
        if (m_evidencePage) {
            m_evidencePage->requestCurrentEvidence();
        }
    });
    connect(m_imageCompareNavButton, &QPushButton::clicked, this, [this]() {
        if (m_imageComparePage) {
            m_imageComparePage->requestCurrentComparison();
        }
    });
    sideLayout->addStretch();

    auto *contentWidget = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(16, 14, 16, 14);
    contentLayout->setSpacing(10);
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Smart Parking Integrated Monitoring System"), contentWidget);
    title->setObjectName(QStringLiteral("titleLabel"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    auto *alarmWidget = new QWidget(contentWidget);
    auto *alarmLayout = new QHBoxLayout(alarmWidget);
    alarmLayout->setContentsMargins(0, 0, 0, 0);
    alarmLayout->setSpacing(4);
    m_notificationButton = new QToolButton(alarmWidget);
    m_notificationButton->setText(QString::fromUtf8("\xF0\x9F\x94\x94"));
    m_notificationButton->setToolTip(QStringLiteral("Event notifications"));
    m_notificationButton->setFixedSize(36, 34);
    m_notificationButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: #eceff1; border: 1px solid #b0bec5; border-radius: 6px; font-size: 18px; }"
        "QToolButton:hover { background: #fff3e0; border-color: #ff9800; }"));
    m_notificationBadge = new QLabel(QStringLiteral("0"), alarmWidget);
    m_notificationBadge->setAlignment(Qt::AlignCenter);
    m_notificationBadge->setFixedSize(18, 18);
    m_notificationBadge->setStyleSheet(QStringLiteral(
        "QLabel { background: #d32f2f; color: white; border-radius: 9px; font-size: 12px; font-weight: 900; }"));
    alarmLayout->addWidget(m_notificationButton);
    alarmLayout->addWidget(m_notificationBadge);
    headerLayout->addWidget(alarmWidget);
    contentLayout->addLayout(headerLayout);
    connect(m_notificationButton, &QToolButton::clicked,
            this, &MainWindow::showNotificationPopup);
    connect(m_notificationCenter, &NotificationCenter::notificationsChanged,
            this, &MainWindow::updateNotificationIndicator);
    updateNotificationIndicator();

    m_alertBanner = new QLabel(QStringLiteral("System ready | Waiting for parking state"), contentWidget);
    m_alertBanner->setAlignment(Qt::AlignCenter);
    m_alertBanner->setMinimumHeight(34);
    contentLayout->addWidget(m_alertBanner);

    m_dashboardPage = new DashboardPage(m_cameraSettings.rtspUrls(QStringLiteral("profile3")),
                                        m_cameraSettings.rtspUrls(QStringLiteral("profile2")), m_pages);
    m_parkingMapPage = new ParkingMapPage(parkingMapLayoutPath(), m_pages);
    m_eventsPage = new EventsPage(m_pages);
    m_evidencePage = new EvidencePage(m_pages);
    m_imageComparePage = new ImageComparePage(m_pages);
    m_settingsPage = new SettingsPage(m_cameraSettings.configPath(),
                                      m_cameraSettings.cameraIp(),
                                      m_pages,
                                      m_cameraSettings.cameraUsername(),
                                      m_cameraSettings.cameraPassword());
    m_ivaSettingsPage = new IvaSettingsPage(m_cameraSettings.cameraIp(), m_pages);
    m_parkingRoiSettingsPage = new ParkingRoiSettingsPage(m_pages);
    m_debugPage = new DebugPage(m_pages);
    m_pages->addWidget(m_dashboardPage);
    m_pages->addWidget(m_parkingMapPage);
    m_pages->addWidget(m_eventsPage);
    m_pages->addWidget(m_evidencePage);
    m_pages->addWidget(m_imageComparePage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_ivaSettingsPage);
    m_pages->addWidget(m_parkingRoiSettingsPage);
    m_pages->addWidget(m_debugPage);
    contentLayout->addWidget(m_pages, 1);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(contentWidget, 1);
    setCentralWidget(central);
    dashboardButton->setChecked(true);
    m_pages->setCurrentIndex(0);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f5f7f9; }"
        "QFrame#sidebar { background: #1f2a33; }"
        "QLabel#brandLabel { color: white; font-size: 18px; font-weight: 800; }"
        "QLabel#titleLabel { font-size: 22px; font-weight: 700; color: #202124; }"
        "QGroupBox { font-weight: 700; border: 1px solid #c7cdd4; border-radius: 6px; margin-top: 8px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QTableWidget { background: white; gridline-color: #d8dde3; }"
        "QHeaderView::section { background: #263238; color: white; padding: 6px; border: none; }"
        "QPushButton { background: #263238; color: white; border: none; border-radius: 4px; padding: 9px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #37474f; }"
        "QPushButton[nav=\"true\"] { text-align: left; background: transparent; color: #dce5ea; padding: 11px 12px; }"
        "QPushButton[nav=\"true\"]:hover { background: #2e3c46; }"
        "QPushButton[nav=\"true\"]:checked { background: #406274; color: white; }"));
}

void MainWindow::connectPages()
{
    m_evidencePage->setImageLoader(m_parkingController->imageLoader());
    m_imageComparePage->setImageLoader(m_parkingController->imageLoader());
    connect(m_parkingController, &ParkingController::stateChanged,
            this, &MainWindow::renderParkingState);
    connect(m_parkingController, &ParkingController::bannerChanged, this,
            [this](const QString &message, bool hasAlert) {
                m_alertBanner->setText(message);
                m_alertBanner->setStyleSheet(hasAlert
                    ? QStringLiteral("background: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; border-radius: 4px; font-weight: 800;")
                    : QStringLiteral("background: #e8f5e9; color: #1b5e20; border: 1px solid #a5d6a7; border-radius: 4px; font-weight: 700;"));
            });
    connect(m_parkingController, &ParkingController::eventLogged, this,
            [this](const MonitoringEvent &event) {
                m_dashboardPage->prependEvent(event);
                m_eventsPage->appendEvent(event);
                m_notificationCenter->ingestEvent(event);
            });
    connect(m_parkingController, &ParkingController::eventLogged,
            m_diagnosticsService, &DiagnosticsService::ingestDomainEvent);
    connect(m_parkingController, &ParkingController::fireConfirmationRequested,
            this, &MainWindow::showFireAlarmPopup);
    connect(m_parkingController,
            &ParkingController::fireConfirmationRetryRequested,
            this, [this](const QString &channelId, const QString &alarmId) {
                QTimer::singleShot(0, this, [this, channelId, alarmId]() {
                    m_shownFireAlarmIds.remove(channelId);
                    showFireAlarmPopup(channelId, alarmId);
                });
            });
    connect(m_parkingController, &ParkingController::fireConfirmationClosed,
            this, &MainWindow::closeFireAlarmPopup);
    connect(m_parkingController, &ParkingController::apiDiagnosticChanged,
            m_diagnosticsService, &DiagnosticsService::setApiState);
    connect(m_dashboardPage, &DashboardPage::rtspDiagnosticsChanged,
            m_diagnosticsService, &DiagnosticsService::setRtspChannels);
    connect(m_dashboardPage, &DashboardPage::eventEvidenceRequested, this,
            [this](const QString &eventId) {
                showEventEvidencePage(eventId);
            });
    connect(m_parkingSimulationService, &ParkingSimulationService::simulationApplied,
            m_diagnosticsService, &DiagnosticsService::markSimulationApplied);
    connect(m_diagnosticsService, &DiagnosticsService::apiStateChanged,
            m_debugPage, &DebugPage::setApiDiagnostic);
    connect(m_diagnosticsService, &DiagnosticsService::rtspChannelsChanged,
            m_debugPage, &DebugPage::setRtspDiagnostics);
    connect(m_diagnosticsService, &DiagnosticsService::parkingStateChanged,
            m_debugPage, &DebugPage::setParkingDiagnostic);
    connect(m_diagnosticsService, &DiagnosticsService::logAdded,
            m_debugPage, &DebugPage::appendDiagnosticLog);
    connect(m_parkingMapPage, &ParkingMapPage::layoutSaveResult, this,
            [this](bool success, const QString &message) {
                m_alertBanner->setText(message);
                m_alertBanner->setStyleSheet(success
                    ? QStringLiteral("background: #e8f5e9; color: #1b5e20; border: 1px solid #a5d6a7; border-radius: 4px; font-weight: 700;")
                    : QStringLiteral("background: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; border-radius: 4px; font-weight: 800;"));
                m_parkingController->recordEvent(
                    QStringLiteral("PARKING_MAP"),
                    success ? QStringLiteral("LAYOUT_SAVED")
                            : QStringLiteral("LAYOUT_SAVE_FAILED"),
                    message,
                    success ? QStringLiteral("DONE") : QStringLiteral("FAILED"));
            });
    connect(m_parkingController, &ParkingController::statusMessageChanged,
            m_debugPage, &DebugPage::setLastMessage);
    connect(m_parkingController, &ParkingController::slotDetailReady,
            this, [this](const QString &slotId) {
                if (m_pages->currentWidget() == m_evidencePage) {
                    m_evidencePage->setImageLoader(
                        m_parkingController->imageLoader());
                    m_evidencePage->showEvidence(
                        slotId, m_parkingController->slotState(slotId),
                        m_parkingController->plateNumber(slotId),
                        m_parkingController->images(slotId));
                } else if (m_pages->currentWidget() == m_imageComparePage) {
                    m_imageComparePage->setImageLoader(
                        m_parkingController->imageLoader());
                    m_imageComparePage->showComparison(
                        slotId, m_parkingController->slotState(slotId),
                        m_parkingController->plateNumber(slotId),
                        m_parkingController->images(slotId));
                }
            });
    connect(m_parkingController, &ParkingController::slotDetailFailed,
            this, [this](const QString &slotId, const QString &message) {
                if (m_pages->currentWidget() == m_evidencePage) {
                    m_evidencePage->showError(slotId, message);
                } else if (m_pages->currentWidget() == m_imageComparePage) {
                    m_imageComparePage->showError(slotId, message);
                }
            });
    connect(m_parkingController, &ParkingController::eventEvidenceReady,
            this,
            [this](const QString &eventId, const QString &slotId,
                   qint64 sessionId, SlotState state,
                   const QString &plateNumber,
                   const QList<ParkingImageResource> &images) {
                m_evidencePage->setImageLoader(
                    m_parkingController->imageLoader());
                m_evidencePage->showEventEvidence(
                    eventId, slotId, sessionId, state, plateNumber, images);
            });
    connect(m_parkingController, &ParkingController::eventEvidenceFailed,
            this,
            [this](const QString &eventId, const QString &slotId,
                   const QString &message) {
                m_evidencePage->showEventError(eventId, slotId, message);
                m_alertBanner->setText(
                    QStringLiteral("Event evidence unavailable: %1")
                        .arg(message));
                m_alertBanner->setStyleSheet(QStringLiteral(
                    "background:#fff3e0;color:#e65100;border:1px solid #ffb74d;"
                    "border-radius:4px;font-weight:800;"));
            });
    connect(m_parkingController, &ParkingController::detailError, this,
            [this](const QString &message) {
                if (m_pages->currentWidget() == m_evidencePage) {
                    m_evidencePage->showError(QString(), message);
                } else if (m_pages->currentWidget() == m_imageComparePage) {
                    m_imageComparePage->showError(QString(), message);
                }
            });
    connect(m_evidencePage, &EvidencePage::slotEvidenceRequested, this,
            [this](const QString &slotId) {
                m_evidencePage->setImageLoader(m_parkingController->imageLoader());
                m_parkingController->requestSlotDetail(slotId);
            });
    connect(m_evidencePage, &EvidencePage::eventEvidenceRequested,
            m_parkingController, &ParkingController::requestEventEvidence);
    connect(m_imageComparePage, &ImageComparePage::comparisonRequested, this,
            [this](const QString &slotId) {
                m_imageComparePage->setImageLoader(
                    m_parkingController->imageLoader());
                m_parkingController->requestSlotDetail(slotId);
            });
    connect(m_debugPage, &DebugPage::clearAlarmsRequested,
            m_parkingController, &ParkingController::clearAlarms);
    connect(m_debugPage, &DebugPage::reconnectApiRequested,
            m_parkingController, &ParkingController::reconnectNow);
    connect(m_debugPage, &DebugPage::toggleMockEvRequested,
            m_parkingSimulationService, &ParkingSimulationService::toggleMockEv);
    connect(m_debugPage, &DebugPage::nonEvAlertRequested,
            m_parkingSimulationService, &ParkingSimulationService::triggerNonEvAlert);
    connect(m_debugPage, &DebugPage::overtimeAlertRequested,
            m_parkingSimulationService, &ParkingSimulationService::triggerOvertimeAlert);
    connect(m_debugPage, &DebugPage::sensorErrorRequested,
            m_parkingSimulationService, &ParkingSimulationService::triggerSensorError);
    connect(m_debugPage, &DebugPage::randomizeParkingRequested,
            m_parkingSimulationService, &ParkingSimulationService::randomizeParkingSlots);
    connect(m_debugPage, &DebugPage::sampleMessagesRequested,
            m_parkingSimulationService, &ParkingSimulationService::runSampleMessages);
    connect(m_debugPage, &DebugPage::manualMessageRequested,
            m_parkingSimulationService, &ParkingSimulationService::applyManualMessage);
    connect(m_eventsPage, &EventsPage::exportResult, this,
            [this](bool success, const QString &message) {
                m_parkingController->recordEvent(
                    QStringLiteral("SYSTEM"),
                    success ? QStringLiteral("EXPORT_CSV") : QStringLiteral("EXPORT_ERROR"),
                    message, success ? QStringLiteral("DONE") : QStringLiteral("FAILED"));
            });
    connect(m_eventsPage, &EventsPage::eventEvidenceRequested, this,
            [this](const QString &eventId) {
                showEventEvidencePage(eventId);
            });
    connect(m_settingsPage, &SettingsPage::saveCameraCredentialsRequested,
            this, &MainWindow::saveCameraCredentials);
    connect(m_settingsPage, &SettingsPage::saveServerBaseUrlRequested,
            m_parkingController, &ParkingController::updateServerBaseUrl);
    connect(m_settingsPage, &SettingsPage::reconnectServerRequested,
            m_parkingController, &ParkingController::reconnectNow);
    connect(m_settingsPage, &SettingsPage::overstayThresholdRefreshRequested,
            m_parkingController, &ParkingController::requestOverstayThreshold);
    connect(m_settingsPage, &SettingsPage::overstayThresholdUpdateRequested,
            m_parkingController, &ParkingController::updateOverstayThreshold);
    connect(m_parkingController, &ParkingController::serverBaseUrlChanged,
            m_settingsPage, &SettingsPage::setServerBaseUrl);
    connect(m_parkingController, &ParkingController::serverConnectionChanged,
            m_settingsPage, &SettingsPage::setServerConnectionStatus);
    connect(m_parkingController,
            &ParkingController::overstayThresholdRequestStarted,
            m_settingsPage, &SettingsPage::setOverstayThresholdRequestStarted);
    connect(m_parkingController, &ParkingController::overstayThresholdReceived,
            m_settingsPage, &SettingsPage::setOverstayThreshold);
    connect(m_parkingController,
            &ParkingController::overstayThresholdRequestFailed,
            m_settingsPage, &SettingsPage::setOverstayThresholdError);
    connect(m_parkingController, &ParkingController::serverConfigurationError, this,
            [this](const QString &message) {
                QMessageBox::warning(this, QStringLiteral("Server API"), message);
            });
    connect(m_ivaSettingsPage, &IvaSettingsPage::refreshRequested,
            m_wiseAiConfigClient, &WiseAiConfigClient::fetchConfiguration);
    connect(m_ivaSettingsPage, &IvaSettingsPage::applyRequested,
            m_wiseAiConfigClient,
            &WiseAiConfigClient::applyChannelConfiguration);
    connect(m_ivaSettingsPage, &IvaSettingsPage::deleteAreaRequested,
            m_wiseAiConfigClient, &WiseAiConfigClient::deleteArea);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::optionsReceived,
            m_ivaSettingsPage, &IvaSettingsPage::setOptions);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::capabilitiesReceived,
            m_ivaSettingsPage, &IvaSettingsPage::setCapabilities);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::configurationReceived,
            m_ivaSettingsPage, &IvaSettingsPage::setConfiguration);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::certificatePinned,
            this, [this](const QString &sha256) {
                QString errorMsg;
                m_cameraSettings.saveHttpsCertificateSha256(sha256, errorMsg);
            });
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::requestFailed,
            m_ivaSettingsPage, &IvaSettingsPage::setRequestError);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::applyStarted,
            m_ivaSettingsPage, &IvaSettingsPage::setApplyStarted);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::applySucceeded,
            m_ivaSettingsPage, &IvaSettingsPage::setApplySuccess);
    connect(m_wiseAiConfigClient, &WiseAiConfigClient::applyFailed,
            m_ivaSettingsPage, &IvaSettingsPage::setApplyError);
    connect(m_ivaSettingsPage, &IvaSettingsPage::previewFrameRequested,
            this, [this](int channel) {
        if (!m_dashboardPage || !m_ivaSettingsPage) return;
        m_ivaSettingsPage->setPreviewFrame(
            channel, m_dashboardPage->currentRtspFrame(channel));
    });
    connect(m_ivaSettingsPage, &IvaSettingsPage::piRoiSaveRequested,
            m_parkingController, &ParkingController::updateParkingRoi);
    connect(m_parkingController, &ParkingController::parkingRoiReceived,
            m_ivaSettingsPage, &IvaSettingsPage::setPiRoiResult);
    connect(m_parkingController, &ParkingController::parkingRoiRequestFailed,
            m_ivaSettingsPage, &IvaSettingsPage::setPiRoiError);
    connect(m_parkingRoiSettingsPage,
            &ParkingRoiSettingsPage::roiListRequested,
            m_parkingController, &ParkingController::requestParkingRois);
    connect(m_parkingRoiSettingsPage,
            &ParkingRoiSettingsPage::roiRequested,
            m_parkingController, &ParkingController::requestParkingRoi);
    connect(m_parkingRoiSettingsPage,
            &ParkingRoiSettingsPage::roiSaveRequested,
            m_parkingController, &ParkingController::updateParkingRoi);
    connect(m_parkingController, &ParkingController::parkingRoiListReceived,
            m_parkingRoiSettingsPage, &ParkingRoiSettingsPage::setRoiList);
    connect(m_parkingController, &ParkingController::parkingRoiReceived,
            m_parkingRoiSettingsPage, &ParkingRoiSettingsPage::setRoi);
    connect(m_parkingController, &ParkingController::parkingRoiRequestFailed,
            m_parkingRoiSettingsPage, &ParkingRoiSettingsPage::setRequestError);
    connect(m_parkingRoiSettingsPage,
            &ParkingRoiSettingsPage::previewFrameRequested,
            this, [this]() {
        if (!m_dashboardPage || !m_parkingRoiSettingsPage) return;
        m_parkingRoiSettingsPage->setPreviewFrame(
            m_dashboardPage->currentRtspFrame(0));
    });
}

void MainWindow::showFireAlarmPopup(const QString &channelId,
                                    const QString &alarmId)
{
    if (channelId.isEmpty() || alarmId.isEmpty()) return;
    if (m_shownFireAlarmIds.value(channelId) == alarmId) return;

    closeFireAlarmPopup(channelId, QString());
    m_shownFireAlarmIds.insert(channelId, alarmId);

    auto *popup = new FireAlarmPopup(channelId, alarmId, this);
    m_fireAlarmPopups.insert(channelId, popup);
    connect(popup, &FireAlarmPopup::checkRequested, this,
            [this](const QString &checkedChannelId, const QString &) {
                m_parkingController->acknowledgeFireAlarm(checkedChannelId);
            });
    connect(popup, &QObject::destroyed, this, [this, channelId, alarmId]() {
        if (m_shownFireAlarmIds.value(channelId) == alarmId) {
            m_fireAlarmPopups.remove(channelId);
        }
    });
    popup->show();
    popup->raise();
    popup->activateWindow();
}

void MainWindow::closeFireAlarmPopup(const QString &channelId,
                                     const QString &alarmId)
{
    if (!alarmId.isEmpty()
        && m_shownFireAlarmIds.value(channelId) != alarmId) return;
    if (FireAlarmPopup *popup = m_fireAlarmPopups.value(channelId)) {
        popup->dismiss();
    }
    m_fireAlarmPopups.remove(channelId);
}

void MainWindow::renderParkingState()
{
    const ParkingViewState &state = m_parkingController->state();
    m_parkingMapPage->render(state);
    m_evidencePage->render(state);
    m_imageComparePage->render(state);
    int occupied = 0;
    int vacant = 0;
    int sensorErrors = 0;
    for (const ParkingSlotInfo &slot : state.parkingSlots) {
        if (slot.visual.occupancy == SlotOccupancy::Occupied) ++occupied;
        else ++vacant;
        if (slot.visual.alarm == SlotAlarmKind::SensorError
            && !slot.visual.alarmAcknowledged) {
            ++sensorErrors;
        }
    }
    // EV 슬롯은 별도 맵(state.evSlots)에 저장된다. 이걸 빼먹으면 EV 슬롯의
    // 점유 상태가 다른 화면(Parking Map/Evidence)엔 반영되면서도 여기 요약
    // 패널에서만 영원히 카운트되지 않는다.
    for (const EvSlotInfo &slot : state.evSlots) {
        if (slot.visual.occupancy == SlotOccupancy::Occupied) ++occupied;
        else ++vacant;
        if (slot.visual.alarm == SlotAlarmKind::SensorError
            && !slot.visual.alarmAcknowledged) {
            ++sensorErrors;
        }
    }
    m_dashboardPage->setSummary(
        state.parkingSlots.size() + state.evSlots.size(),
        occupied, vacant, sensorErrors);
    QHash<QString, ChannelFireAlarmState> fireAlarms = state.fireAlarms;
    for (const QString &channelId : state.fireChannels) {
        if (!fireAlarms.contains(channelId)) {
            ChannelFireAlarmState legacyAlarm;
            legacyAlarm.active = true;
            legacyAlarm.alarmState = QStringLiteral("OPEN");
            legacyAlarm.ackState = QStringLiteral("unacked");
            fireAlarms.insert(channelId, legacyAlarm);
        }
    }
    m_dashboardPage->setFireAlarmStates(fireAlarms);
    if (m_diagnosticsService) {
        int activeAlarms = state.fireChannels.size();
        for (const EvSlotInfo &slot : state.evSlots) {
            if (slot.visual.alarm != SlotAlarmKind::None
                && !slot.visual.alarmAcknowledged) ++activeAlarms;
        }
        for (const ParkingSlotInfo &slot : state.parkingSlots) {
            if (slot.visual.alarm != SlotAlarmKind::None
                && !slot.visual.alarmAcknowledged) ++activeAlarms;
        }
        m_diagnosticsService->setParkingSummary(
            state.evSlots.size() + state.parkingSlots.size(), activeAlarms);
    }
}

void MainWindow::updateNotificationIndicator()
{
    const int unreadCount = m_notificationCenter ? m_notificationCenter->unreadCount() : 0;
    const bool hasNotifications = m_notificationCenter && m_notificationCenter->hasNotifications();
    if (m_notificationBadge) {
        m_notificationBadge->setVisible(unreadCount > 0);
        m_notificationBadge->setText(unreadCount > 9 ? QStringLiteral("9+") : QString::number(unreadCount));
    }
    if (m_notificationButton) {
        m_notificationButton->setToolTip(
            unreadCount > 0
                ? QStringLiteral("Event notifications (%1 unread)").arg(unreadCount)
                : QStringLiteral("Event notifications"));
        m_notificationButton->setStyleSheet(hasNotifications
            ? QStringLiteral(
                "QToolButton { background: #fff3e0; color: #b71c1c; border: 1px solid #fb8c00; border-radius: 6px; font-size: 18px; font-weight: 900; }"
                "QToolButton:hover { background: #ffe0b2; border-color: #ef6c00; }")
            : QStringLiteral(
                "QToolButton { background: #eceff1; border: 1px solid #b0bec5; border-radius: 6px; font-size: 18px; }"
                "QToolButton:hover { background: #fff3e0; border-color: #ff9800; }"));
    }
}

void MainWindow::showNotificationPopup()
{
    if (m_notificationPopup) {
        m_notificationPopup->close();
        return;
    }

    const QList<NotificationRecord> notifications =
        m_notificationCenter ? m_notificationCenter->notifications() : QList<NotificationRecord>();

    auto *popup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    m_notificationPopup = popup;
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName(QStringLiteral("notificationPopup"));
    popup->setFixedWidth(390);
    popup->setStyleSheet(QStringLiteral(
        "QFrame#notificationPopup { background: #ffffff; border: 1px solid #b0bec5; border-radius: 8px; }"
        "QLabel#notificationTitle { color: #17212b; font-size: 14px; font-weight: 800; }"
        "QLabel#notificationHint { color: #607d8b; font-size: 11px; }"
        "QLabel#notificationDetail { background: #f5f7f9; border: 1px solid #d8dde3; border-radius: 6px; color: #263238; padding: 8px; }"
        "QListWidget { background: #ffffff; border: 1px solid #d8dde3; border-radius: 6px; outline: 0; }"
        "QListWidget::item { border-bottom: 1px solid #eceff1; padding: 7px; }"
        "QListWidget::item:selected { background: #fff3e0; color: #17212b; }"
        "QPushButton { background: #263238; color: white; border: none; border-radius: 4px; padding: 7px 10px; font-weight: 700; }"
        "QPushButton:hover { background: #37474f; }"
        "QPushButton:disabled { background: #cfd8dc; color: #78909c; }"));
    connect(popup, &QObject::destroyed, this, [this]() {
        m_notificationPopup = nullptr;
    });

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    auto *headerLayout = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Event notifications"), popup);
    title->setObjectName(QStringLiteral("notificationTitle"));
    auto *countLabel = new QLabel(
        notifications.isEmpty()
            ? QStringLiteral("No open alerts")
            : QStringLiteral("%1 open").arg(notifications.size()),
        popup);
    countLabel->setObjectName(QStringLiteral("notificationHint"));
    countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(countLabel);
    layout->addLayout(headerLayout);

    auto *list = new QListWidget(popup);
    list->setFixedHeight(notifications.isEmpty() ? 72 : qMin(250, qMax(88, notifications.size() * 58)));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list);

    auto *detailLabel = new QLabel(popup);
    detailLabel->setObjectName(QStringLiteral("notificationDetail"));
    detailLabel->setWordWrap(true);
    detailLabel->setMinimumHeight(118);
    detailLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(detailLabel);

    auto *evidenceButton = new QPushButton(QStringLiteral("View evidence"), popup);
    evidenceButton->setObjectName(QStringLiteral("notificationEvidenceButton"));
    evidenceButton->setEnabled(false);

    if (notifications.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("No important notifications are currently open."), list);
        item->setFlags(Qt::NoItemFlags);
        detailLabel->setText(QStringLiteral("All important events are clear. Use Events for the complete event log."));
    } else {
        for (int i = 0; i < notifications.size(); ++i) {
            const NotificationRecord &notification = notifications.at(i);
            const QString newPrefix = notification.unread ? QStringLiteral("[NEW] ") : QString();
            auto *item = new QListWidgetItem(
                newPrefix + QStringLiteral("%1 | %2\n%3")
                    .arg(notification.severity,
                         notification.sourceId,
                         notification.title.isEmpty() ? notification.eventType : notification.title),
                list);
            item->setData(Qt::UserRole, i);
            item->setSizeHint(QSize(0, 56));
        }
        auto updateNotificationSelection =
            [this, detailLabel, evidenceButton, notifications](QListWidgetItem *item) {
                    if (!item) return;
                    const int index = item->data(Qt::UserRole).toInt();
                    if (index < 0 || index >= notifications.size()) return;
                    const NotificationRecord &notification = notifications.at(index);
                    detailLabel->setText(formatNotificationDetail(notification));
                    const bool available = m_parkingController
                        && m_parkingController->hasEventEvidence(
                            notification.eventId);
                    evidenceButton->setEnabled(available);
                    evidenceButton->setToolTip(
                        available
                            ? QStringLiteral("Open %1 evidence").arg(notification.sourceId)
                            : QStringLiteral("This notification is not mapped to a parking slot"));
                };
        connect(list, &QListWidget::currentItemChanged, popup,
                [updateNotificationSelection](QListWidgetItem *current,
                                              QListWidgetItem *) {
                    updateNotificationSelection(current);
                });
        list->setCurrentRow(0);
        updateNotificationSelection(list->currentItem());
    }

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    auto *eventsButton = new QPushButton(QStringLiteral("View all events"), popup);
    auto *closeButton = new QPushButton(QStringLiteral("Close"), popup);
    buttonLayout->addWidget(evidenceButton);
    buttonLayout->addWidget(eventsButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);

    connect(eventsButton, &QPushButton::clicked, this, [this, popup]() {
        popup->close();
        showEventsPage();
    });
    connect(evidenceButton, &QPushButton::clicked, this,
            [this, popup, list, notifications]() {
                const QListWidgetItem *item = list->currentItem();
                if (!item) return;
                const int index = item->data(Qt::UserRole).toInt();
                if (index < 0 || index >= notifications.size()) return;
                const QString eventId = notifications.at(index).eventId;
                popup->close();
                showEventEvidencePage(eventId);
            });
    connect(closeButton, &QPushButton::clicked, popup, &QFrame::close);

    popup->adjustSize();
    const QPoint buttonBottomRight =
        m_notificationButton ? m_notificationButton->mapToGlobal(m_notificationButton->rect().bottomRight())
                             : mapToGlobal(rect().topRight());
    popup->move(buttonBottomRight.x() - popup->width() + 1,
                buttonBottomRight.y() + 8);
    popup->show();
    if (m_notificationCenter) {
        m_notificationCenter->markAllRead();
    }
}

void MainWindow::showEventsPage()
{
    if (m_pages) {
        m_pages->setCurrentIndex(2);
    }
    if (m_eventsNavButton) {
        m_eventsNavButton->setChecked(true);
    }
}

bool MainWindow::showEventEvidencePage(const QString &rawEventId)
{
    const QString eventId = rawEventId.trimmed();
    if (!m_pages || !m_evidencePage || !m_parkingController
        || eventId.isEmpty()) {
        return false;
    }
    if (!m_parkingController->hasEventEvidence(eventId)) {
        m_alertBanner->setText(
            QStringLiteral("Event %1 is not linked to parking evidence.")
                .arg(eventId));
        m_alertBanner->setStyleSheet(QStringLiteral(
            "background:#fff3e0;color:#e65100;border:1px solid #ffb74d;"
            "border-radius:4px;font-weight:800;"));
        return false;
    }
    const QString slotId = m_parkingController->eventEvidenceSlotId(eventId);
    m_evidencePage->openEvent(eventId, slotId);
    m_pages->setCurrentWidget(m_evidencePage);
    if (m_evidenceNavButton) {
        m_evidenceNavButton->setChecked(true);
    }
    m_parkingController->requestEventEvidence(eventId);
    return true;
}

QString MainWindow::cameraConfigPath() const
{
#ifdef SMART_PARKING_CONFIG_DIR
    return QDir(QStringLiteral(SMART_PARKING_CONFIG_DIR))
        .absoluteFilePath(QStringLiteral("camera_config.ini"));
#else
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../config/camera_config.ini"));
#endif
}

QString MainWindow::clientConfigPath() const
{
#ifdef SMART_PARKING_CONFIG_DIR
    return QDir(QStringLiteral(SMART_PARKING_CONFIG_DIR))
        .absoluteFilePath(QStringLiteral("client_config.ini"));
#else
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../config/client_config.ini"));
#endif
}

QString MainWindow::clientLocalConfigPath() const
{
#ifdef SMART_PARKING_CONFIG_DIR
    return QDir(QStringLiteral(SMART_PARKING_CONFIG_DIR))
        .absoluteFilePath(QStringLiteral("client_config.local.ini"));
#else
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../config/client_config.local.ini"));
#endif
}

QString MainWindow::parkingMapLayoutPath() const
{
#ifdef SMART_PARKING_CONFIG_DIR
    return QDir(QStringLiteral(SMART_PARKING_CONFIG_DIR))
        .absoluteFilePath(QStringLiteral("parking_map_layout.local.json"));
#else
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../config/parking_map_layout.local.json"));
#endif
}

void MainWindow::saveCameraCredentials(const QString &cameraIpText,
                                       const QString &username,
                                       const QString &password)
{
    QString newIp;
    QString errorMessage;
    if (!m_cameraSettings.saveCameraCredentials(cameraIpText, username, password,
                                                newIp, errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("Camera settings"), errorMessage);
        return;
    }
    m_settingsPage->setCameraIp(newIp);
    m_settingsPage->setCameraCredentials(username.trimmed(), password);
    m_ivaSettingsPage->setCameraIp(newIp);
    WiseAiConnectionOptions wiseAiOptions;
    wiseAiOptions.baseUrl = QUrl(QStringLiteral("https://%1").arg(newIp));
    wiseAiOptions.username = m_cameraSettings.cameraUsername();
    wiseAiOptions.password = m_cameraSettings.cameraPassword();
    wiseAiOptions.pinnedCertificateSha256 =
        m_cameraSettings.httpsCertificateSha256();
    m_wiseAiConfigClient->setConnectionOptions(wiseAiOptions);
    m_dashboardPage->setRtspUrls(
        m_cameraSettings.rtspUrls(QStringLiteral("profile3")),
        m_cameraSettings.rtspUrls(QStringLiteral("profile2")));
    m_wiseAiConfigClient->fetchConfiguration();
    m_parkingController->recordEvent(
        QStringLiteral("SYSTEM"), QStringLiteral("CAMERA_SETTINGS_UPDATED"),
        QStringLiteral("Camera connection settings updated for ") + newIp,
        QStringLiteral("DONE"));
}
