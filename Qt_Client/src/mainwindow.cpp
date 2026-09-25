#include "mainwindow.h"

#include "auth/authsession.h"
#include "controllers/parkingcontroller.h"
#include "diagnostics/diagnosticsservice.h"
#include "dialogs/firealarmpopup.h"
#include "pages/dashboardpage.h"
#include "pages/debugpage.h"
#include "pages/evidencepage.h"
#include "pages/eventspage.h"
#include "pages/ivasettingspage.h"
#include "pages/parkingmappage.h"
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
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
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

MainWindow::MainWindow(const AuthSession &authSession, QWidget *parent)
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
    m_parkingController->setBearerAuthentication(authSession.serverOrigin,
                                                 authSession.accessToken);
    m_parkingSimulationService = new ParkingSimulationService(m_parkingController, this);
    connectPages();
    m_parkingController->start();
}

bool MainWindow::prepareForReauthentication()
{
    if (!m_parkingMapPage || !m_parkingMapPage->hasUnsavedLayoutChanges()) {
        return true;
    }

    for (;;) {
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this,
            QStringLiteral("Session ended"),
            QStringLiteral(
                "Your session ended and you need to sign in again.\n\n"
                "The parking map layout has unsaved changes. Save them before signing in again?"),
            QMessageBox::Save | QMessageBox::Discard,
            QMessageBox::Save);

        if (choice == QMessageBox::Discard) {
            return true;
        }
        if (choice != QMessageBox::Save) {
            continue;
        }

        QString error;
        if (m_parkingMapPage->saveLayoutNow(&error)) {
            return true;
        }

        const QMessageBox::StandardButton failureChoice = QMessageBox::warning(
            this,
            QStringLiteral("Parking map layout"),
            QStringLiteral("%1\n\nRetry saving, or discard the changes and sign in again.")
                .arg(error),
            QMessageBox::Retry | QMessageBox::Discard,
            QMessageBox::Retry);
        if (failureChoice == QMessageBox::Discard) {
            return true;
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_closeAfterIvaSave && m_ivaSettingsPage
        && m_ivaSettingsPage->hasPendingChanges()) {
        event->ignore();
        const IvaSettingsPage::PendingChangesDecision decision =
            m_ivaSettingsPage->confirmPendingChanges();
        if (decision == IvaSettingsPage::PendingChangesDecision::Waiting) {
            m_closeAfterIvaSave = true;
        } else if (decision == IvaSettingsPage::PendingChangesDecision::Proceed) {
            closeEvent(event);
        }
        return;
    }

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

void MainWindow::handlePageChanged(int index)
{
    if (m_pageTransitionGuard || !m_pages) return;
    const int previousIndex = m_lastPageIndex;
    m_lastPageIndex = index;
    const int ivaIndex = m_pages->indexOf(m_ivaSettingsPage);
    if (previousIndex != ivaIndex || index == ivaIndex
        || !m_ivaSettingsPage || !m_ivaSettingsPage->hasPendingChanges()) {
        return;
    }

    m_pageTransitionGuard = true;
    m_pages->setCurrentIndex(previousIndex);
    m_pageTransitionGuard = false;
    m_lastPageIndex = previousIndex;
    m_pendingPageIndex = index;

    const IvaSettingsPage::PendingChangesDecision decision =
        m_ivaSettingsPage->confirmPendingChanges();
    if (decision == IvaSettingsPage::PendingChangesDecision::Proceed) {
        finishPendingIvaNavigation();
    } else if (decision == IvaSettingsPage::PendingChangesDecision::Cancel) {
        m_pendingPageIndex = -1;
    }
}

void MainWindow::finishPendingIvaNavigation()
{
    if (m_closeAfterIvaSave) {
        m_closeAfterIvaSave = false;
        close();
        return;
    }
    if (!m_pages || m_pendingPageIndex < 0) return;
    const int targetIndex = m_pendingPageIndex;
    m_pendingPageIndex = -1;
    m_pageTransitionGuard = true;
    m_pages->setCurrentIndex(targetIndex);
    m_pageTransitionGuard = false;
    m_lastPageIndex = targetIndex;
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Smart Parking"));
    resize(1440, 900);
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

    auto *sideHeader = new QHBoxLayout;
    sideHeader->setContentsMargins(0, 0, 0, 0);
    sideHeader->setSpacing(6);
    auto *brand = new QLabel(QStringLiteral("Smart Parking"), sidebar);
    brand->setObjectName(QStringLiteral("brandLabel"));
    sideHeader->addWidget(brand, 1);
    auto *sidebarToggle = new QToolButton(sidebar);
    sidebarToggle->setObjectName(QStringLiteral("sidebarToggle"));
    sidebarToggle->setText(QStringLiteral("◀"));
    sidebarToggle->setToolTip(QStringLiteral("페이지 메뉴 접기"));
    sidebarToggle->setFixedSize(30, 30);
    sideHeader->addWidget(sidebarToggle);
    sideLayout->addLayout(sideHeader);
    sideLayout->addSpacing(16);

    m_pages = new QStackedWidget(central);
    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    QList<QPushButton *> navButtons;
    auto addNavButton = [&](const QString &text, int index) {
        auto *button = new QPushButton(text, sidebar);
        button->setCheckable(true);
        button->setProperty("nav", true);
        const QString iconPath = index == 0
            ? QStringLiteral(":/resources/icons/dashboard.svg")
            : index == 1
                ? QStringLiteral(":/resources/icons/parking-map.svg")
                : index == 2
                    ? QStringLiteral(":/resources/icons/events.svg")
                    : index == 3
                        ? QStringLiteral(":/resources/icons/evidence.svg")
                                : index == 4
                                    ? QStringLiteral(":/resources/icons/settings.svg")
                                    : index == 5
                                        ? QStringLiteral(":/resources/icons/iva-setup.svg")
                                        : QStringLiteral(":/resources/icons/debug.svg");
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(20, 20));
        button->setProperty("navFullText", text);
        button->setProperty("navCollapsed", false);
        button->setToolTip(text);
        navGroup->addButton(button, index);
        navButtons.append(button);
        sideLayout->addWidget(button);
        connect(button, &QPushButton::clicked, m_pages,
                [this, index]() { m_pages->setCurrentIndex(index); });
        return button;
    };
    m_dashboardNavButton = addNavButton(QStringLiteral("Dashboard"), 0);
    addNavButton(QStringLiteral("Parking Map"), 1);
    m_eventsNavButton = addNavButton(QStringLiteral("Events"), 2);
    m_evidenceNavButton = addNavButton(QStringLiteral("Evidence"), 3);
    addNavButton(QStringLiteral("Settings"), 4);
    m_ivaNavButton = addNavButton(QStringLiteral("IVA Setup"), 5);
    addNavButton(QStringLiteral("Debug"), 6);
    connect(m_evidenceNavButton, &QPushButton::clicked, this, [this]() {
        if (m_evidencePage) {
            m_evidencePage->requestCurrentEvidence();
        }
    });
    sideLayout->addStretch();

    connect(sidebarToggle, &QToolButton::clicked, this,
            [sidebar, brand, sidebarToggle, navButtons]() {
                const bool collapse = sidebar->width() > 100;
                sidebar->setFixedWidth(collapse ? 64 : 190);
                brand->setVisible(!collapse);
                sidebarToggle->setText(collapse ? QStringLiteral("▶")
                                                 : QStringLiteral("◀"));
                sidebarToggle->setToolTip(
                    collapse ? QStringLiteral("페이지 메뉴 펼치기")
                             : QStringLiteral("페이지 메뉴 접기"));
                for (QPushButton *button : navButtons) {
                    button->setText(collapse
                                        ? QString()
                                        : button->property("navFullText").toString());
                    button->setProperty("navCollapsed", collapse);
                    button->style()->unpolish(button);
                    button->style()->polish(button);
                }
            });

    auto *contentWidget = new QWidget(central);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(16, 14, 16, 14);
    contentLayout->setSpacing(10);
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);
    m_monitorStatusButton = new QToolButton(contentWidget);
    m_monitorStatusButton->setObjectName(QStringLiteral("monitorStatusIndicator"));
    m_monitorStatusButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_monitorStatusButton->setIconSize(QSize(22, 22));
    m_monitorStatusButton->setFixedSize(42, 34);
    m_monitorStatusButton->setAccessibleName(QStringLiteral("Monitoring connection status"));
    headerLayout->addWidget(m_monitorStatusButton);
    updateMonitorStatus(QStringLiteral("Waiting for server connection"), false);

    m_alertBanner = new QLabel(QString(), contentWidget);
    m_alertBanner->setObjectName(QStringLiteral("headerStatusLabel"));
    m_alertBanner->setAlignment(Qt::AlignCenter);
    m_alertBanner->setFixedSize(360, 34);
    m_alertBanner->setMargin(6);
    m_alertBanner->setToolTip(QStringLiteral("No active alerts"));
    m_alertBanner->setStyleSheet(QStringLiteral(
        "background: transparent; color: transparent; "
        "border: 1px solid transparent; border-radius: 4px;"));
    headerLayout->addWidget(m_alertBanner);
    headerLayout->addStretch();

    m_pageHelpStack = new QStackedWidget(contentWidget);
    m_pageHelpStack->setObjectName(QStringLiteral("pageHelpStack"));
    m_pageHelpStack->setFixedSize(112, 34);
    headerLayout->addWidget(m_pageHelpStack);

    auto *alarmWidget = new QWidget(contentWidget);
    alarmWidget->setObjectName(QStringLiteral("notificationIndicator"));
    alarmWidget->setFixedSize(62, 34);
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

    m_dashboardPage = new DashboardPage(m_cameraSettings.rtspUrls(QStringLiteral("profile3")),
                                        m_cameraSettings.rtspUrls(QStringLiteral("profile2")), m_pages);
    m_parkingMapPage = new ParkingMapPage(parkingMapLayoutPath(), m_pages);
    m_eventsPage = new EventsPage(m_pages);
    m_evidencePage = new EvidencePage(m_pages);
    m_settingsPage = new SettingsPage(m_cameraSettings.configPath(),
                                      m_cameraSettings.cameraIp(),
                                      m_pages,
                                      m_cameraSettings.cameraUsername(),
                                      m_cameraSettings.cameraPassword());
    m_ivaSettingsPage = new IvaSettingsPage(m_cameraSettings.cameraIp(), m_pages);
    m_ivaSettingsPage->setParkingZoneMappings(m_parkingMapPage->parkingZoneMappings());
    m_debugPage = new DebugPage(m_pages);
    m_pages->addWidget(m_dashboardPage);
    m_pages->addWidget(m_parkingMapPage);
    m_pages->addWidget(m_eventsPage);
    m_pages->addWidget(m_evidencePage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_ivaSettingsPage);
    m_pages->addWidget(m_debugPage);
    installPageHelpButtons();
    contentLayout->addWidget(m_pages, 1);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(contentWidget, 1);
    setCentralWidget(central);
    m_dashboardNavButton->setChecked(true);
    m_pages->setCurrentIndex(0);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f5f7f9; }"
        "QFrame#sidebar { background: #1f2a33; }"
        "QLabel#brandLabel { color: white; font-size: 18px; font-weight: 800; }"
        "QToolButton#sidebarToggle { background: #263640; color: #dce5ea; border: 1px solid #455a64; border-radius: 5px; font-weight: 800; }"
        "QToolButton#sidebarToggle:hover { background: #37474f; color: white; }"
        "QGroupBox { font-weight: 700; border: 1px solid #c7cdd4; border-radius: 6px; margin-top: 8px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QTableWidget { background: white; gridline-color: #d8dde3; }"
        "QHeaderView::section { background: #263238; color: white; padding: 6px; border: none; }"
        "QPushButton { background: #263238; color: white; border: none; border-radius: 4px; padding: 9px 14px; font-weight: 600; }"
        "QPushButton:hover { background: #37474f; }"
        "QPushButton[nav=\"true\"] { text-align: left; background: transparent; color: #dce5ea; padding: 11px 12px; }"
        "QPushButton[nav=\"true\"][navCollapsed=\"true\"] { text-align: center; padding: 11px 4px; }"
        "QPushButton[nav=\"true\"]:hover { background: #2e3c46; }"
        "QPushButton[nav=\"true\"]:checked { background: #406274; color: white; }"));
}

