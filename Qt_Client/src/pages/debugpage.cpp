#include "debugpage.h"

#include "pages/systemuistyle.h"
#include "widgets/pagehelp.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString valueStyle(const QString &status)
{
    const QString normalized = status.trimmed().toUpper();
    const QString color = normalized == QStringLiteral("CONNECTED")
            || normalized == QStringLiteral("PLAYING")
            || normalized == QStringLiteral("SERVER")
        ? QStringLiteral("#1b5e20")
        : normalized == QStringLiteral("CONNECTING")
              || normalized == QStringLiteral("RETRYING")
              || normalized == QStringLiteral("MIXED")
          ? QStringLiteral("#e65100") : QStringLiteral("#b71c1c");
    return QStringLiteral("color:%1;font-size:18px;font-weight:800;").arg(color);
}

QString timeText(const QDateTime &value)
{
    return value.isValid() ? value.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                           : QStringLiteral("-");
}

QString frameAgeText(qint64 frameWallClockMs)
{
    if (frameWallClockMs <= 0) return QStringLiteral("-");
    const qint64 ageMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - frameWallClockMs);
    if (ageMs < 1000) return QStringLiteral("%1 ms").arg(ageMs);
    return QStringLiteral("%1 s").arg(ageMs / 1000.0, 0, 'f', 1);
}

qint64 frameAgeMs(qint64 frameWallClockMs)
{
    if (frameWallClockMs <= 0) return -1;
    return qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - frameWallClockMs);
}

QString effectiveStreamStatus(const RtspChannelDiagnostic &channel)
{
    if (!channel.configured) return QStringLiteral("NOT CONFIGURED");
    if (channel.status.compare(QStringLiteral("Playing"), Qt::CaseInsensitive) != 0) {
        return channel.status;
    }
    if (!channel.error.isEmpty()) return QStringLiteral("DEGRADED");
    const qint64 ageMs = frameAgeMs(channel.lastFrameWallClockMs);
    if (ageMs < 0 || ageMs > 3000) return QStringLiteral("STALE");
    return QStringLiteral("PLAYING");
}

QString tableStatusColor(const QString &status)
{
    const QString normalized = status.trimmed().toUpper();
    if (normalized == QStringLiteral("PLAYING") || normalized == QStringLiteral("CONNECTED")) {
        return QStringLiteral("#c8e6c9");
    }
    if (normalized == QStringLiteral("CONNECTING")
        || normalized == QStringLiteral("RECONNECTING")
        || normalized == QStringLiteral("DEGRADED")) {
        return QStringLiteral("#ffe0b2");
    }
    return QStringLiteral("#ffcdd2");
}
}

DebugPage::DebugPage(QWidget *parent)
    : DebugPage(nullptr, parent)
{
}

