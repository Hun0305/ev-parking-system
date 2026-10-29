#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "services/camerasettings.h"

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QString>

class DashboardPage;
class DebugPage;
class DiagnosticsService;
class EvidencePage;
class EventsPage;
class FireAlarmPopup;
class ImageComparePage;
class IvaSettingsPage;
class QCloseEvent;
class QFrame;
class QLabel;
class NotificationCenter;
class ParkingController;
class ParkingMapPage;
class ParkingRoiSettingsPage;
class ParkingSimulationService;
class QPushButton;
class SettingsPage;
class WiseAiConfigClient;
class QStackedWidget;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void connectPages();
    void renderParkingState();
    void saveCameraCredentials(const QString &cameraIpText,
                               const QString &username,
                               const QString &password);
    void updateNotificationIndicator();
    void showNotificationPopup();
    void showFireAlarmPopup(const QString &channelId, const QString &alarmId);
    void closeFireAlarmPopup(const QString &channelId, const QString &alarmId);
    void showEventsPage();
    bool showEventEvidencePage(const QString &eventId);
    QString cameraConfigPath() const;
    QString clientConfigPath() const;
    QString clientLocalConfigPath() const;
    QString parkingMapLayoutPath() const;

    CameraSettings m_cameraSettings;
    NotificationCenter *m_notificationCenter = nullptr;
    QLabel *m_alertBanner = nullptr;
    QToolButton *m_notificationButton = nullptr;
    QLabel *m_notificationBadge = nullptr;
    QFrame *m_notificationPopup = nullptr;
    QHash<QString, QPointer<FireAlarmPopup>> m_fireAlarmPopups;
    QHash<QString, QString> m_shownFireAlarmIds;
    QStackedWidget *m_pages = nullptr;
    QPushButton *m_eventsNavButton = nullptr;
    QPushButton *m_evidenceNavButton = nullptr;
    QPushButton *m_imageCompareNavButton = nullptr;
    DashboardPage *m_dashboardPage = nullptr;
    ParkingMapPage *m_parkingMapPage = nullptr;
    ParkingRoiSettingsPage *m_parkingRoiSettingsPage = nullptr;
    EventsPage *m_eventsPage = nullptr;
    EvidencePage *m_evidencePage = nullptr;
    ImageComparePage *m_imageComparePage = nullptr;
    IvaSettingsPage *m_ivaSettingsPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    DebugPage *m_debugPage = nullptr;
    DiagnosticsService *m_diagnosticsService = nullptr;
    ParkingController *m_parkingController = nullptr;
    ParkingSimulationService *m_parkingSimulationService = nullptr;
    WiseAiConfigClient *m_wiseAiConfigClient = nullptr;
};

#endif