void MainWindow::installPageHelpButtons()
{
    if (!m_pages || !m_pageHelpStack) return;

    const QList<QWidget *> pages = {
        m_dashboardPage,
        m_parkingMapPage,
        m_eventsPage,
        m_evidencePage,
        m_settingsPage,
        m_ivaSettingsPage,
        m_debugPage,
    };
    const QStringList buttonNames = {
        QStringLiteral("dashboardHelpButton"),
        QStringLiteral("parkingMapHelpButton"),
        QStringLiteral("eventsHelpButton"),
        QStringLiteral("evidenceHelpButton"),
        QStringLiteral("settingsHelpButton"),
        QStringLiteral("ivaHelpButton"),
        QStringLiteral("debugHelpButton"),
    };

    for (int index = 0; index < pages.size(); ++index) {
        QPushButton *button = pages.at(index)->findChild<QPushButton *>(
            buttonNames.at(index));
        if (!button) {
            button = new QPushButton(QStringLiteral("도움말"), m_pageHelpStack);
            button->setEnabled(false);
        }
        button->setText(QStringLiteral("도움말"));
        button->setFixedSize(112, 34);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background:#263238; color:white; border:1px solid #455a64; "
            "border-radius:6px; padding:6px 10px; font-weight:800; }"
            "QPushButton:hover { background:#37474f; border-color:#fb8c00; }"
            "QPushButton:pressed { background:#1c252a; }"));
        m_pageHelpStack->addWidget(button);
    }

    connect(m_pages, &QStackedWidget::currentChanged,
            m_pageHelpStack, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged,
            this, &MainWindow::handlePageChanged);
    m_pageHelpStack->setCurrentIndex(m_pages->currentIndex());
}

