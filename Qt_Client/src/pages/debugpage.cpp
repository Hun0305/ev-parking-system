#include "debugpage.h"

#include "widgets/pagehelp.h"

#include <QCheckBox>
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
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createOverviewTab(), QStringLiteral("Overview"));
    tabs->addTab(createLogsTab(), QStringLiteral("Live Logs"));
    tabs->addTab(createTestToolsTab(), QStringLiteral("Test Tools"));
    tabs->setCornerWidget(createPageHelpButton(
        this, tabs,
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
             "   시뮬레이션 후 Runtime data가 MIXED로 표시될 수 있습니다.")}));
    layout->addWidget(tabs);

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
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 10, 8, 8);
    layout->setSpacing(10);

    auto *summary = new QWidget(tab);
    auto *summaryLayout = new QGridLayout(summary);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(10);
    auto addCard = [summaryLayout, summary](int column, const QString &title,
                                            QLabel **valueLabel, QLabel **detailLabel) {
        auto *card = new QFrame(summary);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:white; border:1px solid #cfd8dc; border-radius:7px; }"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *titleLabel = new QLabel(title, card);
        titleLabel->setStyleSheet(QStringLiteral("color:#607d8b;font-weight:700;"));
        *valueLabel = new QLabel(QStringLiteral("-"), card);
        (*valueLabel)->setStyleSheet(valueStyle(QStringLiteral("UNKNOWN")));
        *detailLabel = new QLabel(QStringLiteral("Waiting for diagnostics"), card);
        (*detailLabel)->setWordWrap(true);
        (*detailLabel)->setStyleSheet(QStringLiteral("color:#546e7a;font-size:11px;"));
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(*valueLabel);
        cardLayout->addWidget(*detailLabel);
        summaryLayout->addWidget(card, 0, column);
    };
    addCard(0, QStringLiteral("Runtime data"), &m_runtimeValueLabel, &m_runtimeDetailLabel);
    addCard(1, QStringLiteral("Pi API"), &m_apiCardValueLabel, &m_apiCardDetailLabel);
    addCard(2, QStringLiteral("RTSP streams"), &m_streamCardValueLabel, &m_streamCardDetailLabel);
    addCard(3, QStringLiteral("Parking state"), &m_parkingCardValueLabel, &m_parkingCardDetailLabel);
    for (int i = 0; i < 4; ++i) summaryLayout->setColumnStretch(i, 1);
    layout->addWidget(summary);

    auto *apiGroup = new QGroupBox(QStringLiteral("Pi API Diagnostics"), tab);
    auto *apiGrid = new QGridLayout(apiGroup);
    m_apiEndpointLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiStatusLabel = new QLabel(QStringLiteral("DISABLED"), apiGroup);
    m_apiLatencyLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiLastSuccessLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiRetryLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiErrorLabel = new QLabel(QStringLiteral("-"), apiGroup);
    m_apiErrorLabel->setWordWrap(true);
    auto *reconnectButton = new QPushButton(QStringLiteral("Reconnect now"), apiGroup);
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
    apiGrid->addWidget(reconnectButton, 0, 4, 3, 1);
    apiGrid->setColumnStretch(1, 1);
    apiGrid->setColumnStretch(3, 2);
    connect(reconnectButton, &QPushButton::clicked,
            this, &DebugPage::reconnectApiRequested);
    layout->addWidget(apiGroup);

    auto *streamGroup = new QGroupBox(QStringLiteral("RTSP Channel Diagnostics"), tab);
    auto *streamLayout = new QVBoxLayout(streamGroup);
    m_streamTable = new QTableWidget(4, 7, streamGroup);
    m_streamTable->setHorizontalHeaderLabels({
        QStringLiteral("Channel"), QStringLiteral("Configured"), QStringLiteral("State"),
        QStringLiteral("Resolution"), QStringLiteral("Startup"), QStringLiteral("Frame age"),
        QStringLiteral("Last error")});
    m_streamTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_streamTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_streamTable->verticalHeader()->setVisible(false);
    m_streamTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_streamTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    streamLayout->addWidget(m_streamTable);
    layout->addWidget(streamGroup, 1);

    auto *parkingGroup = new QGroupBox(QStringLiteral("Parking Data Diagnostics"), tab);
    auto *parkingLayout = new QVBoxLayout(parkingGroup);
    m_parkingDetailLabel = new QLabel(QStringLiteral("Waiting for parking state"), parkingGroup);
    m_parkingDetailLabel->setWordWrap(true);
    parkingLayout->addWidget(m_parkingDetailLabel);
    layout->addWidget(parkingGroup);
    return tab;
}

