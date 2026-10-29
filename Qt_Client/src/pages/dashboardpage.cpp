#include "dashboardpage.h"

#include "RtspVideoItem.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

DashboardPage::DashboardPage(const QStringList &lowRtspUrls,
                             const QStringList &highRtspUrls,
                             QWidget *parent)
    : QWidget(parent)
{
    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    auto *helpLayout = new QHBoxLayout;
    helpLayout->addStretch();
    auto *helpButton = new QPushButton(QStringLiteral("Dashboard 안내"), this);
    helpButton->setObjectName(QStringLiteral("dashboardHelpButton"));
    helpButton->setAccessibleName(QStringLiteral("Dashboard 관제 안내"));
    helpButton->setToolTip(QStringLiteral("Dashboard 화면 구성과 관제 흐름 보기"));
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setIcon(pageHelpIcon());
    helpButton->setIconSize(QSize(22, 22));
    helpButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238; color:white; border:1px solid #455a64; "
        "border-radius:6px; padding:6px 11px; font-weight:800; }"
        "QPushButton:hover { background:#37474f; border-color:#fb8c00; }"
        "QPushButton:pressed { background:#1c252a; }"));
    helpLayout->addWidget(helpButton);
    pageLayout->addLayout(helpLayout);

    auto *topLayout = new QHBoxLayout;
    topLayout->setSpacing(10);
    auto *videoGroup = new QGroupBox(QStringLiteral("4-Channel RTSP Monitor"), this);
    m_videoGrid = new QGridLayout(videoGroup);
    m_videoGrid->setSpacing(8);
    m_videoGrid->addWidget(createVideoChannel(0, QStringLiteral("CH1"), QStringLiteral("Original Stream"), lowRtspUrls.value(0), highRtspUrls.value(0)), 0, 0);
    m_videoGrid->addWidget(createVideoChannel(1, QStringLiteral("CH2"), QStringLiteral("Plate ROI"), lowRtspUrls.value(1), highRtspUrls.value(1)), 0, 1);
    m_videoGrid->addWidget(createVideoChannel(2, QStringLiteral("CH3"), QStringLiteral("Analysis Overlay"), lowRtspUrls.value(2), highRtspUrls.value(2)), 1, 0);
    m_videoGrid->addWidget(createVideoChannel(3, QStringLiteral("CH4"), QStringLiteral("Event Snapshot"), lowRtspUrls.value(3), highRtspUrls.value(3)), 1, 1);
    topLayout->addWidget(videoGroup, 4);

    auto *summaryGroup = new QGroupBox(QStringLiteral("Current Summary"), this);
    auto *summaryGrid = new QGridLayout(summaryGroup);
    m_totalSlotsLabel = new QLabel(QStringLiteral("0"), summaryGroup);
    m_occupiedSlotsLabel = new QLabel(QStringLiteral("0"), summaryGroup);
    m_vacantSlotsLabel = new QLabel(QStringLiteral("0"), summaryGroup);
    m_sensorErrorLabel = new QLabel(QStringLiteral("0"), summaryGroup);
    summaryGrid->addWidget(new QLabel(QStringLiteral("Total slots"), summaryGroup), 0, 0);
    summaryGrid->addWidget(m_totalSlotsLabel, 0, 1);
    summaryGrid->addWidget(new QLabel(QStringLiteral("Occupied"), summaryGroup), 1, 0);
    summaryGrid->addWidget(m_occupiedSlotsLabel, 1, 1);
    summaryGrid->addWidget(new QLabel(QStringLiteral("Vacant"), summaryGroup), 2, 0);
    summaryGrid->addWidget(m_vacantSlotsLabel, 2, 1);
    summaryGrid->addWidget(new QLabel(QStringLiteral("Hall errors"), summaryGroup), 3, 0);
    summaryGrid->addWidget(m_sensorErrorLabel, 3, 1);
    topLayout->addWidget(summaryGroup, 1);
    pageLayout->addLayout(topLayout, 4);

    m_recentEventTable = new QTableWidget(0, 5, this);
    m_recentEventTable->setObjectName(QStringLiteral("recentEventTable"));
    m_recentEventTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Zone"), QStringLiteral("Event"), QStringLiteral("Message"), QStringLiteral("Status")});
    m_recentEventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_recentEventTable->verticalHeader()->setVisible(false);
    m_recentEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recentEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto *recentGroup = new QGroupBox(QStringLiteral("Recent Events"), this);
    auto *recentLayout = new QVBoxLayout(recentGroup);
    auto *recentHint = new QLabel(
        QStringLiteral("Double-click an event to open the parking session recorded by its event ID."), recentGroup);
    recentHint->setStyleSheet(QStringLiteral("color:#607d8b;"));
    recentLayout->addWidget(recentHint);
    recentLayout->addWidget(m_recentEventTable);
    pageLayout->addWidget(recentGroup, 1);
    startDelayedVideoStreams();

    m_diagnosticTimer = new QTimer(this);
    m_diagnosticTimer->setInterval(1000);
    connect(m_diagnosticTimer, &QTimer::timeout,
            this, &DashboardPage::publishRtspDiagnostics);
    m_diagnosticTimer->start();
    QTimer::singleShot(0, this, &DashboardPage::publishRtspDiagnostics);
    connect(m_recentEventTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                const QTableWidgetItem *eventItem =
                    m_recentEventTable->item(row, 0);
                const QString eventId = eventItem
                    ? eventItem->data(Qt::UserRole).toString().trimmed()
                    : QString();
                if (!eventId.isEmpty()) {
                    emit eventEvidenceRequested(eventId);
                }
            });
    connect(helpButton, &QPushButton::clicked,
            this, &DashboardPage::showHelpDialog);
}