void MainWindow::connectPages()
{
    m_evidencePage->setImageLoader(m_parkingController->imageLoader());
    connect(m_parkingController, &ParkingController::authenticationExpired,
            this, &MainWindow::reauthenticationRequested);
    m_parkingMapPage->setImageLoader(m_parkingController->imageLoader());
    connect(m_parkingController, &ParkingController::imageLoaderChanged,
            m_parkingMapPage, &ParkingMapPage::setImageLoader);
    connect(m_parkingMapPage, &ParkingMapPage::slotDetailRequested,
            m_parkingController, &ParkingController::requestSlotDetail);
    connect(m_parkingController, &ParkingController::stateChanged,
            this, &MainWindow::renderParkingState);
    connect(m_parkingController, &ParkingController::bannerChanged, this,
            [this](const QString &message, bool hasAlert) {
                if (!hasAlert) {
                    m_alertBanner->clear();
                    m_alertBanner->setToolTip(QStringLiteral("No active alerts"));
                    m_alertBanner->setStyleSheet(QStringLiteral(
                        "background: transparent; color: transparent; "
                        "border: 1px solid transparent; border-radius: 4px;"));
                    return;
                }

                m_alertBanner->setText(message);
                m_alertBanner->setToolTip(message);
                m_alertBanner->setStyleSheet(QStringLiteral(
                    "background: #ffebee; color: #b71c1c; border: 1px solid #ef9a9a; "
                    "border-radius: 4px; font-weight: 800;"));
            });
    connect(m_parkingController, &ParkingController::eventLogged, this,
            [this](const MonitoringEvent &event) {
                m_dashboardPage->prependEvent(event);
                m_eventsPage->appendEvent(event);
                m_parkingMapPage->appendEvent(event);
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
    connect(m_dashboardPage, &DashboardPage::recentEventsDetailRequested,
            this, &MainWindow::showEventsPage);
    connect(m_parkingSimulationService, &ParkingSimulationService::simulationApplied,
            m_diagnosticsService, &DiagnosticsService::markSimulationApplied);
    connect(m_diagnosticsService, &DiagnosticsService::apiStateChanged,
            m_debugPage, &DebugPage::setApiDiagnostic);
    connect(m_diagnosticsService, &DiagnosticsService::apiStateChanged,
            m_eventsPage, [this](const ApiDiagnosticState &state) {
                m_eventsPage->setServerState(state.status, state.lastError);
            });
    connect(m_diagnosticsService, &DiagnosticsService::rtspChannelsChanged,
            m_debugPage, &DebugPage::setRtspDiagnostics);
    connect(m_diagnosticsService, &DiagnosticsService::parkingStateChanged,
            m_debugPage, &DebugPage::setParkingDiagnostic);
    connect(m_diagnosticsService, &DiagnosticsService::logAdded,
            m_debugPage, &DebugPage::appendDiagnosticLog);
    connect(m_parkingMapPage, &ParkingMapPage::layoutSaveResult, this,
            [this](bool success, const QString &message) {
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
                }
            });
    connect(m_parkingController, &ParkingController::slotDetailFailed,
            this, [this](const QString &slotId, const QString &message) {
                if (m_pages->currentWidget() == m_evidencePage) {
                    m_evidencePage->showError(slotId, message);
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
            });
    connect(m_parkingMapPage, &ParkingMapPage::eventsRequested, this,
            [this](const QString &, const QString &) { showEventsPage(); });
    connect(m_parkingMapPage, &ParkingMapPage::evidenceRequested, this,
            [this](const QString &zoneId, const QString &eventId) {
                if (!eventId.trimmed().isEmpty()
                    && m_parkingController->hasEventEvidence(eventId)) {
                    showEventEvidencePage(eventId);
                    return;
                }
                m_pages->setCurrentWidget(m_evidencePage);
                if (m_evidenceNavButton) m_evidenceNavButton->setChecked(true);
                if (m_evidencePage->selectSlot(zoneId)) {
                    m_evidencePage->requestCurrentEvidence();
                }
            });
    connect(m_parkingMapPage, &ParkingMapPage::cameraRequested, this,
            [this](const QString &channel) {
                m_pages->setCurrentWidget(m_dashboardPage);
                if (m_dashboardNavButton) m_dashboardNavButton->setChecked(true);
                m_dashboardPage->showExpandedChannel(channel);
            });
    connect(m_parkingMapPage, &ParkingMapPage::ivaSettingsRequested, this,
            [this](const QString &zoneId,
                   const QString &cameraChannel,
                   const QString &ivaAreaId) {
                m_ivaSettingsPage->setParkingZoneMappings(
                    m_parkingMapPage->parkingZoneMappings());
                m_ivaSettingsPage->setParkingSelection(zoneId,
                                                        cameraChannel,
                                                        ivaAreaId);
                m_pages->setCurrentWidget(m_ivaSettingsPage);
                if (m_ivaNavButton) m_ivaNavButton->setChecked(true);
            });
    connect(m_parkingController, &ParkingController::detailError, this,
            [this](const QString &message) {
                if (m_pages->currentWidget() == m_evidencePage) {
                    m_evidencePage->showError(QString(), message);
                }
            });
    connect(m_evidencePage, &EvidencePage::slotEvidenceRequested, this,
            [this](const QString &slotId) {
                m_evidencePage->setImageLoader(m_parkingController->imageLoader());
                m_parkingController->requestSlotDetail(slotId);
            });
    connect(m_evidencePage, &EvidencePage::eventEvidenceRequested,
            m_parkingController, &ParkingController::requestEventEvidence);
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
    connect(m_parkingController, &ParkingController::serverConnectionChanged,
            this, &MainWindow::updateMonitorStatus);
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
    connect(m_ivaSettingsPage, &IvaSettingsPage::pendingChangesSaved,
            this, &MainWindow::finishPendingIvaNavigation);
    connect(m_parkingController, &ParkingController::parkingRoiReceived,
            m_ivaSettingsPage, &IvaSettingsPage::setPiRoiResult);
    connect(m_parkingController, &ParkingController::parkingRoiRequestFailed,
            m_ivaSettingsPage, &IvaSettingsPage::setPiRoiError);
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
        state.hasServerSnapshot ? state.serverSlotCount : 0,
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
        m_notificationBadge->setText(unreadCount > 9 ? QStringLiteral("9+") : QString::number(unreadCount));
        m_notificationBadge->setStyleSheet(unreadCount > 0
            ? QStringLiteral(
                "QLabel { background: #d32f2f; color: white; border-radius: 9px; "
                "font-size: 12px; font-weight: 900; }")
            : QStringLiteral(
                "QLabel { background: #cfd8dc; color: #546e7a; border-radius: 9px; "
                "font-size: 12px; font-weight: 800; }"));
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
        m_evidencePage->openEvent(eventId, QString());
        m_evidencePage->showEventError(
            eventId, QString(),
            QStringLiteral("This event is not linked to parking evidence."));
        m_pages->setCurrentWidget(m_evidencePage);
        if (m_evidenceNavButton) {
            m_evidenceNavButton->setChecked(true);
        }
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

void MainWindow::updateMonitorStatus(const QString &status, bool connected)
{
    if (!m_monitorStatusButton) return;

    const QString normalized = status.trimmed().toUpper();
    const bool connecting = normalized.contains(QStringLiteral("CONNECT"))
        || normalized.contains(QStringLiteral("RETRY"));
    const QString iconPath = connected
        ? QStringLiteral(":/resources/icons/monitor-connected.svg")
        : connecting
            ? QStringLiteral(":/resources/icons/monitor-connecting.svg")
            : QStringLiteral(":/resources/icons/monitor-disconnected.svg");
    const QString accent = connected
        ? QStringLiteral("#2e7d32")
        : connecting
            ? QStringLiteral("#f9a825")
            : QStringLiteral("#c62828");
    const QString background = connected
        ? QStringLiteral("#e8f5e9")
        : connecting
            ? QStringLiteral("#fff8e1")
            : QStringLiteral("#ffebee");

    m_monitorStatusButton->setIcon(QIcon(iconPath));
    m_monitorStatusButton->setToolTip(
        QStringLiteral("Monitoring connection: %1").arg(status));
    m_monitorStatusButton->setStyleSheet(QStringLiteral(
        "QToolButton { background:%1; border:1px solid %2; border-radius:6px; }"
        "QToolButton:hover { background:%3; }")
            .arg(background, accent,
                 connected ? QStringLiteral("#c8e6c9")
                           : connecting ? QStringLiteral("#ffecb3")
                                        : QStringLiteral("#ffcdd2")));
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
