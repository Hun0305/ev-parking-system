#ifndef DEBUGPAGE_H
#define DEBUGPAGE_H

#include "diagnostics/diagnostictypes.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;

class DebugPage : public QWidget
{
    Q_OBJECT

public:
    explicit DebugPage(QWidget *parent = nullptr);

    void setLastMessage(const QString &message);
    void setApiDiagnostic(const ApiDiagnosticState &state);
    void setRtspDiagnostics(const QList<RtspChannelDiagnostic> &channels);
    void setParkingDiagnostic(const ParkingDiagnosticState &state);
    void appendDiagnosticLog(const DiagnosticLogRecord &record);

signals:
    void reconnectApiRequested();
    void clearAlarmsRequested();
    void toggleMockEvRequested();
    void nonEvAlertRequested();
    void overtimeAlertRequested();
    void sensorErrorRequested();
    void randomizeParkingRequested();
    void sampleMessagesRequested();
    void manualMessageRequested(const QString &message);

private:
    QWidget *createOverviewTab();
    QWidget *createLogsTab();
    QWidget *createTestToolsTab();
    void renderApiDiagnostic();
    void renderRtspDiagnostics();
    void renderParkingDiagnostic();
    void refreshLogFilter();

    ApiDiagnosticState m_apiState;
    QList<RtspChannelDiagnostic> m_rtspChannels;
    ParkingDiagnosticState m_parkingState;
    QList<DiagnosticLogRecord> m_logs;

    QLabel *m_runtimeValueLabel = nullptr;
    QLabel *m_runtimeDetailLabel = nullptr;
    QLabel *m_apiCardValueLabel = nullptr;
    QLabel *m_apiCardDetailLabel = nullptr;
    QLabel *m_streamCardValueLabel = nullptr;
    QLabel *m_streamCardDetailLabel = nullptr;
    QLabel *m_parkingCardValueLabel = nullptr;
    QLabel *m_parkingCardDetailLabel = nullptr;
    QLabel *m_apiEndpointLabel = nullptr;
    QLabel *m_apiStatusLabel = nullptr;
    QLabel *m_apiLatencyLabel = nullptr;
    QLabel *m_apiLastSuccessLabel = nullptr;
    QLabel *m_apiRetryLabel = nullptr;
    QLabel *m_apiErrorLabel = nullptr;
    QLabel *m_parkingDetailLabel = nullptr;
    QTableWidget *m_streamTable = nullptr;
    QTableWidget *m_logTable = nullptr;
    QComboBox *m_levelFilter = nullptr;
    QComboBox *m_moduleFilter = nullptr;
    QLineEdit *m_logSearch = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;
    QLineEdit *m_messageInput = nullptr;
    QLabel *m_lastMessageLabel = nullptr;
    QTimer *m_ageRefreshTimer = nullptr;
};

#endif