void DashboardPage::showHelpDialog()
{
    if (QDialog *existing = findChild<QDialog *>(
            QStringLiteral("dashboardHelpDialog"))) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("dashboardHelpDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Dashboard 관제 가이드"));
    dialog->setModal(true);
    dialog->setMinimumSize(760, 560);
    dialog->resize(900, 720);

    auto *dialogLayout = new QVBoxLayout(dialog);
    dialogLayout->setContentsMargins(14, 14, 14, 14);
    dialogLayout->setSpacing(10);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setObjectName(QStringLiteral("dashboardHelpScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("Dashboard 관제 가이드"), content);
    title->setObjectName(QStringLiteral("dashboardHelpTitle"));
    title->setStyleSheet(QStringLiteral(
        "font-size:22px;font-weight:900;color:#1f2d35;"));
    layout->addWidget(title);
    auto *intro = new QLabel(
        QStringLiteral("Dashboard는 설정을 변경하는 화면이 아니라, 영상·주차 현황·최근 이벤트를 빠르게 훑는 실시간 관제 시작점입니다."),
        content);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral(
        "background:#e3f2fd;color:#0d47a1;border:1px solid #90caf9;"
        "border-radius:7px;padding:10px;font-weight:700;"));
    layout->addWidget(intro);

    auto *monitorGroup = new QGroupBox(
        QStringLiteral("4-Channel RTSP Monitor · 무엇을 보는 영역인가"), content);
    auto *monitorLayout = new QVBoxLayout(monitorGroup);
    auto *monitorHint = new QLabel(
        QStringLiteral("각 채널은 RTSP 상태·해상도·Frame 시간을 함께 표시합니다. 채널 화면을 클릭하면 단일 채널로 확대되고 다시 클릭하면 4분할로 복귀합니다."),
        monitorGroup);
    monitorHint->setWordWrap(true);
    monitorLayout->addWidget(monitorHint);

    auto *videoGridWidget = new QWidget(monitorGroup);
    videoGridWidget->setObjectName(QStringLiteral("dashboardHelpVideoGrid"));
    auto *videoGrid = new QGridLayout(videoGridWidget);
    videoGrid->setContentsMargins(0, 4, 0, 0);
    videoGrid->setSpacing(8);
    const QStringList channelTitles{
        QStringLiteral("Original Stream"), QStringLiteral("Plate ROI"),
        QStringLiteral("Analysis Overlay"), QStringLiteral("Event Snapshot")};
    for (int channel = 0; channel < 4; ++channel) {
        auto *preview = new QFrame(videoGridWidget);
        const bool fireExample = channel == 1;
        preview->setStyleSheet(QStringLiteral(
            "QFrame { background:#111820; border:%1px solid %2; border-radius:6px; }")
                                   .arg(fireExample ? 3 : 1)
                                   .arg(fireExample ? QStringLiteral("#ff1744")
                                                    : QStringLiteral("#455a64")));
        auto *previewLayout = new QVBoxLayout(preview);
        previewLayout->setContentsMargins(10, 8, 10, 8);
        auto *channelLabel = new QLabel(
            QStringLiteral("CH%1 · %2")
                .arg(channel + 1).arg(channelTitles.at(channel)), preview);
        channelLabel->setStyleSheet(QStringLiteral(
            "border:none;color:white;font-weight:800;"));
        auto *statusLabel = new QLabel(
            fireExample
                ? QStringLiteral("FIRE 활성 예시 · 빨간 테두리 점멸")
                : QStringLiteral("RTSP 상태 · 해상도 · Frame 시간"), preview);
        statusLabel->setStyleSheet(QStringLiteral(
            "border:none;color:%1;font-size:11px;")
                                       .arg(fireExample
                                                ? QStringLiteral("#ff8a80")
                                                : QStringLiteral("#b0bec5")));
        previewLayout->addWidget(channelLabel);
        previewLayout->addWidget(statusLabel);
        videoGrid->addWidget(preview, channel / 2, channel % 2);
    }
    monitorLayout->addWidget(videoGridWidget);
    layout->addWidget(monitorGroup);

    auto *summaryGroup = new QGroupBox(
        QStringLiteral("Current Summary · 숫자를 어떻게 해석하는가"), content);
    auto *summaryLayout = new QHBoxLayout(summaryGroup);
    summaryLayout->setSpacing(8);
    auto *summaryCards = new QWidget(summaryGroup);
    summaryCards->setObjectName(QStringLiteral("dashboardHelpSummaryCards"));
    auto *cardLayout = new QHBoxLayout(summaryCards);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(8);
    const QList<QPair<QString, QString>> metrics{
        {QStringLiteral("Total slots"), QStringLiteral("EV + General 전체")},
        {QStringLiteral("Occupied"), QStringLiteral("현재 점유")},
        {QStringLiteral("Vacant"), QStringLiteral("현재 빈자리")},
        {QStringLiteral("Hall errors"), QStringLiteral("센서 오류")}};
    for (const auto &metric : metrics) {
        auto *card = new QFrame(summaryCards);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:white;border:1px solid #cfd8dc;border-radius:6px; }"));
        auto *metricLayout = new QVBoxLayout(card);
        auto *name = new QLabel(metric.first, card);
        name->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:800;"));
        auto *meaning = new QLabel(metric.second, card);
        meaning->setWordWrap(true);
        meaning->setStyleSheet(QStringLiteral(
            "border:none;color:#607d8b;font-size:11px;"));
        metricLayout->addWidget(name);
        metricLayout->addWidget(meaning);
        cardLayout->addWidget(card, 1);
    }
    summaryLayout->addWidget(summaryCards, 1);
    layout->addWidget(summaryGroup);

    auto *eventGroup = new QGroupBox(
        QStringLiteral("Recent Events · 다음 화면으로 이동하는 방법"), content);
    eventGroup->setObjectName(QStringLiteral("dashboardHelpEventFlow"));
    auto *eventLayout = new QHBoxLayout(eventGroup);
    auto makeFlowCard = [eventGroup](const QString &titleText,
                                     const QString &body,
                                     const QString &color) {
        auto *card = new QFrame(eventGroup);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:%1;border:1px solid #cfd8dc;border-radius:6px; }")
                                .arg(color));
        auto *cardLayout = new QVBoxLayout(card);
        auto *cardTitle = new QLabel(titleText, card);
        cardTitle->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:800;"));
        auto *cardBody = new QLabel(body, card);
        cardBody->setWordWrap(true);
        cardBody->setStyleSheet(QStringLiteral(
            "border:none;color:#455a64;font-size:11px;"));
        cardLayout->addWidget(cardTitle);
        cardLayout->addWidget(cardBody);
        return card;
    };
    eventLayout->addWidget(makeFlowCard(
        QStringLiteral("Recent Events"),
        QStringLiteral("Time · Zone · Event · Message · Status\n최근 5건만 표시"),
        QStringLiteral("#f5f7f9")), 1);
    eventLayout->addWidget(new QLabel(QStringLiteral("→"), eventGroup));
    eventLayout->addWidget(makeFlowCard(
        QStringLiteral("주차 이벤트 더블클릭"),
        QStringLiteral("선택한 이벤트와 연결된 Evidence 화면 열기"),
        QStringLiteral("#fff8e1")), 1);
    eventLayout->addWidget(new QLabel(QStringLiteral("→"), eventGroup));
    eventLayout->addWidget(makeFlowCard(
        QStringLiteral("Evidence"),
        QStringLiteral("해당 슬롯 및 주차 세션 증거 확인"),
        QStringLiteral("#e8f5e9")), 1);
    layout->addWidget(eventGroup);

    auto *troubleshooting = new QLabel(
        QStringLiteral(
            "문제가 보일 때\n"
            "• 영상 안의 상태·오류 문구와 Frame 시간이 멈췄는지 먼저 확인합니다.\n"
            "• 연결 상태, 해상도, Frame age, 마지막 오류는 Debug > Overview의 RTSP Channel Diagnostics에서 확인합니다.\n"
            "• Recent Events는 요약이므로 전체 검색·필터·CSV 저장은 Events 메뉴를 사용합니다."),
        content);
    troubleshooting->setObjectName(QStringLiteral("dashboardHelpTroubleshooting"));
    troubleshooting->setWordWrap(true);
    troubleshooting->setStyleSheet(QStringLiteral(
        "background:#fff3e0;color:#5d4037;border:1px solid #ffcc80;"
        "border-radius:7px;padding:11px;"));
    layout->addWidget(troubleshooting);
    layout->addStretch();

    scrollArea->setWidget(content);
    dialogLayout->addWidget(scrollArea, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(QStringLiteral("dashboardHelpButtons"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialogLayout->addWidget(buttons);
    dialog->open();
}

QWidget *DashboardPage::createVideoChannel(int channelIndex, const QString &channel,
                                           const QString &title, const QString &lowRtspUrl,
                                           const QString &highRtspUrl)
{
    auto *frame = new QFrame(this);
    frame->setMinimumSize(260, 150);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral("QFrame { background: #15191d; border: 1px solid #3a4148; border-radius: 4px; }"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *videoView = new QQuickWidget(frame);
    videoView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    videoView->setClearColor(Qt::black);
    videoView->rootContext()->setContextProperty(QStringLiteral("channelName"), channel);
    videoView->rootContext()->setContextProperty(QStringLiteral("channelTitle"), title);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspLowSourceUrl"), lowRtspUrl);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspHighSourceUrl"), highRtspUrl);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspSourceLabel"), lowRtspUrl.isEmpty() ? QStringLiteral("RTSP URL not set") : QStringLiteral("RTSP"));
    videoView->rootContext()->setContextProperty(QStringLiteral("channelExpanded"), false);
    videoView->rootContext()->setContextProperty(QStringLiteral("channelStreamEnabled"), false);
    videoView->setSource(QUrl(QStringLiteral("qrc:/qml/RtspChannel.qml")));
    if (QQuickItem *rootObject = videoView->rootObject()) {
        rootObject->setProperty("channelIndex", channelIndex);
        connect(rootObject, SIGNAL(clicked()), this, SLOT(handleVideoChannelClicked()));
        m_rtspVideoItems.append(rootObject->findChild<RtspVideoItem *>());
    } else {
        m_rtspVideoItems.append(nullptr);
    }
    layout->addWidget(videoView, 1);
    m_videoChannelWidgets.append(frame);
    m_videoQuickWidgets.append(videoView);
    return frame;
}

QImage DashboardPage::currentRtspFrame(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= m_rtspVideoItems.size()) {
        return {};
    }
    RtspVideoItem *videoItem = m_rtspVideoItems.at(channelIndex);
    return videoItem ? videoItem->currentFrame() : QImage();
}

