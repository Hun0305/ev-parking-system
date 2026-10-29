#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "services/camerasettings.h"

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QString>

class DashboardPage;
struct AuthSession;
class DebugPage;
class DiagnosticsService;
class EvidencePage;
class EventsPage;
class FireAlarmPopup;
class IvaSettingsPage;
class QCloseEvent;
class QFrame;
class QLabel;
class NotificationCenter;
class ParkingController;
class ParkingMapPage;
class ParkingSimulationService;
class QPushButton;
class SettingsPage;
class WiseAiConfigClient;
class QStackedWidget;
class QToolButton;
class UiFontScaleControl;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const AuthSession &authSession,
                        QWidget *parent = nullptr);
    bool prepareForReauthentication();

signals:
    void reauthenticationRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void installPageHelpButtons();
    void connectPages();
    void renderParkingState();
    void updateMonitorStatus(const QString &status, bool connected);
    void saveCameraCredentials(const QString &cameraIpText,
                               const QString &username,
                               const QString &password);
    void updateNotificationIndicator();
    void showNotificationPopup();
    void showFireAlarmPopup(const QString &channelId, const QString &alarmId);
    void closeFireAlarmPopup(const QString &channelId, const QString &alarmId);
    void showEventsPage();
    bool showEventEvidencePage(const QString &eventId);
    void handlePageChanged(int index);
    void finishPendingIvaNavigation();
    QString cameraConfigPath() const;
    QString clientConfigPath() const;
    QString clientLocalConfigPath() const;
    QString parkingMapLayoutPath() const;

    CameraSettings m_cameraSettings;
    NotificationCenter *m_notificationCenter = nullptr;
    QToolButton *m_monitorStatusButton = nullptr;
    QLabel *m_alertBanner = nullptr;
    QToolButton *m_notificationButton = nullptr;
    UiFontScaleControl *m_uiFontScaleControl = nullptr;
    QLabel *m_notificationBadge = nullptr;
    QFrame *m_notificationPopup = nullptr;
    QHash<QString, QPointer<FireAlarmPopup>> m_fireAlarmPopups;
    QHash<QString, QString> m_shownFireAlarmIds;
    QStackedWidget *m_pages = nullptr;
    QStackedWidget *m_pageHelpStack = nullptr;
    QPushButton *m_eventsNavButton = nullptr;
    QPushButton *m_dashboardNavButton = nullptr;
    QPushButton *m_evidenceNavButton = nullptr;
    QPushButton *m_ivaNavButton = nullptr;
    DashboardPage *m_dashboardPage = nullptr;
    ParkingMapPage *m_parkingMapPage = nullptr;
    EventsPage *m_eventsPage = nullptr;
    EvidencePage *m_evidencePage = nullptr;
    IvaSettingsPage *m_ivaSettingsPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    DebugPage *m_debugPage = nullptr;
    DiagnosticsService *m_diagnosticsService = nullptr;
    ParkingController *m_parkingController = nullptr;
    ParkingSimulationService *m_parkingSimulationService = nullptr;
    WiseAiConfigClient *m_wiseAiConfigClient = nullptr;
    bool m_pageTransitionGuard = false;
    int m_lastPageIndex = 0;
    int m_pendingPageIndex = -1;
    bool m_closeAfterIvaSave = false;
};

#endif
