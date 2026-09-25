#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>

class QLabel;
class QLineEdit;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(const QString &configPath, const QString &cameraIp,
                          QWidget *parent = nullptr);
    void setCameraIp(const QString &cameraIp);
    void setServerBaseUrl(const QString &baseUrl);
    void setServerConnectionStatus(const QString &status, bool connected);

signals:
    void saveCameraIpRequested(const QString &cameraIp);
    void saveServerBaseUrlRequested(const QString &baseUrl);
    void reconnectServerRequested();

private:
    QLabel *m_cameraIpLabel = nullptr;
    QLineEdit *m_cameraIpInput = nullptr;
    QLineEdit *m_serverBaseUrlInput = nullptr;
    QLabel *m_serverConnectionLabel = nullptr;
};

#endif