DebugPage::DebugPage(QTabWidget *systemTabs, QWidget *parent)
    : QWidget(parent)
{
    m_embeddedInSystemTabs = systemTabs != nullptr;
    QVBoxLayout *layout = nullptr;
    QTabWidget *tabs = systemTabs;
    if (!m_embeddedInSystemTabs) {
        setStyleSheet(SystemUiStyle::pageStyleSheet());
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addLayout(createPageHeader(
            this, QStringLiteral("Debug"),
            QStringLiteral("Inspect runtime connections and test scenarios")));

        auto *environmentBanner = new QLabel(
            QStringLiteral("DEVELOPMENT & DIAGNOSTICS · Review each action's impact scope before running it."),
            this);
        environmentBanner->setObjectName(QStringLiteral("debugEnvironmentBanner"));
        environmentBanner->setWordWrap(true);
        environmentBanner->setProperty("uiBanner", QStringLiteral("info"));
        layout->addWidget(environmentBanner);
        tabs = new QTabWidget(this);
        tabs->setObjectName(QStringLiteral("debugTabWidget"));
        createPageHelpButton(
            this, this,
            {QStringLiteral("debug"), QStringLiteral("Debug"),
             QStringLiteral("Debug 사용 안내"),
             QStringLiteral("Pi API, RTSP, 주차 데이터의 상태를 진단하고 로컬 테스트 시나리오를 실행합니다."),
             QStringLiteral(
                 "<b>1. Overview</b><br>Runtime data, Pi API, RTSP streams, Parking state 카드와 상세 표에서 연결 상태·지연·마지막 오류를 확인합니다. <i>Reconnect now</i>로 API 재연결을 요청할 수 있습니다.<br><br>"
                 "<b>2. Live Logs</b><br>Level, Module과 검색어로 진단 로그를 필터링합니다. <i>Clear view</i>는 현재 Qt 로그 화면을 비웁니다.<br><br>"
                 "<b>3. Test Tools</b><br>알람 ACK, mock EV, 위반·초과주차·센서 오류, 랜덤 상태와 normalized RX sample을 로컬 Qt 상태에 적용합니다.<br><br>"
                 "<b>4. Manual message</b><br><i>RX message</i>에 정규화 메시지를 입력하고 <i>Inject locally</i>로 parser와 화면 반영을 시험합니다."),
             QStringLiteral(
                 "※ Test Tools와 Inject locally는 Pi 서버로 전송되지 않는 로컬 simulation sandbox입니다. 실제 장비 통합 성공 증거로 사용하지 마세요.\n"
                 "   시뮬레이션 후 Runtime data가 MIXED로 표시될 수 있습니다.")});
    }

    tabs->addTab(createOverviewTab(),
                 m_embeddedInSystemTabs ? QStringLiteral("Diagnostics")
                                        : QStringLiteral("Overview"));
    tabs->addTab(createLogsTab(), QStringLiteral("Live Logs"));
    tabs->addTab(createTestToolsTab(), QStringLiteral("Test Tools"));
    if (layout) layout->addWidget(tabs);

    m_ageRefreshTimer = new QTimer(this);
    m_ageRefreshTimer->setInterval(1000);
    connect(m_ageRefreshTimer, &QTimer::timeout,
            this, &DebugPage::renderRtspDiagnostics);
    m_ageRefreshTimer->start();

    renderApiDiagnostic();
    renderParkingDiagnostic();
    renderRtspDiagnostics();
}