void DashboardPage::startDelayedVideoStreams()
{
    QTimer::singleShot(300, this, [this]() {
        for (QQuickWidget *view : m_videoQuickWidgets) {
            if (view) {
                if (QQuickItem *root = view->rootObject()) root->setProperty("streamEnabled", true);
            }
        }
    });
}

void DashboardPage::handleVideoChannelClicked()
{
    if (QObject *source = sender()) toggleVideoChannel(source->property("channelIndex").toInt());
}

void DashboardPage::toggleVideoChannel(int channelIndex)
{
    if (!m_videoGrid || channelIndex < 0 || channelIndex >= m_videoChannelWidgets.size()) return;
    m_expandedVideoChannel = m_expandedVideoChannel == channelIndex ? -1 : channelIndex;
    while (QLayoutItem *item = m_videoGrid->takeAt(0)) {
        if (item->widget()) item->widget()->setVisible(false);
        delete item;
    }
    if (m_expandedVideoChannel < 0) {
        for (int i = 0; i < m_videoChannelWidgets.size(); ++i) {
            QWidget *widget = m_videoChannelWidgets.at(i);
            widget->setVisible(true);
            if (QQuickItem *root = m_videoQuickWidgets.value(i)->rootObject()) root->setProperty("expanded", false);
            m_videoGrid->addWidget(widget, i / 2, i % 2);
        }
        return;
    }
    QWidget *expanded = m_videoChannelWidgets.at(m_expandedVideoChannel);
    expanded->setVisible(true);
    if (QQuickItem *root = m_videoQuickWidgets.value(m_expandedVideoChannel)->rootObject()) root->setProperty("expanded", true);
    m_videoGrid->addWidget(expanded, 0, 0, 2, 2);
}

