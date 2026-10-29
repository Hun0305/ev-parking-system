#include "settingspage.h"

#include "widgets/pagehelp.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

SettingsPage::SettingsPage(const QString &configPath, const QString &cameraIp,
                           QWidget *parent,
                           const QString &cameraUsername,
                           const QString &cameraPassword)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *helpLayout = new QHBoxLayout;
    helpLayout->addStretch();
    helpLayout->addWidget(createPageHelpButton(
        this, this,
        {QStringLiteral("settings"), QStringLiteral("Settings"),
         QStringLiteral("Settings 사용 안내"),
         QStringLiteral("카메라·Pi 연결 주소와 서버의 초과주차 정책을 관리합니다."),
         QStringLiteral(
             "<b>1. 카메라 접속 설정</b><br>카메라 IPv4 주소·아이디·비밀번호를 입력하고 <i>Save camera settings</i>를 누르면 로컬 카메라 설정에 저장됩니다.<br><br>"
             "<b>2. Pi 서버 주소 변경</b><br>Protocol, Server IP/Host, API Port를 입력하고 <i>Save and reconnect</i>를 누릅니다. 주소는 이 PC의 <i>client_config.local.ini</i>에 저장됩니다.<br><br>"
             "<b>3. 연결 재시도</b><br><i>Reconnect now</i>는 저장된 주소로 API 연결을 즉시 다시 시도합니다.<br><br>"
             "<b>4. 초과주차 정책</b><br>페이지를 열거나 <i>Refresh</i>를 누르면 서버 설정을 읽습니다. 1분~24시간 범위로 입력하고 <i>Apply</i>하면 저장 후 서버 값을 다시 검증합니다."),
         QStringLiteral(
             "※ Pi 주소 변경 시 MQTT host도 같은 서버 host를 따릅니다. HTTP 허용 여부는 로컬 보안 설정에 따르며 TLS 실패 시 평문으로 자동 전환하지 않습니다.\n"
             "   카메라와 서버 credential은 이 화면에서 편집하지 않으며 Git에 저장하면 안 됩니다.")}));
    layout->addLayout(helpLayout);
    auto *group = new QGroupBox(QStringLiteral("Runtime Configuration"), this);
    auto *grid = new QGridLayout(group);
    m_cameraIpLabel = new QLabel(group);
    m_cameraIpInput = new QLineEdit(group);
    m_cameraIpInput->setPlaceholderText(QStringLiteral("e.g. 192.168.10.20"));
    m_cameraUsernameInput = new QLineEdit(group);
    m_cameraUsernameInput->setObjectName(QStringLiteral("cameraUsernameInput"));
    m_cameraUsernameInput->setPlaceholderText(QStringLiteral("e.g. admin"));
    m_cameraPasswordInput = new QLineEdit(group);
    m_cameraPasswordInput->setObjectName(QStringLiteral("cameraPasswordInput"));
    m_cameraPasswordInput->setEchoMode(QLineEdit::Password);
    m_cameraPasswordInput->setPlaceholderText(QStringLiteral("Camera password"));
    auto *saveCameraButton = new QPushButton(QStringLiteral("Save camera settings"), group);
    saveCameraButton->setObjectName(QStringLiteral("saveCameraSettingsButton"));
    auto *cameraLayout = new QHBoxLayout;
    cameraLayout->addWidget(m_cameraIpInput, 1);
    cameraLayout->addWidget(saveCameraButton);

    m_serverSchemeInput = new QComboBox(group);
    m_serverSchemeInput->setObjectName(QStringLiteral("serverApiSchemeInput"));
    m_serverSchemeInput->addItems(
        {QStringLiteral("http"), QStringLiteral("https")});
    m_serverHostInput = new QLineEdit(group);
    m_serverHostInput->setObjectName(QStringLiteral("serverApiHostInput"));
    m_serverHostInput->setPlaceholderText(QStringLiteral("e.g. raspberry-pi.local"));
    m_serverPortInput = new QSpinBox(group);
    m_serverPortInput->setObjectName(QStringLiteral("serverApiPortInput"));
    m_serverPortInput->setRange(1, 65535);
    m_serverPortInput->setValue(8080);
    auto *saveServerButton = new QPushButton(QStringLiteral("Save and reconnect"), group);
    auto *reconnectButton = new QPushButton(QStringLiteral("Reconnect now"), group);
    auto *serverLayout = new QHBoxLayout;
    serverLayout->addWidget(new QLabel(QStringLiteral("Protocol"), group));
    serverLayout->addWidget(m_serverSchemeInput);
    serverLayout->addWidget(new QLabel(QStringLiteral("Server IP / Host"), group));
    serverLayout->addWidget(m_serverHostInput, 1);
    serverLayout->addWidget(new QLabel(QStringLiteral("API Port"), group));
    serverLayout->addWidget(m_serverPortInput);
    serverLayout->addWidget(saveServerButton);
    serverLayout->addWidget(reconnectButton);
    m_serverConnectionLabel = new QLabel(QStringLiteral("Not connected"), group);

    grid->addWidget(new QLabel(QStringLiteral("Camera config"), group), 0, 0);
    grid->addWidget(new QLabel(configPath, group), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Current camera IP"), group), 1, 0);
    grid->addWidget(m_cameraIpLabel, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Camera IPv4 address"), group), 2, 0);
    grid->addLayout(cameraLayout, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Camera username"), group), 3, 0);
    grid->addWidget(m_cameraUsernameInput, 3, 1);
    grid->addWidget(new QLabel(QStringLiteral("Camera password"), group), 4, 0);
    grid->addWidget(m_cameraPasswordInput, 4, 1);
    grid->addWidget(new QLabel(QStringLiteral("Credentials storage"), group), 5, 0);
    grid->addWidget(new QLabel(QStringLiteral("Saved only to the local camera config. Never commit real passwords."), group), 5, 1);
    grid->addWidget(new QLabel(QStringLiteral("Server API Address"), group), 6, 0);
    grid->addLayout(serverLayout, 6, 1);
    grid->addWidget(new QLabel(QStringLiteral("Server connection"), group), 7, 0);
    grid->addWidget(m_serverConnectionLabel, 7, 1);
    grid->addWidget(new QLabel(QStringLiteral("Network addressing"), group), 8, 0);
    grid->addWidget(new QLabel(QStringLiteral("Enter the server host and API port separately. MQTT follows the Server API host."), group), 8, 1);
    grid->addWidget(new QLabel(QStringLiteral("Server address save target"), group), 9, 0);
    grid->addWidget(new QLabel(QStringLiteral("client_config.local.ini (this PC override)"), group), 9, 1);
    grid->addWidget(new QLabel(QStringLiteral("Channel source"), group), 10, 0);
    grid->addWidget(new QLabel(QStringLiteral("Final URL pattern: /{channel}/{profile}/media.smp"), group), 10, 1);
    layout->addWidget(group);

    auto *overstayGroup = new QGroupBox(QStringLiteral("Overstay Policy"), this);
    auto *overstayGrid = new QGridLayout(overstayGroup);
    m_currentOverstayLabel = new QLabel(QStringLiteral("Not loaded"), overstayGroup);
    m_currentOverstayLabel->setObjectName(QStringLiteral("currentOverstayThresholdLabel"));
    m_applyPolicyLabel = new QLabel(QStringLiteral("Not loaded"), overstayGroup);
    m_applyPolicyLabel->setObjectName(QStringLiteral("overstayApplyPolicyLabel"));

    m_overstayHoursInput = new QSpinBox(overstayGroup);
    m_overstayHoursInput->setObjectName(QStringLiteral("overstayHoursInput"));
    m_overstayHoursInput->setRange(0, 24);
    m_overstayHoursInput->setSuffix(QStringLiteral(" h"));
    m_overstayMinutesInput = new QSpinBox(overstayGroup);
    m_overstayMinutesInput->setObjectName(QStringLiteral("overstayMinutesInput"));
    m_overstayMinutesInput->setRange(0, 59);
    m_overstayMinutesInput->setSuffix(QStringLiteral(" min"));
    m_overstaySecondsInput = new QSpinBox(overstayGroup);
    m_overstaySecondsInput->setObjectName(QStringLiteral("overstaySecondsInput"));
    m_overstaySecondsInput->setRange(0, 59);
    m_overstaySecondsInput->setSuffix(QStringLiteral(" sec"));
    auto *thresholdLayout = new QHBoxLayout;
    thresholdLayout->addWidget(m_overstayHoursInput);
    thresholdLayout->addWidget(m_overstayMinutesInput);
    thresholdLayout->addWidget(m_overstaySecondsInput);
    thresholdLayout->addStretch();

    m_overstayStatusLabel = new QLabel(
        QStringLiteral("Open this page to load the server setting."), overstayGroup);
    m_overstayStatusLabel->setObjectName(QStringLiteral("overstayStatusLabel"));
    m_overstayStatusLabel->setWordWrap(true);
    m_refreshOverstayButton = new QPushButton(QStringLiteral("Refresh"), overstayGroup);
    m_refreshOverstayButton->setObjectName(QStringLiteral("refreshOverstayThresholdButton"));
    m_applyOverstayButton = new QPushButton(QStringLiteral("Apply"), overstayGroup);
    m_applyOverstayButton->setObjectName(QStringLiteral("applyOverstayThresholdButton"));
    auto *overstayButtonLayout = new QHBoxLayout;
    overstayButtonLayout->addStretch();
    overstayButtonLayout->addWidget(m_refreshOverstayButton);
    overstayButtonLayout->addWidget(m_applyOverstayButton);

    overstayGrid->addWidget(new QLabel(QStringLiteral("Current server setting"), overstayGroup), 0, 0);
    overstayGrid->addWidget(m_currentOverstayLabel, 0, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("Apply policy"), overstayGroup), 1, 0);
    overstayGrid->addWidget(m_applyPolicyLabel, 1, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("Overstay threshold"), overstayGroup), 2, 0);
    overstayGrid->addLayout(thresholdLayout, 2, 1);
    overstayGrid->addWidget(new QLabel(QStringLiteral("Status"), overstayGroup), 3, 0);
    overstayGrid->addWidget(m_overstayStatusLabel, 3, 1);
    overstayGrid->addLayout(overstayButtonLayout, 4, 1);
    layout->addWidget(overstayGroup);
    layout->addStretch();
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
        url.setScheme(m_serverSchemeInput->currentText());
        url.setHost(m_serverHostInput->text().trimmed());
        url.setPort(m_serverPortInput->value());
        emit saveServerBaseUrlRequested(url.toString());
    };
    connect(saveCameraButton, &QPushButton::clicked, this, requestCameraSave);
    connect(m_cameraIpInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(m_cameraUsernameInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(m_cameraPasswordInput, &QLineEdit::returnPressed, this, requestCameraSave);
    connect(saveServerButton, &QPushButton::clicked, this, requestServerSave);
    connect(m_serverHostInput, &QLineEdit::returnPressed, this, requestServerSave);
    connect(reconnectButton, &QPushButton::clicked, this, &SettingsPage::reconnectServerRequested);
    connect(m_refreshOverstayButton, &QPushButton::clicked, this, [this]() {
        if (m_overstayRequestInFlight) return;
        setOverstayThresholdRequestStarted(QStringLiteral("Loading the server setting..."));
        emit overstayThresholdRefreshRequested();
    });
    connect(m_applyOverstayButton, &QPushButton::clicked, this, [this]() {
        if (m_overstayRequestInFlight || !m_serverConnected) return;
        const int seconds = overstaySeconds(
            m_overstayHoursInput->value(), m_overstayMinutesInput->value(),
            m_overstaySecondsInput->value());
        if (seconds < 60 || seconds > 86400) {
            m_overstayStatusLabel->setText(QStringLiteral(
                "The threshold must be between 00h 01m 00s and 24h 00m 00s."));
            m_overstayStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        setOverstayThresholdRequestStarted(
            QStringLiteral("Applying the new threshold..."));
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
    const int schemeIndex = m_serverSchemeInput->findText(scheme);
    if (schemeIndex >= 0) m_serverSchemeInput->setCurrentIndex(schemeIndex);
    m_serverHostInput->setText(url.host());
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    m_serverPortInput->setValue(url.port(defaultPort));
}

void SettingsPage::setServerConnectionStatus(const QString &status, bool connected)
{
    m_serverConnected = connected;
    m_serverConnectionLabel->setText(status);
    m_serverConnectionLabel->setStyleSheet(
        connected ? QStringLiteral("color:#1b5e20;font-weight:700;")
                  : QStringLiteral("color:#b71c1c;font-weight:700;"));
    updateOverstayButtons();
}

void SettingsPage::setOverstayThresholdRequestStarted(const QString &status)
{
    m_overstayRequestInFlight = true;
    m_overstayStatusLabel->setText(status);
    m_overstayStatusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
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
            ? QStringLiteral("Overstay threshold changed to %1.")
                  .arg(formatOverstayDuration(seconds))
            : QStringLiteral("Server setting loaded successfully."));
    m_overstayStatusLabel->setStyleSheet(
        QStringLiteral("color:#1b5e20;font-weight:700;"));
    updateOverstayButtons();
}

void SettingsPage::setOverstayThresholdError(const QString &message,
                                              bool updateRequest)
{
    m_overstayRequestInFlight = false;
    const QString prefix = updateRequest
        ? QStringLiteral("Failed to update the overstay threshold.")
        : QStringLiteral("Failed to load the server setting.");
    m_overstayStatusLabel->setText(
        message.trimmed().isEmpty()
            ? prefix
            : prefix + QStringLiteral("\nServer response: ") + message.trimmed());
    m_overstayStatusLabel->setStyleSheet(
        QStringLiteral("color:#b71c1c;font-weight:700;"));
    updateOverstayButtons();
}

int SettingsPage::overstaySeconds(int hours, int minutes, int seconds)
{
    return hours * 3600 + minutes * 60 + seconds;
}

void SettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_overstayRequestInFlight) return;
    setOverstayThresholdRequestStarted(QStringLiteral("Loading the server setting..."));
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
