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
    QStringList rtspUrls(const QString &profileOverride = QString()) const;
    bool saveCameraIp(const QString &cameraIpText, QString &newIp, QString &errorMessage) const;

private:
    QString m_configPath;
};

#endif