void DashboardPage::setRtspUrls(const QStringList &lowRtspUrls, const QStringList &highRtspUrls)
{
    m_expandedVideoChannel = -1;
    for (int i = 0; i < m_videoQuickWidgets.size(); ++i) {
        QQuickWidget *view = m_videoQuickWidgets.at(i);
        if (QQuickItem *root = view->rootObject()) {
            // Force a full source transition even when the generated URL text
            // is unchanged. This makes a credential-only save reconnect the
            // decoder instead of leaving a failed worker untouched.
            root->setProperty("streamEnabled", false);
            root->setProperty("lowRtspUrl", lowRtspUrls.value(i));
            root->setProperty("highRtspUrl", highRtspUrls.value(i));
            root->setProperty("sourceLabel", lowRtspUrls.value(i).isEmpty() ? QStringLiteral("RTSP URL not set") : QStringLiteral("RTSP"));
            root->setProperty("expanded", false);
            root->setProperty("streamEnabled", true);
        }
    }
    toggleVideoChannel(-1);
    while (QLayoutItem *item = m_videoGrid->takeAt(0)) delete item;
    for (int i = 0; i < m_videoChannelWidgets.size(); ++i) {
        m_videoChannelWidgets.at(i)->setVisible(true);
        m_videoGrid->addWidget(m_videoChannelWidgets.at(i), i / 2, i % 2);
    }
}