QWidget *DebugPage::createOverviewTab()
{
    auto *tab = new QWidget(this);
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    auto *scrollArea = new QScrollArea(tab);
    scrollArea->setObjectName(QStringLiteral("debugOverviewScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *content = new QWidget(scrollArea);
    content->setProperty("systemTabContent", true);
    content->setMaximumWidth(SystemUiStyle::DiagnosticsMaxWidth);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    scrollArea->setWidget(content);
    tabLayout->addWidget(scrollArea);

    auto *summary = new QWidget(tab);
    auto *summaryLayout = new QGridLayout(summary);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(12);
    auto addCard = [summaryLayout, summary](int column, const QString &title,
                                            QLabel **valueLabel, QLabel **detailLabel) {
        auto *card = new QFrame(summary);
        card->setObjectName(QStringLiteral("debugSummaryCard%1").arg(column));
        card->setFrameShape(QFrame::StyledPanel);
        card->setProperty("uiCard", true);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 11, 12, 11);
        cardLayout->setSpacing(4);
        auto *titleLabel = new QLabel(title, card);
        titleLabel->setProperty("uiCaption", true);
        *valueLabel = new QLabel(QStringLiteral("-"), card);
        (*valueLabel)->setStyleSheet(valueStyle(QStringLiteral("UNKNOWN")));
        *detailLabel = new QLabel(QStringLiteral("Waiting for diagnostics"), card);
        (*detailLabel)->setWordWrap(true);
        (*detailLabel)->setProperty("uiDetail", true);
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(*valueLabel);
        cardLayout->addWidget(*detailLabel);
        summaryLayout->addWidget(card, column / 2, column % 2);
    };
    addCard(0, QStringLiteral("Runtime data"), &m_runtimeValueLabel, &m_runtimeDetailLabel);
    addCard(1, QStringLiteral("Pi API"), &m_apiCardValueLabel, &m_apiCardDetailLabel);
    addCard(2, QStringLiteral("RTSP streams"), &m_streamCardValueLabel, &m_streamCardDetailLabel);
    addCard(3, QStringLiteral("Parking state"), &m_parkingCardValueLabel, &m_parkingCardDetailLabel);
    summaryLayout->setColumnStretch(0, 1);
    summaryLayout->setColumnStretch(1, 1);
    layout->addWidget(summary);

    auto *apiGroup = new QGroupBox(QStringLiteral("Pi API Diagnostics"), tab);
    auto *apiLayout = new QVBoxLayout(apiGroup);
    apiLayout->setContentsMargins(12, 14, 12, 12);
    apiLayout->setSpacing(10);
    auto *apiImpact = new QLabel(
        m_embeddedInSystemTabs
            ? QStringLiteral("READ ONLY · Endpoint changes and reconnection are managed in the Configuration tab.")
            : QStringLiteral("SERVER/API CONNECTION · Reconnect now starts a real server request from this client."),
        apiGroup);
    apiImpact->setObjectName(QStringLiteral("debugApiImpactLabel"));
    apiImpact->setWordWrap(true);
    apiImpact->setProperty("uiBanner", QStringLiteral("info"));
    apiLayout->addWidget(apiImpact);
    auto *apiGrid = new QGridLayout;
    m_apiEndpointLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiStatusLabel = new QLabel(QStringLiteral("DISABLED"), apiGroup);
    m_apiLatencyLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiLastSuccessLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiRetryLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiErrorLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiErrorLabel->setWordWrap(true);
    QPushButton *reconnectButton = nullptr;
    if (!m_embeddedInSystemTabs) {
        reconnectButton = new QPushButton(QStringLiteral("Reconnect now"), apiGroup);
        reconnectButton->setObjectName(QStringLiteral("debugReconnectApiButton"));
        reconnectButton->setProperty("impactScope", QStringLiteral("SERVER_API"));
        reconnectButton->setProperty("uiActionRole", QStringLiteral("primary"));
        reconnectButton->setToolTip(QStringLiteral(
            "Starts a real API connection attempt using the configured server endpoint"));
    }
    apiGrid->addWidget(new QLabel(QStringLiteral("Endpoint"), apiGroup), 0, 0);
    apiGrid->addWidget(m_apiEndpointLabel, 0, 1);
    apiGrid->addWidget(new QLabel(QStringLiteral("State"), apiGroup), 1, 0);
    apiGrid->addWidget(m_apiStatusLabel, 1, 1);
    apiGrid->addWidget(new QLabel(QStringLiteral("Last latency / HTTP"), apiGroup), 0, 2);
    apiGrid->addWidget(m_apiLatencyLabel, 0, 3);
    apiGrid->addWidget(new QLabel(QStringLiteral("Last successful sync"), apiGroup), 1, 2);
    apiGrid->addWidget(m_apiLastSuccessLabel, 1, 3);
    apiGrid->addWidget(new QLabel(QStringLiteral("Retry"), apiGroup), 2, 0);
    apiGrid->addWidget(m_apiRetryLabel, 2, 1);
    apiGrid->addWidget(new QLabel(QStringLiteral("Last error"), apiGroup), 2, 2);
    apiGrid->addWidget(m_apiErrorLabel, 2, 3);
    if (reconnectButton) apiGrid->addWidget(reconnectButton, 0, 4, 3, 1);
    apiGrid->setColumnStretch(1, 1);
    apiGrid->setColumnStretch(3, 2);
    apiLayout->addLayout(apiGrid);
    if (reconnectButton) {
        connect(reconnectButton, &QPushButton::clicked,
                this, &DebugPage::reconnectApiRequested);
    }
    layout->addWidget(apiGroup);

    auto *streamGroup = new QGroupBox(QStringLiteral("RTSP Channel Diagnostics"), tab);
    auto *streamLayout = new QVBoxLayout(streamGroup);
    streamLayout->setContentsMargins(12, 14, 12, 12);
    streamLayout->setSpacing(10);
    m_streamTable = new QTableWidget(4, 7, streamGroup);
    m_streamTable->setObjectName(QStringLiteral("debugStreamTable"));
    m_streamTable->setHorizontalHeaderLabels({
        QStringLiteral("Channel"), QStringLiteral("Configured"), QStringLiteral("State"),
        QStringLiteral("Resolution"), QStringLiteral("Startup"), QStringLiteral("Frame age"),
        QStringLiteral("Last error")});
    m_streamTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_streamTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_streamTable->verticalHeader()->setVisible(false);
    m_streamTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_streamTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_streamTable->setAlternatingRowColors(true);
    streamLayout->addWidget(m_streamTable);
    layout->addWidget(streamGroup, 1);

    auto *parkingGroup = new QGroupBox(QStringLiteral("Parking Data Diagnostics"), tab);
    auto *parkingLayout = new QVBoxLayout(parkingGroup);
    parkingLayout->setContentsMargins(12, 14, 12, 12);
    parkingLayout->setSpacing(10);
    m_parkingDetailLabel = new QLabel(QStringLiteral("Waiting for parking state"), parkingGroup);
    m_parkingDetailLabel->setWordWrap(true);
    parkingLayout->addWidget(m_parkingDetailLabel);
    layout->addWidget(parkingGroup);
    return tab;
}

QWidget *DebugPage::createLogsTab()
{
    auto *tab = new QWidget(this);
    tab->setProperty("systemTabContent", true);
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    auto *scrollArea = new QScrollArea(tab);
    scrollArea->setObjectName(QStringLiteral("debugLogsScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *content = new QWidget(scrollArea);
    content->setProperty("systemTabContent", true);
    content->setMaximumWidth(SystemUiStyle::LogsMaxWidth);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    scrollArea->setWidget(content);
    tabLayout->addWidget(scrollArea);
    auto *scopeLabel = new QLabel(
        QStringLiteral("READ ONLY · Local diagnostic history. Clearing this view does not clear server or device logs."),
        tab);
    scopeLabel->setObjectName(QStringLiteral("debugLogsImpactLabel"));
    scopeLabel->setWordWrap(true);
    scopeLabel->setProperty("uiBanner", QStringLiteral("neutral"));
    layout->addWidget(scopeLabel);

    auto *filterPanel = new QFrame(tab);
    filterPanel->setObjectName(QStringLiteral("debugLogFilterPanel"));
    filterPanel->setProperty("uiPanel", true);
    auto *filterPanelLayout = new QVBoxLayout(filterPanel);
    filterPanelLayout->setContentsMargins(12, 12, 12, 12);
    filterPanelLayout->setSpacing(10);
    auto *filters = new QHBoxLayout;
    m_levelFilter = new QComboBox(tab);
    m_levelFilter->setObjectName(QStringLiteral("debugLogLevelFilter"));
    m_levelFilter->addItems({QStringLiteral("All levels"), QStringLiteral("INFO"),
                             QStringLiteral("WARN"), QStringLiteral("ERROR")});
    m_levelFilter->setMinimumWidth(130);
    m_moduleFilter = new QComboBox(tab);
    m_moduleFilter->setObjectName(QStringLiteral("debugLogModuleFilter"));
    m_moduleFilter->addItems({QStringLiteral("All modules"), QStringLiteral("API"),
                              QStringLiteral("RTSP"), QStringLiteral("EVENT"),
                              QStringLiteral("SIMULATION")});
    m_moduleFilter->setMinimumWidth(150);
    m_logSearch = new QLineEdit(tab);
    m_logSearch->setObjectName(QStringLiteral("debugLogSearchEdit"));
    m_logSearch->setPlaceholderText(QStringLiteral("Search code or message"));
    m_logSearch->setClearButtonEnabled(true);
    m_autoScrollCheck = new QCheckBox(QStringLiteral("Auto scroll"), tab);
    m_autoScrollCheck->setObjectName(QStringLiteral("debugLogAutoScrollCheck"));
    m_autoScrollCheck->setChecked(true);
    auto *clearButton = new QPushButton(QStringLiteral("Clear view"), tab);
    clearButton->setObjectName(QStringLiteral("debugLogClearButton"));
    clearButton->setToolTip(QStringLiteral(
        "Clear only the logs shown in this Qt client view"));
    clearButton->setProperty("uiActionRole", QStringLiteral("danger"));
    filters->addWidget(new QLabel(QStringLiteral("Level"), tab));
    filters->addWidget(m_levelFilter);
    filters->addSpacing(8);
    filters->addWidget(new QLabel(QStringLiteral("Module"), tab));
    filters->addWidget(m_moduleFilter);
    filters->addSpacing(8);
    filters->addWidget(m_autoScrollCheck);
    filters->addStretch();
    filterPanelLayout->addLayout(filters);
    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(QStringLiteral("Search"), tab));
    searchRow->addWidget(m_logSearch, 1);
    searchRow->addWidget(clearButton);
    filterPanelLayout->addLayout(searchRow);
    layout->addWidget(filterPanel);

    m_logStateLabel = new QLabel(
        QStringLiteral("No diagnostic logs received yet."), tab);
    m_logStateLabel->setObjectName(QStringLiteral("debugLogStateLabel"));
    m_logStateLabel->setProperty("uiBanner", QStringLiteral("neutral"));
    layout->addWidget(m_logStateLabel);

    m_logTable = new QTableWidget(0, 5, tab);
    m_logTable->setObjectName(QStringLiteral("debugLogTable"));
    m_logTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Level"),
                                           QStringLiteral("Module"), QStringLiteral("Code"),
                                           QStringLiteral("Message")});
    m_logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setAlternatingRowColors(true);
    layout->addWidget(m_logTable, 1);

    connect(m_levelFilter, &QComboBox::currentTextChanged,
            this, &DebugPage::refreshLogFilter);
    connect(m_moduleFilter, &QComboBox::currentTextChanged,
            this, &DebugPage::refreshLogFilter);
    connect(m_logSearch, &QLineEdit::textChanged,
            this, &DebugPage::refreshLogFilter);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        m_logs.clear();
        m_logTable->setRowCount(0);
        updateLogStatePresentation();
    });
    return tab;
}

