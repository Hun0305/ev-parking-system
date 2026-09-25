#include "diagnosticsservice.h"

#include <algorithm>
#include <QUrl>

namespace {
constexpr int kMaxDiagnosticLogs = 1000;

QString normalizedLevel(const QString &status, const QString &eventType)
{
    const QString normalizedStatus = status.trimmed().toUpper();
    const QString normalizedType = eventType.trimmed().toUpper();
    if (normalizedStatus == QStringLiteral("FAILED")
        || normalizedStatus == QStringLiteral("ERROR")
        || normalizedType.contains(QStringLiteral("ERROR"))
        || normalizedType.contains(QStringLiteral("FAILED"))) {
        return QStringLiteral("ERROR");
    }
    if (normalizedStatus == QStringLiteral("OPEN")
        || normalizedType.contains(QStringLiteral("ALERT"))
        || normalizedType.contains(QStringLiteral("WARNING"))) {
        return QStringLiteral("WARN");
    }
    return QStringLiteral("INFO");
}
}

DiagnosticsService::DiagnosticsService(QObject *parent)
    : QObject(parent)
{
}

QString DiagnosticsService::sanitizedUrl(const QString &urlText)
{
    QUrl url(urlText.trimmed());
    if (!url.isValid()) return QStringLiteral("Not configured");
    url.setUserName(QString());
    url.setPassword(QString());
    url.setQuery(QString());
    url.setFragment(QString());
    const QString safe = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    return safe.isEmpty() ? QStringLiteral("Not configured") : safe;
}

void DiagnosticsService::setApiState(const ApiDiagnosticState &state)
{
    const QString previousStatus = m_apiState.status;
    const QString previousError = m_apiState.lastError;
    m_apiState = state;
    m_apiState.endpoint = sanitizedUrl(state.endpoint);

    if (m_apiState.connected && m_apiState.lastSuccessAt.isValid()) {
        m_lastServerStateAt = m_apiState.lastSuccessAt;
        m_serverStateReceived = true;
        m_simulationAfterServer = false;
        m_parkingState.lastServerSyncAt = m_apiState.lastSuccessAt;
        m_parkingState.lastServerSlotCount = m_apiState.appliedSlotCount;
        updateDataSource();
    }

    if (previousStatus != m_apiState.status || previousError != m_apiState.lastError) {
        const QString level = m_apiState.connected
            ? QStringLiteral("INFO")
            : (m_apiState.status == QStringLiteral("CONNECTING")
                   ? QStringLiteral("INFO") : QStringLiteral("ERROR"));
        QString message = m_apiState.status;
        if (!m_apiState.lastError.isEmpty()) message += QStringLiteral(" | ") + m_apiState.lastError;
        appendLog(level, QStringLiteral("API"), QStringLiteral("API_STATE_CHANGED"), message);
    }

    emit apiStateChanged(m_apiState);
}

void DiagnosticsService::setRtspChannels(const QList<RtspChannelDiagnostic> &channels)
{
    for (const RtspChannelDiagnostic &channel : channels) {
        const auto previous = std::find_if(
            m_rtspChannels.cbegin(), m_rtspChannels.cend(),
            [&channel](const RtspChannelDiagnostic &candidate) {
                return candidate.channel == channel.channel;
            });
        if (previous == m_rtspChannels.cend()
            || previous->status != channel.status
            || previous->error != channel.error) {
            const QString level = channel.status.compare(QStringLiteral("Playing"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("INFO") : QStringLiteral("WARN");
            QString message = channel.status;
            if (!channel.error.isEmpty()) message += QStringLiteral(" | ") + channel.error;
            appendLog(level, QStringLiteral("RTSP"),
                      QStringLiteral("%1_STATE_CHANGED").arg(channel.channel), message);
        }
    }
    m_rtspChannels = channels;
    emit rtspChannelsChanged(m_rtspChannels);
}

void DiagnosticsService::setParkingSummary(int slotCount, int activeAlarmCount)
{
    if (m_parkingState.slotCount == slotCount
        && m_parkingState.activeAlarmCount == activeAlarmCount) return;
    m_parkingState.slotCount = slotCount;
    m_parkingState.activeAlarmCount = activeAlarmCount;
    emit parkingStateChanged(m_parkingState);
}

void DiagnosticsService::markMockStateLoaded(int slotCount)
{
    m_mockStateLoaded = true;
    m_lastSimulationStateAt = QDateTime::currentDateTime();
    m_parkingState.slotCount = slotCount;
    m_parkingState.lastSimulationScenario = QStringLiteral("Initial mock state");
    updateDataSource();
    appendLog(QStringLiteral("INFO"), QStringLiteral("SIMULATION"),
              QStringLiteral("MOCK_STATE_LOADED"),
              QStringLiteral("Initial mock state loaded: %1 slots").arg(slotCount));
}

void DiagnosticsService::markSimulationApplied(const QString &scenario)
{
    m_mockStateLoaded = true;
    m_lastSimulationStateAt = QDateTime::currentDateTime();
    if (m_serverStateReceived) m_simulationAfterServer = true;
    m_parkingState.lastSimulationScenario = scenario;
    updateDataSource();
    appendLog(QStringLiteral("INFO"), QStringLiteral("SIMULATION"),
              QStringLiteral("SCENARIO_APPLIED"), scenario);
}

void DiagnosticsService::ingestDomainEvent(const MonitoringEvent &event)
{
    const QString status = monitoringEventStatusText(event);
    appendLog(normalizedLevel(status, event.eventType), QStringLiteral("EVENT"),
              event.eventType,
              QStringLiteral("%1 | %2 | %3")
                  .arg(event.sourceId, status, event.message));
}

void DiagnosticsService::updateDataSource()
{
    QString source = QStringLiteral("UNKNOWN");
    if (m_serverStateReceived) {
        source = m_simulationAfterServer ? QStringLiteral("MIXED")
                                         : QStringLiteral("SERVER");
    } else if (m_mockStateLoaded) {
        source = QStringLiteral("MOCK");
    }
    if (m_parkingState.dataSource == source) return;
    m_parkingState.dataSource = source;
    emit parkingStateChanged(m_parkingState);
}

void DiagnosticsService::appendLog(const QString &level, const QString &module,
                                   const QString &code, const QString &message)
{
    const DiagnosticLogRecord record{
        QDateTime::currentDateTime(), level, module, code, message};
    m_logs.append(record);
    while (m_logs.size() > kMaxDiagnosticLogs) m_logs.removeFirst();
    emit logAdded(record);
}
