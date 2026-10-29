#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include "diagnostics/diagnostictypes.h"
#include "models/monitoringevent.h"
#include "models/parkingstate.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QGridLayout;
class QEvent;
class QLabel;
class QQuickWidget;
class QTableWidget;
class QTimer;
class RtspVideoItem;

class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardPage(const QStringList &lowRtspUrls,
                           const QStringList &highRtspUrls,
                           QWidget *parent = nullptr);

    void setSummary(int total, int occupied, int vacant, int sensorErrors);
    void setFireChannels(const QSet<QString> &channels);
    void setFireAlarmStates(const QHash<QString, ChannelFireAlarmState> &alarms);
    void prependEvent(const MonitoringEvent &event);
    void setRtspUrls(const QStringList &lowRtspUrls, const QStringList &highRtspUrls);
    QImage currentRtspFrame(int channelIndex) const;
    bool showExpandedChannel(const QString &channel);

signals:
    void rtspDiagnosticsChanged(const QList<RtspChannelDiagnostic> &channels);
    void eventEvidenceRequested(const QString &eventId);
    void recentEventsDetailRequested();

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void handleVideoChannelClicked();

private:
    void showHelpDialog();
    QWidget *createVideoChannel(int channelIndex, const QString &channel,
                                const QString &lowRtspUrl, const QString &highRtspUrl);
    void startDelayedVideoStreams();
    void toggleVideoChannel(int channelIndex);
    void applyVideoChannelLayout();
    void publishRtspDiagnostics();

    QGridLayout *m_videoGrid = nullptr;
    QList<QWidget *> m_videoChannelWidgets;
    QList<QQuickWidget *> m_videoQuickWidgets;
    QList<RtspVideoItem *> m_rtspVideoItems;
    QLabel *m_totalSlotsLabel = nullptr;
    QLabel *m_occupiedSlotsLabel = nullptr;
    QLabel *m_vacantSlotsLabel = nullptr;
    QLabel *m_sensorErrorLabel = nullptr;
    QTableWidget *m_recentEventTable = nullptr;
    QTimer *m_diagnosticTimer = nullptr;
    QSet<QString> m_fireChannels;
    QHash<QString, ChannelFireAlarmState> m_fireAlarms;
    int m_expandedVideoChannel = -1;
};

#endif