QWidget *DebugPage::createTestToolsTab()
{
    auto *tab = new QWidget(this);
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    auto *scrollArea = new QScrollArea(tab);
    scrollArea->setObjectName(QStringLiteral("debugTestToolsScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *content = new QWidget(scrollArea);
    content->setProperty("systemTabContent", true);
    content->setMaximumWidth(SystemUiStyle::TestToolsMaxWidth);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    scrollArea->setWidget(content);
    tabLayout->addWidget(scrollArea);

    auto *notice = new QLabel(
        QStringLiteral("Simulation sandbox: these actions update only the local Qt state and are never sent to the Pi server."),
        tab);
    notice->setObjectName(QStringLiteral("debugSimulationNotice"));
    notice->setWordWrap(true);
    notice->setProperty("uiBanner", QStringLiteral("warning"));
    layout->addWidget(notice);

    auto *controls = new QGroupBox(QStringLiteral("Local Qt State Actions"), tab);
    controls->setObjectName(QStringLiteral("debugLocalStateGroup"));
    auto *grid = new QGridLayout(controls);
    grid->setContentsMargins(12, 14, 12, 12);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    auto *clearButton = new QPushButton(QStringLiteral("Acknowledge alarms"), controls);
    auto *mockButton = new QPushButton(QStringLiteral("Toggle mock EV"), controls);
    auto *randomButton = new QPushButton(QStringLiteral("Randomize parking"), controls);
    clearButton->setObjectName(QStringLiteral("debugAcknowledgeAlarmsButton"));
    mockButton->setObjectName(QStringLiteral("debugToggleMockEvButton"));
    randomButton->setObjectName(QStringLiteral("debugRandomizeParkingButton"));
    for (QPushButton *button : {clearButton, mockButton, randomButton}) {
        button->setProperty("impactScope", QStringLiteral("LOCAL_QT_STATE"));
        button->setToolTip(QStringLiteral(
            "Updates the local Qt runtime only; no command is sent to the Pi server"));
        button->setProperty("uiActionRole", QStringLiteral("secondary"));
        button->setProperty("uiWideAction", true);
    }
    grid->addWidget(clearButton, 0, 0);
    grid->addWidget(mockButton, 0, 1);
    grid->addWidget(randomButton, 1, 0, 1, 2);
    layout->addWidget(controls);

    auto *eventControls = new QGroupBox(QStringLiteral("Local Test Event Publication"), tab);
    eventControls->setObjectName(QStringLiteral("debugLocalEventGroup"));
    auto *eventGrid = new QGridLayout(eventControls);
    eventGrid->setContentsMargins(12, 14, 12, 12);
    eventGrid->setHorizontalSpacing(10);
    eventGrid->setVerticalSpacing(10);
    auto *nonEvButton = new QPushButton(QStringLiteral("Non-EV violation"), eventControls);
    auto *overtimeButton = new QPushButton(QStringLiteral("Overstay warning"), eventControls);
    auto *sensorButton = new QPushButton(QStringLiteral("Sensor error"), eventControls);
    auto *sampleButton = new QPushButton(QStringLiteral("Sample messages"), eventControls);
    nonEvButton->setObjectName(QStringLiteral("debugNonEvAlertButton"));
    overtimeButton->setObjectName(QStringLiteral("debugOverstayAlertButton"));
    sensorButton->setObjectName(QStringLiteral("debugSensorErrorButton"));
    sampleButton->setObjectName(QStringLiteral("debugSampleMessagesButton"));
    for (QPushButton *button : {nonEvButton, overtimeButton, sensorButton, sampleButton}) {
        button->setProperty("impactScope", QStringLiteral("LOCAL_TEST_EVENT"));
        button->setToolTip(QStringLiteral(
            "Publishes a test event inside the local Qt process only"));
        button->setProperty("uiActionRole", QStringLiteral("warning"));
        button->setProperty("uiWideAction", true);
    }
    eventGrid->addWidget(nonEvButton, 0, 0);
    eventGrid->addWidget(overtimeButton, 0, 1);
    eventGrid->addWidget(sensorButton, 1, 0);
    eventGrid->addWidget(sampleButton, 1, 1);
    layout->addWidget(eventControls);

    auto *messageGroup = new QGroupBox(QStringLiteral("Manual Normalized RX Message"), tab);
    auto *messageLayout = new QGridLayout(messageGroup);
    messageLayout->setContentsMargins(12, 14, 12, 12);
    messageLayout->setSpacing(10);
    m_messageInput = new QLineEdit(QStringLiteral("EV_ALERT,EV01,NON_EV"), messageGroup);
    m_messageInput->setObjectName(QStringLiteral("debugManualMessageEdit"));
    m_messageInput->setPlaceholderText(QStringLiteral("Example: PARKING_SLOT,P01,OCCUPIED"));
    auto *applyButton = new QPushButton(QStringLiteral("Inject message"), messageGroup);
    applyButton->setObjectName(QStringLiteral("debugManualInjectButton"));
    applyButton->setProperty("impactScope", QStringLiteral("LOCAL_TEST_EVENT"));
    applyButton->setProperty("uiActionRole", QStringLiteral("primary"));
    applyButton->setToolTip(QStringLiteral(
        "Injects this normalized message into the local Qt parser only"));
    messageLayout->addWidget(new QLabel(QStringLiteral("RX message"), messageGroup), 0, 0);
    messageLayout->addWidget(m_messageInput, 0, 1);
    messageLayout->addWidget(applyButton, 1, 1, Qt::AlignRight);
    messageLayout->setColumnStretch(1, 1);
    layout->addWidget(messageGroup);
    m_lastMessageLabel = new QLabel(QStringLiteral("Last RX: -"), tab);
    m_lastMessageLabel->setObjectName(QStringLiteral("debugLastMessageLabel"));
    m_lastMessageLabel->setWordWrap(true);
    m_lastMessageLabel->setProperty("uiBanner", QStringLiteral("neutral"));
    layout->addWidget(m_lastMessageLabel);
    layout->addStretch();

    connect(clearButton, &QPushButton::clicked, this, &DebugPage::clearAlarmsRequested);
    connect(mockButton, &QPushButton::clicked, this, &DebugPage::toggleMockEvRequested);
    connect(nonEvButton, &QPushButton::clicked, this, &DebugPage::nonEvAlertRequested);
    connect(overtimeButton, &QPushButton::clicked, this, &DebugPage::overtimeAlertRequested);
    connect(sensorButton, &QPushButton::clicked, this, &DebugPage::sensorErrorRequested);
    connect(randomButton, &QPushButton::clicked, this, &DebugPage::randomizeParkingRequested);
    connect(sampleButton, &QPushButton::clicked, this, &DebugPage::sampleMessagesRequested);
    auto sendManual = [this]() { emit manualMessageRequested(m_messageInput->text().trimmed()); };
    connect(applyButton, &QPushButton::clicked, this, sendManual);
    connect(m_messageInput, &QLineEdit::returnPressed, this, sendManual);
    return tab;
}

void DebugPage::setLastMessage(const QString &message)
{
    if (m_lastMessageLabel) m_lastMessageLabel->setText(message);
}

void DebugPage::setApiDiagnostic(const ApiDiagnosticState &state)
{
    m_apiState = state;
    renderApiDiagnostic();
}

void DebugPage::setRtspDiagnostics(const QList<RtspChannelDiagnostic> &channels)
{
    m_rtspChannels = channels;
    renderRtspDiagnostics();
}

void DebugPage::setParkingDiagnostic(const ParkingDiagnosticState &state)
{
    m_parkingState = state;
    renderParkingDiagnostic();
}

void DebugPage::appendDiagnosticLog(const DiagnosticLogRecord &record)
{
    m_logs.prepend(record);
    while (m_logs.size() > 1000) m_logs.removeLast();
    m_logTable->insertRow(0);
    const QStringList values = {
        record.occurredAt.toString(QStringLiteral("HH:mm:ss.zzz")), record.level,
        record.module, record.code, record.message};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        const QString level = record.level.trimmed().toUpper();
        if (level == QStringLiteral("ERROR")) {
            item->setBackground(QColor(QStringLiteral("#ffebee")));
            item->setForeground(QColor(QStringLiteral("#b71c1c")));
        } else if (level == QStringLiteral("WARN")) {
            item->setBackground(QColor(QStringLiteral("#fff3e0")));
            item->setForeground(QColor(QStringLiteral("#e65100")));
        }
        m_logTable->setItem(0, column, item);
    }
    while (m_logTable->rowCount() > 1000) m_logTable->removeRow(1000);
    refreshLogFilter();
    updateLogStatePresentation();
    if (m_autoScrollCheck && m_autoScrollCheck->isChecked()) m_logTable->scrollToTop();
}

void DebugPage::renderApiDiagnostic()
{
    if (!m_apiStatusLabel) return;
    m_apiCardValueLabel->setText(m_apiState.status);
    m_apiCardValueLabel->setStyleSheet(valueStyle(m_apiState.status));
    m_apiCardDetailLabel->setText(m_apiState.endpoint.isEmpty()
                                      ? QStringLiteral("Not configured") : m_apiState.endpoint);
    m_apiEndpointLabel->setText(m_apiState.endpoint.isEmpty()
                                    ? QStringLiteral("Not configured") : m_apiState.endpoint);
    m_apiStatusLabel->setText(m_apiState.status);
    m_apiStatusLabel->setStyleSheet(valueStyle(m_apiState.status));
    m_apiLatencyLabel->setText(m_apiState.lastLatencyMs < 0
        ? QStringLiteral("-")
        : QStringLiteral("%1 ms / HTTP %2")
              .arg(m_apiState.lastLatencyMs)
              .arg(m_apiState.lastHttpStatus > 0
                       ? QString::number(m_apiState.lastHttpStatus) : QStringLiteral("-")));
    m_apiLastSuccessLabel->setText(timeText(m_apiState.lastSuccessAt));
    m_apiRetryLabel->setText(m_apiState.nextRetrySeconds > 0
        ? QStringLiteral("%1 failures | next in %2 s")
              .arg(m_apiState.consecutiveFailures).arg(m_apiState.nextRetrySeconds)
        : QStringLiteral("%1 failures").arg(m_apiState.consecutiveFailures));
    m_apiErrorLabel->setText(m_apiState.lastError.isEmpty()
                                 ? QStringLiteral("-") : m_apiState.lastError);
}

void DebugPage::renderRtspDiagnostics()
{
    if (!m_streamTable) return;
    int playing = 0;
    int configured = 0;
    int degraded = 0;
    int stale = 0;
    for (int row = 0; row < 4; ++row) {
        RtspChannelDiagnostic channel;
        channel.channel = QStringLiteral("CH%1").arg(row + 1);
        if (row < m_rtspChannels.size()) channel = m_rtspChannels.at(row);
        if (channel.configured) ++configured;
        const QString effectiveStatus = effectiveStreamStatus(channel);
        if (effectiveStatus == QStringLiteral("PLAYING")) ++playing;
        else if (effectiveStatus == QStringLiteral("DEGRADED")) ++degraded;
        else if (effectiveStatus == QStringLiteral("STALE")) ++stale;
        const QStringList values = {
            channel.channel,
            channel.configured ? QStringLiteral("YES") : QStringLiteral("NO"),
            effectiveStatus,
            channel.resolution.isValid()
                ? QStringLiteral("%1x%2").arg(channel.resolution.width()).arg(channel.resolution.height())
                : QStringLiteral("-"),
            channel.startupDelayMs >= 0
                ? QStringLiteral("%1 ms").arg(channel.startupDelayMs) : QStringLiteral("-"),
            frameAgeText(channel.lastFrameWallClockMs),
            channel.error.isEmpty() ? QStringLiteral("-") : channel.error};
        for (int column = 0; column < values.size(); ++column) {
            QTableWidgetItem *item = m_streamTable->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                m_streamTable->setItem(row, column, item);
            }
            item->setText(values.at(column));
            if (column == 2) item->setBackground(QColor(tableStatusColor(effectiveStatus)));
        }
    }
    m_streamCardValueLabel->setText(QStringLiteral("%1 / %2 HEALTHY").arg(playing).arg(configured));
    m_streamCardValueLabel->setStyleSheet(valueStyle(
        configured > 0 && playing == configured ? QStringLiteral("CONNECTED")
                                                : QStringLiteral("ERROR")));
    m_streamCardDetailLabel->setText(configured == 0
        ? QStringLiteral("RTSP URLs are not configured")
        : QStringLiteral("%1 configured | %2 degraded | %3 stale")
              .arg(configured).arg(degraded).arg(stale));
}

