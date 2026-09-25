#include "dashboardpage.h"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
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
        QStringLiteral("Double-click a parking event to view evidence."), recentGroup);
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
                const QTableWidgetItem *sourceItem = m_recentEventTable->item(row, 1);
                if (sourceItem && !sourceItem->text().trimmed().isEmpty()) {
                    emit evidenceRequested(sourceItem->text());
                }
            });
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
    }
    layout->addWidget(videoView, 1);
    m_videoChannelWidgets.append(frame);
    m_videoQuickWidgets.append(videoView);
    return frame;
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
        m_recentEventTable->setItem(0, column, new QTableWidgetItem(values.at(column)));
    }
    while (m_recentEventTable->rowCount() > 5) m_recentEventTable->removeRow(5);
}