QWidget *DebugPage::createLogsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 10, 8, 8);
    auto *filters = new QHBoxLayout;
    m_levelFilter = new QComboBox(tab);
    m_levelFilter->addItems({QStringLiteral("All levels"), QStringLiteral("INFO"),
                             QStringLiteral("WARN"), QStringLiteral("ERROR")});
    m_moduleFilter = new QComboBox(tab);
    m_moduleFilter->addItems({QStringLiteral("All modules"), QStringLiteral("API"),
                              QStringLiteral("RTSP"), QStringLiteral("EVENT"),
                              QStringLiteral("SIMULATION")});
    m_logSearch = new QLineEdit(tab);
    m_logSearch->setPlaceholderText(QStringLiteral("Search code or message"));
    m_autoScrollCheck = new QCheckBox(QStringLiteral("Auto scroll"), tab);
    m_autoScrollCheck->setChecked(true);
    auto *clearButton = new QPushButton(QStringLiteral("Clear view"), tab);
    filters->addWidget(m_levelFilter);
    filters->addWidget(m_moduleFilter);
    filters->addWidget(m_logSearch, 1);
    filters->addWidget(m_autoScrollCheck);
    filters->addWidget(clearButton);
    layout->addLayout(filters);

    m_logTable = new QTableWidget(0, 5, tab);
    m_logTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Level"),
                                           QStringLiteral("Module"), QStringLiteral("Code"),
                                           QStringLiteral("Message")});
    m_logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
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
    });
    return tab;
}

QWidget *DebugPage::createTestToolsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 10, 8, 8);

    auto *notice = new QLabel(
        QStringLiteral("Simulation sandbox: these actions update only the local Qt state and are never sent to the Pi server."),
        tab);
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral(
        "background:#fff3e0;color:#e65100;border:1px solid #ffcc80;border-radius:5px;padding:8px;font-weight:700;"));
    layout->addWidget(notice);

    auto *controls = new QGroupBox(QStringLiteral("Simulation Scenarios"), tab);
    auto *grid = new QGridLayout(controls);
    auto *clearButton = new QPushButton(QStringLiteral("Acknowledge alarms"), controls);
    auto *mockButton = new QPushButton(QStringLiteral("Toggle mock EV"), controls);
    auto *nonEvButton = new QPushButton(QStringLiteral("Non-EV violation"), controls);
    auto *overtimeButton = new QPushButton(QStringLiteral("Overstay warning"), controls);
    auto *sensorButton = new QPushButton(QStringLiteral("Hall sensor error"), controls);
    auto *randomButton = new QPushButton(QStringLiteral("Randomize parking"), controls);
    auto *sampleButton = new QPushButton(QStringLiteral("Run normalized RX samples"), controls);
    grid->addWidget(clearButton, 0, 0); grid->addWidget(mockButton, 0, 1);
    grid->addWidget(nonEvButton, 1, 0); grid->addWidget(overtimeButton, 1, 1);
    grid->addWidget(sensorButton, 2, 0); grid->addWidget(randomButton, 2, 1);
    grid->addWidget(sampleButton, 3, 0, 1, 2);
    layout->addWidget(controls);

    auto *messageGroup = new QGroupBox(QStringLiteral("Manual Normalized RX Message"), tab);
    auto *messageLayout = new QHBoxLayout(messageGroup);
    m_messageInput = new QLineEdit(QStringLiteral("EV_ALERT,EV01,NON_EV"), messageGroup);
    m_messageInput->setPlaceholderText(QStringLiteral("Example: PARKING_SLOT,P01,OCCUPIED"));
    auto *applyButton = new QPushButton(QStringLiteral("Inject locally"), messageGroup);
    messageLayout->addWidget(new QLabel(QStringLiteral("RX message"), messageGroup));
    messageLayout->addWidget(m_messageInput, 1);
    messageLayout->addWidget(applyButton);
    layout->addWidget(messageGroup);
    m_lastMessageLabel = new QLabel(QStringLiteral("Last RX: -"), tab);
    m_lastMessageLabel->setStyleSheet(QStringLiteral("color:#455a64;font-size:12px;"));
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
        m_logTable->setItem(0, column, new QTableWidgetItem(values.at(column)));
    }
    while (m_logTable->rowCount() > 1000) m_logTable->removeRow(1000);
    refreshLogFilter();
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