void DebugPage::renderParkingDiagnostic()
{
    if (!m_runtimeValueLabel) return;
    m_runtimeValueLabel->setText(m_parkingState.dataSource);
    m_runtimeValueLabel->setStyleSheet(valueStyle(m_parkingState.dataSource));
    m_runtimeDetailLabel->setText(
        m_parkingState.dataSource == QStringLiteral("MOCK")
            ? QStringLiteral("Local simulation data; Pi state not synchronized")
            : m_parkingState.dataSource == QStringLiteral("MIXED")
              ? QStringLiteral("Server state was modified by local test tools")
              : m_parkingState.dataSource == QStringLiteral("SERVER")
                ? QStringLiteral("Latest state came from the Pi API")
                : QStringLiteral("Waiting for a data source"));
    m_parkingCardValueLabel->setText(QStringLiteral("%1 slots").arg(m_parkingState.slotCount));
    m_parkingCardValueLabel->setStyleSheet(valueStyle(
        m_parkingState.slotCount > 0 ? QStringLiteral("CONNECTED") : QStringLiteral("ERROR")));
    m_parkingCardDetailLabel->setText(
        QStringLiteral("%1 active alarms").arg(m_parkingState.activeAlarmCount));
    m_parkingDetailLabel->setText(
        QStringLiteral("Source: %1 | Current slots: %2 | Active alarms: %3 | "
                       "Last server slots: %4 | Last server sync: %5 | Last simulation: %6")
            .arg(m_parkingState.dataSource)
            .arg(m_parkingState.slotCount)
            .arg(m_parkingState.activeAlarmCount)
            .arg(m_parkingState.lastServerSlotCount)
            .arg(timeText(m_parkingState.lastServerSyncAt),
                 m_parkingState.lastSimulationScenario.isEmpty()
                     ? QStringLiteral("-") : m_parkingState.lastSimulationScenario));
}

