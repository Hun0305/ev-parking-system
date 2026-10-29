#include "settingspage.h"

#include "pages/systemuistyle.h"
#include "widgets/pagehelp.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
void setUiBannerTone(QLabel *label, const QString &tone)
{
    if (!label) return;
    label->setProperty("uiBanner", tone);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
}

SettingsPage::SettingsPage(const QString &configPath, const QString &cameraIp,
                           QWidget *parent,
                           const QString &cameraUsername,
                           const QString &cameraPassword)
    : QWidget(parent)
{
    setStyleSheet(SystemUiStyle::pageStyleSheet());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(createPageHeader(
        this, QStringLiteral("System"),
        QStringLiteral("Configure connections, policies, diagnostics, and local test tools")));
    createPageHelpButton(
        this, this,
        {QStringLiteral("settings"), QStringLiteral("System"),
         QStringLiteral("System 사용 안내"),
         QStringLiteral("연결 설정, 운영 정책, 런타임 진단과 로컬 테스트 도구를 한 화면에서 관리합니다."),
         QStringLiteral(
             "<b>1. Configuration</b><br>카메라 접근 정보, Pi API endpoint와 초과주차 정책을 한 화면에서 관리합니다.<br><br>"
             "<b>2. Diagnostics</b><br>Pi API, RTSP 채널과 주차 데이터 상태·지연·마지막 오류를 읽기 전용으로 확인합니다.<br><br>"
             "<b>3. Live Logs</b><br>Level, Module과 검색어로 로컬 Qt 진단 로그를 필터링합니다.<br><br>"
             "<b>4. Test Tools</b><br>Mock EV, 테스트 이벤트와 normalized RX 메시지를 로컬 Qt 프로세스에만 적용합니다."),
         QStringLiteral(
             "※ Pi 주소 변경 시 MQTT host도 같은 서버 host를 따릅니다. TLS 실패 시 평문으로 자동 전환하지 않습니다.\n"
             "   Test Tools는 Pi 서버로 전송되지 않는 로컬 simulation sandbox이며 실제 장비 통합 성공 증거가 아닙니다.")});

    auto *systemBanner = new QLabel(
        QStringLiteral("SYSTEM MANAGEMENT  ·  Configure endpoints and policy, inspect runtime health, and run local test scenarios."),
        this);
    systemBanner->setObjectName(QStringLiteral("systemScopeBanner"));
    systemBanner->setWordWrap(true);
    systemBanner->setProperty("uiBanner", QStringLiteral("info"));
    layout->addWidget(systemBanner);

    m_systemTabs = new QTabWidget(this);
    m_systemTabs->setObjectName(QStringLiteral("systemTabWidget"));
    m_systemTabs->setDocumentMode(true);

    auto *configurationScroll = new QScrollArea(m_systemTabs);
    configurationScroll->setObjectName(QStringLiteral("systemConnectionsScrollArea"));
    configurationScroll->setWidgetResizable(true);
    configurationScroll->setFrameShape(QFrame::NoFrame);
    configurationScroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *configuration = new QWidget(configurationScroll);
    configuration->setProperty("systemTabContent", true);
    configuration->setMaximumWidth(SystemUiStyle::ConnectionsMaxWidth);
    auto *configurationLayout = new QVBoxLayout(configuration);
    configurationLayout->setContentsMargins(16, 16, 16, 16);
    configurationLayout->setSpacing(12);
    configurationScroll->setWidget(configuration);

    auto *configurationNote = new QLabel(
        QStringLiteral("CONNECTION CHANGES · Applying a Pi endpoint updates this PC's local override and reconnects the API. MQTT follows the same host."),
        configuration);
    configurationNote->setWordWrap(true);
    configurationNote->setProperty("uiBanner", QStringLiteral("warning"));
    configurationLayout->addWidget(configurationNote);

    auto *group = new QGroupBox(QStringLiteral("Camera & Pi Server"), configuration);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(12, 14, 12, 12);
    m_cameraIpLabel = new QLabel(group);
    m_cameraIpInput = new QLineEdit(group);
    m_cameraIpInput->setPlaceholderText(QStringLiteral("e.g. 192.168.10.20"));
    m_cameraIpInput->setClearButtonEnabled(true);
    m_cameraUsernameInput = new QLineEdit(group);
    m_cameraUsernameInput->setObjectName(QStringLiteral("cameraUsernameInput"));
    m_cameraUsernameInput->setPlaceholderText(QStringLiteral("e.g. admin"));
    m_cameraUsernameInput->setClearButtonEnabled(true);
    m_cameraPasswordInput = new QLineEdit(group);
    m_cameraPasswordInput->setObjectName(QStringLiteral("cameraPasswordInput"));
    m_cameraPasswordInput->setEchoMode(QLineEdit::Password);
    m_cameraPasswordInput->setPlaceholderText(QStringLiteral("Camera password"));
    auto *saveCameraButton = new QPushButton(QStringLiteral("Save camera access"), group);
    saveCameraButton->setObjectName(QStringLiteral("saveCameraSettingsButton"));
    auto *cameraLayout = new QHBoxLayout;
    cameraLayout->addWidget(m_cameraIpInput, 1);
    auto *passwordLayout = new QHBoxLayout;
    passwordLayout->addWidget(m_cameraPasswordInput, 1);
    auto *showPassword = new QCheckBox(QStringLiteral("Show"), group);
    showPassword->setObjectName(QStringLiteral("showCameraPasswordCheck"));
    showPassword->setToolTip(QStringLiteral("Show the camera password on this screen"));
    passwordLayout->addWidget(showPassword);
    auto *cameraActions = new QHBoxLayout;
    cameraActions->addStretch();
    cameraActions->addWidget(saveCameraButton);

    m_serverHttpRadio = new QRadioButton(QStringLiteral("HTTP"), group);
    m_serverHttpRadio->setObjectName(QStringLiteral("serverApiHttpRadio"));
    m_serverHttpsRadio = new QRadioButton(QStringLiteral("HTTPS"), group);
    m_serverHttpsRadio->setObjectName(QStringLiteral("serverApiHttpsRadio"));
    m_serverHttpRadio->setChecked(true);
    m_serverHostInput = new QLineEdit(group);
    m_serverHostInput->setObjectName(QStringLiteral("serverApiHostInput"));
    m_serverHostInput->setPlaceholderText(QStringLiteral("e.g. raspberry-pi.local"));
    m_serverHostInput->setClearButtonEnabled(true);
    m_serverPortInput = new QSpinBox(group);
    m_serverPortInput->setObjectName(QStringLiteral("serverApiPortInput"));
    m_serverPortInput->setRange(1, 65535);
    m_serverPortInput->setValue(8080);
    auto *saveServerButton = new QPushButton(QStringLiteral("Apply & reconnect"), group);
    saveServerButton->setObjectName(QStringLiteral("saveServerSettingsButton"));
    auto *reconnectButton = new QPushButton(QStringLiteral("Retry connection"), group);
    reconnectButton->setObjectName(QStringLiteral("reconnectServerButton"));
    auto *serverLayout = new QHBoxLayout;
    serverLayout->addWidget(m_serverHostInput, 1);
    serverLayout->addWidget(m_serverPortInput);
    auto *protocolLayout = new QHBoxLayout;
    protocolLayout->setSpacing(20);
    protocolLayout->addWidget(m_serverHttpRadio);
    protocolLayout->addWidget(m_serverHttpsRadio);
    protocolLayout->addStretch();
    auto *serverActions = new QHBoxLayout;
    serverActions->addStretch();
    serverActions->addWidget(reconnectButton);
    serverActions->addWidget(saveServerButton);
    m_serverConnectionLabel = new QLabel(QStringLiteral("Not connected"), group);

    auto makeSectionLabel = [group](const QString &text) {
        auto *label = new QLabel(text, group);
        label->setStyleSheet(QStringLiteral(
            "color:#34515e;font-size:12px;font-weight:900;"
            "border-bottom:1px solid #d7e0e5;padding:4px 0;"));
        return label;
    };
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(9);
    grid->setColumnMinimumWidth(0, 168);
    grid->setColumnStretch(1, 1);
    grid->addWidget(makeSectionLabel(QStringLiteral("CAMERA ACCESS")), 0, 0, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Configuration file"), group), 1, 0);
    auto *configPathLabel = new QLabel(configPath, group);
    configPathLabel->setWordWrap(true);
    configPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(configPathLabel, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Active IP"), group), 2, 0);
    grid->addWidget(m_cameraIpLabel, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Camera IP address"), group), 3, 0);
    grid->addLayout(cameraLayout, 3, 1);
    grid->addWidget(new QLabel(QStringLiteral("Username"), group), 4, 0);
    grid->addWidget(m_cameraUsernameInput, 4, 1);
    grid->addWidget(new QLabel(QStringLiteral("Password"), group), 5, 0);
    grid->addLayout(passwordLayout, 5, 1);
    grid->addWidget(new QLabel(QStringLiteral("Credential storage"), group), 6, 0);
    auto *credentialNote = new QLabel(
        QStringLiteral("Local camera config only · Never commit real credentials."), group);
    credentialNote->setWordWrap(true);
    grid->addWidget(credentialNote, 6, 1);
    grid->addLayout(cameraActions, 7, 1);
    grid->addWidget(makeSectionLabel(QStringLiteral("PI SERVER")), 8, 0, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("API protocol"), group), 9, 0);
    grid->addLayout(protocolLayout, 9, 1);
    grid->addWidget(new QLabel(QStringLiteral("API endpoint"), group), 10, 0);
    grid->addLayout(serverLayout, 10, 1);
    grid->addWidget(new QLabel(QStringLiteral("Endpoint format"), group), 11, 0);
    grid->addWidget(new QLabel(QStringLiteral("Host  ·  Port"), group), 11, 1);
    grid->addWidget(new QLabel(QStringLiteral("Connection status"), group), 12, 0);
    grid->addWidget(m_serverConnectionLabel, 12, 1);
    grid->addWidget(new QLabel(QStringLiteral("MQTT routing"), group), 13, 0);
    grid->addWidget(new QLabel(QStringLiteral("Automatically follows the Pi API host."), group), 13, 1);
    grid->addWidget(new QLabel(QStringLiteral("Local override file"), group), 14, 0);
    grid->addWidget(new QLabel(QStringLiteral("client_config.local.ini"), group), 14, 1);
    grid->addWidget(new QLabel(QStringLiteral("RTSP URL pattern"), group), 15, 0);
    grid->addWidget(new QLabel(QStringLiteral("/{channel}/{profile}/media.smp"), group), 15, 1);
    grid->addLayout(serverActions, 16, 1);
    configurationLayout->addWidget(group);

    auto *policyNote = new QLabel(
        QStringLiteral("SERVER POLICY · Reload reads the current value from the Pi server. Apply writes the new value and verifies the server response."),
        configuration);
    policyNote->setWordWrap(true);
    policyNote->setProperty("uiBanner", QStringLiteral("success"));
    configurationLayout->addWidget(policyNote);

    auto *overstayGroup = new QGroupBox(QStringLiteral("Parking Policy"), configuration);
    overstayGroup->setObjectName(QStringLiteral("parkingPolicyGroup"));
    auto *overstayGrid = new QGridLayout(overstayGroup);
    overstayGrid->setContentsMargins(12, 14, 12, 12);
    overstayGrid->setHorizontalSpacing(14);
    overstayGrid->setVerticalSpacing(10);
    m_currentOverstayLabel = new QLabel(QStringLiteral("Not loaded"), overstayGroup);
    m_currentOverstayLabel->setObjectName(QStringLiteral("currentOverstayThresholdLabel"));
    m_currentOverstayLabel->setProperty("uiReadOnlyValue", true);
    m_applyPolicyLabel = new QLabel(QStringLiteral("Not loaded"), overstayGroup);
    m_applyPolicyLabel->setObjectName(QStringLiteral("overstayApplyPolicyLabel"));
    m_applyPolicyLabel->setWordWrap(true);
    m_applyPolicyLabel->setProperty("uiReadOnlyValue", true);

    m_overstayHoursInput = new QSpinBox(overstayGroup);
    m_overstayHoursInput->setObjectName(QStringLiteral("overstayHoursInput"));
    m_overstayHoursInput->setRange(0, 24);
    m_overstayHoursInput->setSuffix(QStringLiteral("h"));
    m_overstayMinutesInput = new QSpinBox(overstayGroup);
    m_overstayMinutesInput->setObjectName(QStringLiteral("overstayMinutesInput"));
    m_overstayMinutesInput->setRange(0, 59);
    m_overstayMinutesInput->setSuffix(QStringLiteral("m"));
    m_overstaySecondsInput = new QSpinBox(overstayGroup);
    m_overstaySecondsInput->setObjectName(QStringLiteral("overstaySecondsInput"));
    m_overstaySecondsInput->setRange(0, 59);
    m_overstaySecondsInput->setSuffix(QStringLiteral("s"));
    for (QSpinBox *input : {m_overstayHoursInput, m_overstayMinutesInput,
                            m_overstaySecondsInput}) {
        input->setProperty("uiDurationInput", true);
        input->setMinimumHeight(34);
        input->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        input->setAlignment(Qt::AlignCenter);
    }
    m_overstayHoursInput->setAccessibleName(QStringLiteral("Hours"));
    m_overstayMinutesInput->setAccessibleName(QStringLiteral("Minutes"));
    m_overstaySecondsInput->setAccessibleName(QStringLiteral("Seconds"));

    auto *thresholdLayout = new QHBoxLayout;
    thresholdLayout->setObjectName(QStringLiteral("overstayDurationLayout"));
    thresholdLayout->setContentsMargins(0, 0, 0, 0);
    thresholdLayout->setSpacing(12);
    thresholdLayout->addWidget(m_overstayHoursInput);
    thresholdLayout->addWidget(m_overstayMinutesInput);
    thresholdLayout->addWidget(m_overstaySecondsInput);
    thresholdLayout->addStretch();

    m_overstayStatusLabel = new QLabel(
        QStringLiteral("Open System to load the current policy."), overstayGroup);
    m_overstayStatusLabel->setObjectName(QStringLiteral("overstayStatusLabel"));
    m_overstayStatusLabel->setWordWrap(true);
    m_overstayStatusLabel->setMinimumHeight(38);
    m_overstayStatusLabel->setProperty("uiBanner", QStringLiteral("neutral"));
    m_refreshOverstayButton = new QPushButton(QStringLiteral("Reload"), overstayGroup);
    m_refreshOverstayButton->setObjectName(QStringLiteral("refreshOverstayThresholdButton"));
    m_applyOverstayButton = new QPushButton(QStringLiteral("Apply policy"), overstayGroup);
    m_applyOverstayButton->setObjectName(QStringLiteral("applyOverstayThresholdButton"));
    auto *overstayButtonLayout = new QHBoxLayout;
    overstayButtonLayout->addStretch();
    overstayButtonLayout->addWidget(m_refreshOverstayButton);
    overstayButtonLayout->addWidget(m_applyOverstayButton);

    overstayGrid->addWidget(new QLabel(QStringLiteral("Current threshold"), overstayGroup), 0, 0);
    overstayGrid->addWidget(m_currentOverstayLabel, 0, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("Applies to"), overstayGroup), 1, 0);
    overstayGrid->addWidget(m_applyPolicyLabel, 1, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("New threshold"), overstayGroup), 2, 0);
    overstayGrid->addLayout(thresholdLayout, 2, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("Policy update"), overstayGroup), 3, 0);
    overstayGrid->addWidget(m_overstayStatusLabel, 3, 1);
    overstayGrid->addLayout(overstayButtonLayout, 4, 1);
    configurationLayout->addWidget(overstayGroup);
    configurationLayout->addStretch();
    m_systemTabs->addTab(configurationScroll, QStringLiteral("Configuration"));
    layout->addWidget(m_systemTabs, 1);

    saveCameraButton->setProperty("uiActionRole", QStringLiteral("primary"));
    saveServerButton->setProperty("uiActionRole", QStringLiteral("primary"));
    m_applyOverstayButton->setProperty("uiActionRole", QStringLiteral("primary"));
    reconnectButton->setProperty("uiActionRole", QStringLiteral("secondary"));
    m_refreshOverstayButton->setProperty("uiActionRole", QStringLiteral("secondary"));
    for (QWidget *field : {static_cast<QWidget *>(m_cameraIpInput),
                           static_cast<QWidget *>(m_cameraUsernameInput),
                           static_cast<QWidget *>(m_cameraPasswordInput),
                           static_cast<QWidget *>(m_serverHostInput),
                           static_cast<QWidget *>(m_serverPortInput)}) {
        field->setMinimumHeight(32);
    }
    m_serverPortInput->setFixedWidth(110);
    setCameraIp(cameraIp);
    setCameraCredentials(cameraUsername, cameraPassword);

    auto requestCameraSave = [this]() {
        emit saveCameraCredentialsRequested(
            m_cameraIpInput->text().trimmed(),
            m_cameraUsernameInput->text().trimmed(),
            m_cameraPasswordInput->text());
    };
    auto requestServerSave = [this]() {
        QUrl url;
        url.setScheme(m_serverHttpsRadio->isChecked()
                          ? QStringLiteral("https")
                          : QStringLiteral("http"));
        url.setHost(m_serverHostInput->text().trimmed());
        url.setPort(m_serverPortInput->value());
        emit saveServerBaseUrlRequested(url.toString());
    };
    connect(saveCameraButton, &QPushButton::clicked, this, requestCameraSave);
    connect(m_cameraIpInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(m_cameraUsernameInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(m_cameraPasswordInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(showPassword, &QCheckBox::toggled, this, [this](bool checked) {
        m_cameraPasswordInput->setEchoMode(
            checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(saveServerButton, &QPushButton::clicked, this, requestServerSave);
    connect(m_serverHostInput, &QLineEdit::returnPressed, this, requestServerSave);
    connect(reconnectButton, &QPushButton::clicked, this, &SettingsPage::reconnectServerRequested);
    connect(m_refreshOverstayButton, &QPushButton::clicked, this, [this]() {
        if (m_overstayRequestInFlight) return;
        setOverstayThresholdRequestStarted(QStringLiteral("Loading the current parking policy..."));
        emit overstayThresholdRefreshRequested();
    });
    connect(m_applyOverstayButton, &QPushButton::clicked, this, [this]() {
        if (m_overstayRequestInFlight || !m_serverConnected) return;
        const int seconds = overstaySeconds(
            m_overstayHoursInput->value(), m_overstayMinutesInput->value(),
            m_overstaySecondsInput->value());
        if (seconds < 60 || seconds > 86400) {
            m_overstayStatusLabel->setText(QStringLiteral(
                "Enter a threshold between 00h 01m 00s and 24h 00m 00s."));
            setUiBannerTone(m_overstayStatusLabel, QStringLiteral("danger"));
            return;
        }
        setOverstayThresholdRequestStarted(
            QStringLiteral("Applying the parking policy..."));
        emit overstayThresholdUpdateRequested(seconds);
    });
    connect(m_overstayHoursInput, &QSpinBox::valueChanged, this,
            [this](int hours) {
        if (hours != 24) return;
        m_overstayMinutesInput->setValue(0);
        m_overstaySecondsInput->setValue(0);
    });
    updateOverstayButtons();
}

void SettingsPage::setCameraIp(const QString &cameraIp)
{
    m_cameraIpLabel->setText(cameraIp.isEmpty() ? QStringLiteral("Not configured") : cameraIp);
    m_cameraIpInput->setText(cameraIp);
}

void SettingsPage::setCameraCredentials(const QString &username,
                                         const QString &password)
{
    m_cameraUsernameInput->setText(username);
    m_cameraPasswordInput->setText(password);
}

void SettingsPage::setServerBaseUrl(const QString &baseUrl)
{
    const QUrl url(baseUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        m_serverHostInput->clear();
        return;
    }

    const QString scheme = url.scheme().toLower();
    m_serverHttpsRadio->setChecked(scheme == QStringLiteral("https"));
    m_serverHttpRadio->setChecked(scheme != QStringLiteral("https"));
    m_serverHostInput->setText(url.host());
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    m_serverPortInput->setValue(url.port(defaultPort));
}

void SettingsPage::setServerConnectionStatus(const QString &status, bool connected)
{
    m_serverConnected = connected;
    m_serverConnectionLabel->setText(status);
    m_serverConnectionLabel->setStyleSheet(
        connected
            ? QStringLiteral("color:#1b5e20;background:#e8f5e9;border:1px solid #a5d6a7;"
                             "border-radius:5px;padding:5px 8px;font-weight:800;")
            : QStringLiteral("color:#b71c1c;background:#ffebee;border:1px solid #ef9a9a;"
                             "border-radius:5px;padding:5px 8px;font-weight:800;"));
    updateOverstayButtons();
}

void SettingsPage::setOverstayThresholdRequestStarted(const QString &status)
{
    m_overstayRequestInFlight = true;
    m_overstayStatusLabel->setText(status);
    setUiBannerTone(m_overstayStatusLabel, QStringLiteral("neutral"));
    updateOverstayButtons();
}

void SettingsPage::setOverstayThreshold(int seconds,
                                        const QString &applyPolicy,
                                        bool afterUpdate)
{
    if (seconds < 60 || seconds > 86400) {
        setOverstayThresholdError(
            QStringLiteral("The server returned an out-of-range threshold."),
            afterUpdate);
        return;
    }

    m_serverOverstaySeconds = seconds;
    m_overstayHoursInput->setValue(seconds / 3600);
    m_overstayMinutesInput->setValue((seconds % 3600) / 60);
    m_overstaySecondsInput->setValue(seconds % 60);
    m_currentOverstayLabel->setText(formatOverstayDuration(seconds));
    m_applyPolicyLabel->setText(
        applyPolicy == QStringLiteral("ACTIVE_AND_NEW_SESSIONS")
            ? QStringLiteral("Active and new sessions (ACTIVE_AND_NEW_SESSIONS)")
            : (applyPolicy.isEmpty() ? QStringLiteral("Not provided") : applyPolicy));
    m_overstayRequestInFlight = false;
    m_overstayStatusLabel->setText(
        afterUpdate
            ? QStringLiteral("Parking policy updated to %1.")
                  .arg(formatOverstayDuration(seconds))
            : QStringLiteral("Current parking policy loaded."));
    setUiBannerTone(m_overstayStatusLabel, QStringLiteral("success"));
    updateOverstayButtons();
}

void SettingsPage::setOverstayThresholdError(const QString &message,
                                              bool updateRequest)
{
    m_overstayRequestInFlight = false;
    const QString prefix = updateRequest
        ? QStringLiteral("Failed to update the parking policy.")
        : QStringLiteral("Failed to load the parking policy.");
    m_overstayStatusLabel->setText(
        message.trimmed().isEmpty()
            ? prefix
            : prefix + QStringLiteral("\nServer response: ") + message.trimmed());
    setUiBannerTone(m_overstayStatusLabel, QStringLiteral("danger"));
    updateOverstayButtons();
}

int SettingsPage::overstaySeconds(int hours, int minutes, int seconds)
{
    return hours * 3600 + minutes * 60 + seconds;
}

QTabWidget *SettingsPage::systemTabs() const
{
    return m_systemTabs;
}

void SettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_overstayRequestInFlight) return;
    setOverstayThresholdRequestStarted(QStringLiteral("Loading the current parking policy..."));
    emit overstayThresholdRefreshRequested();
}

void SettingsPage::updateOverstayButtons()
{
    m_applyOverstayButton->setEnabled(
        m_serverConnected && !m_overstayRequestInFlight);
    m_refreshOverstayButton->setEnabled(!m_overstayRequestInFlight);
    m_overstayHoursInput->setEnabled(!m_overstayRequestInFlight);
    m_overstayMinutesInput->setEnabled(!m_overstayRequestInFlight);
    m_overstaySecondsInput->setEnabled(!m_overstayRequestInFlight);
}

QString SettingsPage::formatOverstayDuration(int seconds)
{
    const int bounded = qMax(0, seconds);
    return QStringLiteral("%1h %2m %3s")
        .arg(bounded / 3600, 2, 10, QLatin1Char('0'))
        .arg((bounded % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(bounded % 60, 2, 10, QLatin1Char('0'));
}
