#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QShowEvent;
class QSpinBox;
class QTabWidget;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(const QString &configPath, const QString &cameraIp,
                          QWidget *parent = nullptr,
                          const QString &cameraUsername = QString(),
                          const QString &cameraPassword = QString());
    void setCameraIp(const QString &cameraIp);
    void setCameraCredentials(const QString &username, const QString &password);
    void setServerBaseUrl(const QString &baseUrl);
    void setServerConnectionStatus(const QString &status, bool connected);
    void setOverstayThresholdRequestStarted(const QString &status);
    void setOverstayThreshold(int seconds, const QString &applyPolicy,
                              bool afterUpdate);
    void setOverstayThresholdError(const QString &message, bool updateRequest);
    static int overstaySeconds(int hours, int minutes, int seconds);
    QTabWidget *systemTabs() const;

signals:
    void saveCameraCredentialsRequested(const QString &cameraIp,
                                        const QString &username,
                                        const QString &password);
    void saveServerBaseUrlRequested(const QString &baseUrl);
    void reconnectServerRequested();
    void overstayThresholdRefreshRequested();
    void overstayThresholdUpdateRequested(int seconds);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateOverstayButtons();
    static QString formatOverstayDuration(int seconds);

    QLabel *m_cameraIpLabel = nullptr;
    QLineEdit *m_cameraIpInput = nullptr;
    QLineEdit *m_cameraUsernameInput = nullptr;
    QLineEdit *m_cameraPasswordInput = nullptr;
    QRadioButton *m_serverHttpRadio = nullptr;
    QRadioButton *m_serverHttpsRadio = nullptr;
    QLineEdit *m_serverHostInput = nullptr;
    QSpinBox *m_serverPortInput = nullptr;
    QLabel *m_serverConnectionLabel = nullptr;
    QSpinBox *m_overstayHoursInput = nullptr;
    QSpinBox *m_overstayMinutesInput = nullptr;
    QSpinBox *m_overstaySecondsInput = nullptr;
    QLabel *m_currentOverstayLabel = nullptr;
    QLabel *m_applyPolicyLabel = nullptr;
    QLabel *m_overstayStatusLabel = nullptr;
    QPushButton *m_applyOverstayButton = nullptr;
    QPushButton *m_refreshOverstayButton = nullptr;
    QTabWidget *m_systemTabs = nullptr;
    int m_serverOverstaySeconds = -1;
    bool m_serverConnected = false;
    bool m_overstayRequestInFlight = false;
};

#endif