void DebugPage::refreshLogFilter()
{
    if (!m_logTable) return;
    const QString level = m_levelFilter ? m_levelFilter->currentText() : QStringLiteral("All levels");
    const QString module = m_moduleFilter ? m_moduleFilter->currentText() : QStringLiteral("All modules");
    const QString search = m_logSearch ? m_logSearch->text().trimmed() : QString();
    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        const QString rowLevel = m_logTable->item(row, 1)->text();
        const QString rowModule = m_logTable->item(row, 2)->text();
        const QString searchable = m_logTable->item(row, 3)->text()
            + QLatin1Char(' ') + m_logTable->item(row, 4)->text();
        const bool visible = (level == QStringLiteral("All levels") || level == rowLevel)
            && (module == QStringLiteral("All modules") || module == rowModule)
            && (search.isEmpty() || searchable.contains(search, Qt::CaseInsensitive));
        m_logTable->setRowHidden(row, !visible);
    }
}

void DebugPage::updateLogStatePresentation()
{
    if (!m_logStateLabel || !m_logTable) return;
    if (m_logTable->rowCount() == 0) {
        m_logStateLabel->setText(QStringLiteral("No diagnostic logs received yet."));
        m_logStateLabel->show();
    } else {
        m_logStateLabel->hide();
    }
}