void DashboardPage::publishRtspDiagnostics()
{
    QList<RtspChannelDiagnostic> channels;
    for (int i = 0; i < m_videoQuickWidgets.size(); ++i) {
        QQuickWidget *view = m_videoQuickWidgets.at(i);
        QQuickItem *root = view ? view->rootObject() : nullptr;
        RtspChannelDiagnostic channel;
        channel.channel = QStringLiteral("CH%1").arg(i + 1);
        if (root) {
            channel.configured = root->property("diagnosticConfigured").toBool();
            channel.status = root->property("diagnosticStatus").toString();
            channel.error = root->property("diagnosticError").toString();
            channel.resolution = root->property("diagnosticVideoSize").toSize();
            channel.startupDelayMs = root->property("diagnosticStartupDelayMs").toInt();
            channel.lastFrameWallClockMs =
                root->property("diagnosticFrameWallClockMs").toLongLong();
        }
        if (channel.status.isEmpty()) channel.status = QStringLiteral("WAITING");
        channels.append(channel);
    }
    emit rtspDiagnosticsChanged(channels);
}

void DashboardPage::setSummary(int total, int occupied, int vacant, int sensorErrors)
{
    m_totalSlotsLabel->setText(QString::number(total));
    m_occupiedSlotsLabel->setText(QString::number(occupied));
    m_vacantSlotsLabel->setText(QString::number(vacant));
    m_sensorErrorLabel->setText(QString::number(sensorErrors));
}

void DashboardPage::setFireChannels(const QSet<QString> &channels)
{
    m_fireChannels = channels;
    m_fireAlarms.clear();
    for (const QString &channel : channels) {
        ChannelFireAlarmState alarm;
        alarm.active = true;
        m_fireAlarms.insert(channel, alarm);
    }
    setFireAlarmStates(m_fireAlarms);
}

void DashboardPage::setFireAlarmStates(
    const QHash<QString, ChannelFireAlarmState> &alarms)
{
    m_fireAlarms = alarms;
    m_fireChannels.clear();
    for (int i = 0; i < m_videoQuickWidgets.size(); ++i) {
        QQuickWidget *view = m_videoQuickWidgets.at(i);
        if (QQuickItem *root = view ? view->rootObject() : nullptr) {
            const QString channelId = QStringLiteral("CH%1").arg(i + 1);
            const ChannelFireAlarmState alarm = alarms.value(channelId);
            const bool active = alarm.active;
            if (active) m_fireChannels.insert(channelId);
            root->setProperty("fireAlarmActive", active);
        }
    }
}

void DashboardPage::prependEvent(const MonitoringEvent &event)
{
    m_recentEventTable->insertRow(0);
    const QStringList values = {
        monitoringEventTimeText(event), event.sourceId, event.eventType,
        event.message, monitoringEventStatusText(event)};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setData(Qt::UserRole, event.id);
        item->setData(Qt::UserRole + 1, event.evidenceSlotId);
        m_recentEventTable->setItem(0, column, item);
    }
    while (m_recentEventTable->rowCount() > 5) m_recentEventTable->removeRow(5);
}
