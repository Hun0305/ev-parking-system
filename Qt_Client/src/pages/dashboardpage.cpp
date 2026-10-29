#include "dashboardpage.h"

#include "RtspVideoItem.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
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
#include <QSizePolicy>
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

    pageLayout->addLayout(createPageHeader(
        this, QStringLiteral("Dashboard"),
        QStringLiteral("Live monitoring, summary, and recent events")));
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
    auto *topLayout = new QHBoxLayout;
    topLayout->setSpacing(10);
    auto *videoGroup = new QGroupBox(QStringLiteral("4-Channel RTSP Monitor"), this);
    m_videoGrid = new QGridLayout(videoGroup);
    m_videoGrid->setSpacing(8);
    m_videoGrid->addWidget(createVideoChannel(0, QStringLiteral("CH1"), lowRtspUrls.value(0), highRtspUrls.value(0)), 0, 0);
    m_videoGrid->addWidget(createVideoChannel(1, QStringLiteral("CH2"), lowRtspUrls.value(1), highRtspUrls.value(1)), 0, 1);
    m_videoGrid->addWidget(createVideoChannel(2, QStringLiteral("CH3"), lowRtspUrls.value(2), highRtspUrls.value(2)), 1, 0);
    m_videoGrid->addWidget(createVideoChannel(3, QStringLiteral("CH4"), lowRtspUrls.value(3), highRtspUrls.value(3)), 1, 1);
    topLayout->addWidget(videoGroup, 3);

    auto *infoPanel = new QWidget(this);
    infoPanel->setFixedWidth(280);
    auto *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(8);

    auto *summaryGroup = new QGroupBox(QStringLiteral("Current Summary"), infoPanel);
    summaryGroup->setObjectName(QStringLiteral("dashboardCurrentSummary"));
    summaryGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *summaryGrid = new QGridLayout(summaryGroup);
    summaryGrid->setContentsMargins(8, 10, 8, 8);
    summaryGrid->setSpacing(8);

    const auto addSummaryCard = [summaryGroup, summaryGrid](int row, int column,
                                                              const QString &title,
                                                              const QString &objectName,
                                                              const QString &color,
                                                              QLabel **valueLabel) {
        auto *card = new QFrame(summaryGroup);
        card->setObjectName(objectName + QStringLiteral("Card"));
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:%1; border:1px solid #cfd8dc; border-radius:6px; }")
                                .arg(color));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 7, 8, 7);
        cardLayout->setSpacing(2);

        auto *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName(objectName + QStringLiteral("Title"));
        titleLabel->setStyleSheet(QStringLiteral(
            "border:none; color:#546e7a; font-size:11px; font-weight:700;"));
        titleLabel->setWordWrap(true);
        *valueLabel = new QLabel(QStringLiteral("0"), card);
        (*valueLabel)->setObjectName(objectName);
        (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*valueLabel)->setStyleSheet(QStringLiteral(
            "border:none; color:#17212b; font-size:22px; font-weight:900;"));
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(*valueLabel);
        summaryGrid->addWidget(card, row, column);
    };

    addSummaryCard(0, 0, QStringLiteral("Total slots"),
                   QStringLiteral("totalSlotsLabel"), QStringLiteral("#e3f2fd"),
                   &m_totalSlotsLabel);
    addSummaryCard(0, 1, QStringLiteral("Occupied"),
                   QStringLiteral("occupiedSlotsLabel"), QStringLiteral("#fff3e0"),
                   &m_occupiedSlotsLabel);
    addSummaryCard(1, 0, QStringLiteral("Vacant"),
                   QStringLiteral("vacantSlotsLabel"), QStringLiteral("#e8f5e9"),
                   &m_vacantSlotsLabel);
    addSummaryCard(1, 1, QStringLiteral("Hall errors"),
                   QStringLiteral("sensorErrorLabel"), QStringLiteral("#ffebee"),
                   &m_sensorErrorLabel);
    infoLayout->addWidget(summaryGroup);

    auto *recentGroup = new QGroupBox(QStringLiteral("Recent Events"), infoPanel);
    recentGroup->setObjectName(QStringLiteral("dashboardRecentEvents"));
    auto *recentLayout = new QVBoxLayout(recentGroup);
    recentLayout->setContentsMargins(8, 10, 8, 8);
    recentLayout->setSpacing(6);
    auto *recentHeaderLayout = new QHBoxLayout;
    auto *recentHint = new QLabel(QStringLiteral("Latest events"), recentGroup);
    recentHint->setStyleSheet(QStringLiteral("color:#607d8b;"));
    recentHeaderLayout->addWidget(recentHint);
    recentHeaderLayout->addStretch();
    auto *recentDetailButton = new QPushButton(QStringLiteral("Detail"), recentGroup);
    recentDetailButton->setObjectName(QStringLiteral("recentEventsDetailButton"));
    recentDetailButton->setToolTip(QStringLiteral("Open the complete Events page"));
    recentDetailButton->setFixedHeight(30);
    recentHeaderLayout->addWidget(recentDetailButton);
    recentLayout->addLayout(recentHeaderLayout);

    m_recentEventTable = new QTableWidget(0, 3, recentGroup);
    m_recentEventTable->setObjectName(QStringLiteral("recentEventTable"));
    m_recentEventTable->setHorizontalHeaderLabels({QStringLiteral("Time"),
                                                    QStringLiteral("Event"),
                                                    QStringLiteral("Message")});
    auto *recentHeader = m_recentEventTable->horizontalHeader();
    recentHeader->setSectionResizeMode(0, QHeaderView::Fixed);
    recentHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    recentHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    recentHeader->resizeSection(0, 92);
    recentHeader->resizeSection(1, 124);
    m_recentEventTable->verticalHeader()->setVisible(false);
    m_recentEventTable->setMinimumHeight(160);
    m_recentEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recentEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    recentLayout->addWidget(m_recentEventTable);
    infoLayout->addWidget(recentGroup, 1);
    topLayout->addWidget(infoPanel, 1);
    pageLayout->addLayout(topLayout, 1);
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
    connect(recentDetailButton, &QPushButton::clicked,
            this, &DashboardPage::recentEventsDetailRequested);
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
        QStringLiteral("각 채널은 동일한 4K 스트림을 표시합니다. 채널 화면을 클릭하면 해당 화면만 전체 보기로 확대되며, 다시 클릭하면 4분할로 복귀합니다. 클릭으로 스트림을 재연결하거나 화질을 전환하지 않습니다."),
        monitorGroup);
    monitorHint->setWordWrap(true);
    monitorLayout->addWidget(monitorHint);

    auto *videoGridWidget = new QWidget(monitorGroup);
    videoGridWidget->setObjectName(QStringLiteral("dashboardHelpVideoGrid"));
    auto *videoGrid = new QGridLayout(videoGridWidget);
    videoGrid->setContentsMargins(0, 4, 0, 0);
    videoGrid->setSpacing(8);
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
            QStringLiteral("CH%1").arg(channel + 1), preview);
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
        QStringLiteral("Time · Event · Message\n최근 5건만 표시"),
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
            "• 연결 상태, 해상도, Frame age, 마지막 오류는 System > Diagnostics의 RTSP Channel Diagnostics에서 확인합니다.\n"
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
                                           const QString &lowRtspUrl,
                                           const QString &highRtspUrl)
{
    auto *frame = new QFrame(this);
    frame->setObjectName(QStringLiteral("dashboardVideoChannel%1").arg(channel));
    frame->setMinimumSize(260, 150);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral("QFrame { background: #15191d; border: 1px solid #3a4148; border-radius: 4px; }"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *videoView = new QQuickWidget(frame);
    videoView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    videoView->setClearColor(Qt::black);
    videoView->rootContext()->setContextProperty(QStringLiteral("channelName"), channel);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspLowSourceUrl"), lowRtspUrl);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspHighSourceUrl"), highRtspUrl);
    videoView->rootContext()->setContextProperty(QStringLiteral("rtspSourceLabel"), lowRtspUrl.isEmpty() ? QStringLiteral("RTSP URL not set") : QStringLiteral("RTSP"));
    videoView->rootContext()->setContextProperty(QStringLiteral("channelExpanded"), false);
    videoView->rootContext()->setContextProperty(QStringLiteral("channelStreamEnabled"), false);
    videoView->rootContext()->setContextProperty(
        QStringLiteral("uiFontScale"),
        qApp->property("uiFontScalePercent").isValid()
            ? qApp->property("uiFontScalePercent").toInt() / 100.0
            : 1.0);
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

void DashboardPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ApplicationFontChange
        && event->type() != QEvent::FontChange) return;
    const QVariant scaleProperty = qApp->property("uiFontScalePercent");
    const qreal scale = scaleProperty.isValid()
        ? scaleProperty.toInt() / 100.0
        : 1.0;
    for (QQuickWidget *view : m_videoQuickWidgets) {
        if (view) {
            view->rootContext()->setContextProperty(
                QStringLiteral("uiFontScale"), scale);
        }
    }
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
    applyVideoChannelLayout();
}

bool DashboardPage::showExpandedChannel(const QString &channel)
{
    QString normalized = channel.trimmed().toUpper();
    if (!normalized.startsWith(QStringLiteral("CH"))) return false;
    normalized.remove(0, 2);
    normalized.remove(QLatin1Char(' '));
    bool validNumber = false;
    const int channelIndex = normalized.toInt(&validNumber) - 1;
    if (!validNumber || channelIndex < 0
        || channelIndex >= m_videoChannelWidgets.size()) {
        return false;
    }
    m_expandedVideoChannel = channelIndex;
    applyVideoChannelLayout();
    return true;
}

void DashboardPage::applyVideoChannelLayout()
{
    if (!m_videoGrid) return;
    while (QLayoutItem *item = m_videoGrid->takeAt(0)) {
        if (item->widget()) item->widget()->setVisible(false);
        delete item;
    }
    if (m_expandedVideoChannel < 0) {
        for (int i = 0; i < m_videoChannelWidgets.size(); ++i) {
            QWidget *widget = m_videoChannelWidgets.at(i);
            widget->setVisible(true);
            if (QQuickItem *root = m_videoQuickWidgets.value(i)->rootObject()) {
                root->setProperty("expanded", false);
            }
            m_videoGrid->addWidget(widget, i / 2, i % 2);
        }
        return;
    }
    QWidget *expanded = m_videoChannelWidgets.at(m_expandedVideoChannel);
    for (int i = 0; i < m_videoQuickWidgets.size(); ++i) {
        if (QQuickItem *root = m_videoQuickWidgets.value(i)->rootObject()) {
            root->setProperty("expanded", false);
        }
    }
    expanded->setVisible(true);
    if (QQuickItem *root = m_videoQuickWidgets.value(m_expandedVideoChannel)->rootObject()) {
        root->setProperty("expanded", true);
    }
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
    applyVideoChannelLayout();
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
        monitoringEventTimeText(event), event.eventType, event.message};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setData(Qt::UserRole, event.id);
        item->setData(Qt::UserRole + 1, event.evidenceSlotId);
        item->setToolTip(values.at(column));
        m_recentEventTable->setItem(0, column, item);
    }
    while (m_recentEventTable->rowCount() > 5) m_recentEventTable->removeRow(5);
}
