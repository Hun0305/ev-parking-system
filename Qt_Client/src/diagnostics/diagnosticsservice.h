#pragma once

#include "diagnostictypes.h"
#include "models/monitoringevent.h"

#include <QObject>

class DiagnosticsService : public QObject
{
    Q_OBJECT

public:
    explicit DiagnosticsService(QObject *parent = nullptr);

    const ApiDiagnosticState &apiState() const { return m_apiState; }
    const QList<RtspChannelDiagnostic> &rtspChannels() const { return m_rtspChannels; }
    const ParkingDiagnosticState &parkingState() const { return m_parkingState; }
    const QList<DiagnosticLogRecord> &logs() const { return m_logs; }

    static QString sanitizedUrl(const QString &urlText);

public slots:
    void setApiState(const ApiDiagnosticState &state);
    void setRtspChannels(const QList<RtspChannelDiagnostic> &channels);
    void setParkingSummary(int slotCount, int activeAlarmCount);
    void markMockStateLoaded(int slotCount);
    void markSimulationApplied(const QString &scenario);
    void ingestDomainEvent(const MonitoringEvent &event);

signals:
    void apiStateChanged(const ApiDiagnosticState &state);
    void rtspChannelsChanged(const QList<RtspChannelDiagnostic> &channels);
    void parkingStateChanged(const ParkingDiagnosticState &state);
    void logAdded(const DiagnosticLogRecord &record);

private:
    void updateDataSource();
    void appendLog(const QString &level, const QString &module,
                   const QString &code, const QString &message);

    ApiDiagnosticState m_apiState;
    QList<RtspChannelDiagnostic> m_rtspChannels;
    ParkingDiagnosticState m_parkingState;
    QList<DiagnosticLogRecord> m_logs;
    QDateTime m_lastServerStateAt;
    QDateTime m_lastSimulationStateAt;
    bool m_mockStateLoaded = false;
    bool m_serverStateReceived = false;
    bool m_simulationAfterServer = false;
};
