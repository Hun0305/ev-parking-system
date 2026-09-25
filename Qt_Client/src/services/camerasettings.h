#ifndef CAMERASETTINGS_H
#define CAMERASETTINGS_H

#include <QString>
#include <QStringList>

class CameraSettings
{
public:
    explicit CameraSettings(QString configPath);

    const QString &configPath() const { return m_configPath; }
    QString cameraIp() const;
    QString cameraUsername() const;
    QString cameraPassword() const;
    QString httpsCertificateSha256() const;
    QStringList rtspUrls(const QString &profileOverride = QString()) const;
    bool saveCameraCredentials(const QString &cameraIpText,
                               const QString &usernameText,
                               const QString &passwordText,
                               QString &newIp,
                               QString &errorMessage);
    bool saveCameraIp(const QString &cameraIpText, QString &newIp, QString &errorMessage);
    bool saveHttpsCertificateSha256(const QString &sha256, QString &errorMessage) const;

private:
    QString m_configPath;
    bool m_runtimeCredentialsConfigured = false;
    QString m_runtimeCameraIp;
    QString m_runtimeCameraUsername;
    QString m_runtimeCameraPassword;
};

#endif
