#include "camerasettings.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSettings>

CameraSettings::CameraSettings(QString configPath)
    : m_configPath(std::move(configPath))
{
}

QString CameraSettings::cameraIp() const
{
    if (m_runtimeCredentialsConfigured) return m_runtimeCameraIp;
    const QString environmentIp = qEnvironmentVariable("HANWHA_CAMERA_IP").trimmed();
    if (!environmentIp.isEmpty()) {
        return environmentIp;
    }
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value(QStringLiteral("camera/camera_ip")).toString();
}

QString CameraSettings::cameraUsername() const
{
    if (m_runtimeCredentialsConfigured) return m_runtimeCameraUsername;
    const QString environmentUsername = qEnvironmentVariable(
        "HANWHA_CAMERA_USERNAME");
    if (!environmentUsername.isEmpty()) {
        return environmentUsername;
    }
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value(QStringLiteral("camera/username")).toString();
}

QString CameraSettings::cameraPassword() const
{
    if (m_runtimeCredentialsConfigured) return m_runtimeCameraPassword;
    const QString environmentPassword = qEnvironmentVariable(
        "HANWHA_CAMERA_PASSWORD");
    if (!environmentPassword.isEmpty()) {
        return environmentPassword;
    }
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value(QStringLiteral("camera/password")).toString();
}

QString CameraSettings::httpsCertificateSha256() const
{
    const QString environmentFingerprint = qEnvironmentVariable(
        "HANWHA_HTTPS_CERT_SHA256");
    if (!environmentFingerprint.isEmpty()) {
        return environmentFingerprint;
    }
    QSettings settings(m_configPath, QSettings::IniFormat);
    return settings.value(
        QStringLiteral("camera/https_certificate_sha256")).toString();
}

bool CameraSettings::saveCameraCredentials(const QString &cameraIpText,
                                           const QString &usernameText,
                                           const QString &passwordText,
                                           QString &newIp,
                                           QString &errorMessage)
{
    static const QRegularExpression dottedDecimal(
        QStringLiteral(R"(^(?:0|[1-9][0-9]{0,2})(?:\.(?:0|[1-9][0-9]{0,2})){3}$)"));
    const QString trimmedIp = cameraIpText.trimmed();
    QHostAddress address;
    if (!dottedDecimal.match(trimmedIp).hasMatch()
        || !address.setAddress(trimmedIp)
        || address.protocol() != QAbstractSocket::IPv4Protocol
        || address == QHostAddress::AnyIPv4
        || address == QHostAddress::Broadcast) {
        errorMessage = QStringLiteral("Enter a valid full IPv4 address, for example 192.168.10.20.");
        return false;
    }

    const QString username = usernameText.trimmed();
    if (username.isEmpty()) {
        errorMessage = QStringLiteral("Enter the camera username.");
        return false;
    }
    if (passwordText.isEmpty()) {
        errorMessage = QStringLiteral("Enter the camera password.");
        return false;
    }

    newIp = address.toString();
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("camera/camera_ip"), newIp);
    settings.setValue(QStringLiteral("camera/username"), username);
    settings.setValue(QStringLiteral("camera/password"), passwordText);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = QStringLiteral("Failed to save local camera settings.");
        return false;
    }
    m_runtimeCredentialsConfigured = true;
    m_runtimeCameraIp = newIp;
    m_runtimeCameraUsername = username;
    m_runtimeCameraPassword = passwordText;
    return true;
}

bool CameraSettings::saveCameraIp(const QString &cameraIpText, QString &newIp,
                                  QString &errorMessage)
{
    const QString username = cameraUsername();
    const QString password = cameraPassword();
    if (username.isEmpty() || password.isEmpty()) {
        errorMessage = QStringLiteral(
            "Camera username and password must be configured before saving the IP.");
        return false;
    }
    return saveCameraCredentials(cameraIpText, username, password, newIp,
                                 errorMessage);
}

bool CameraSettings::saveHttpsCertificateSha256(const QString &sha256,
                                                QString &errorMessage) const
{
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("camera/https_certificate_sha256"), sha256);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        errorMessage = QStringLiteral("Failed to save camera_config.ini.");
        return false;
    }
    return true;
}

QStringList CameraSettings::rtspUrls(const QString &profileOverride) const
{
    QStringList urls(4);
    if (!m_runtimeCredentialsConfigured) {
        const QString commonUrl = qEnvironmentVariable("RTSP_URL", qEnvironmentVariable("HANWHA_RTSP_URL"));
        const QString highUrl = qEnvironmentVariable("RTSP_HIGH_URL");
        const QString lowUrl = qEnvironmentVariable("RTSP_LOW_URL");
        const QStringList channelEnvNames = {QStringLiteral("RTSP_CH1_URL"), QStringLiteral("RTSP_CH2_URL"),
                                             QStringLiteral("RTSP_CH3_URL"), QStringLiteral("RTSP_CH4_URL")};
        for (int i = 0; i < channelEnvNames.size(); ++i) {
            QString url = qEnvironmentVariable(channelEnvNames.at(i).toUtf8().constData());
            if (url.isEmpty() && i == 0) url = highUrl;
            if (url.isEmpty() && i == 1) url = lowUrl;
            if (url.isEmpty()) url = commonUrl;
            urls[i] = url;
        }
        if (std::any_of(urls.cbegin(), urls.cend(), [](const QString &url) { return !url.isEmpty(); })) {
            return urls;
        }
    }

    const QString exampleConfig = QDir(QFileInfo(m_configPath).absolutePath())
        .absoluteFilePath(QStringLiteral("camera_config.example.ini"));
    const QString sourcePath = QFile::exists(m_configPath) ? m_configPath : exampleConfig;
    QSettings settings(sourcePath, QSettings::IniFormat);
    const QString configuredUrl = settings.value(QStringLiteral("camera/rtsp_url")).toString();
    if (!configuredUrl.isEmpty() && profileOverride.isEmpty()) {
        urls.fill(configuredUrl);
        return urls;
    }

    const QString cameraIpValue = cameraIp();
    const QString user = cameraUsername();
    const QString password = cameraPassword();
    const int port = settings.value(QStringLiteral("camera/rtsp_port"), 554).toInt();
    QString defaultProfile;
    if (profileOverride == QStringLiteral("profile2")) {
        defaultProfile = settings.value(QStringLiteral("camera/high_profile"), profileOverride).toString();
    } else if (profileOverride == QStringLiteral("profile3")) {
        defaultProfile = settings.value(QStringLiteral("camera/low_profile"), profileOverride).toString();
    } else {
        defaultProfile = settings.value(QStringLiteral("camera/low_profile"), QStringLiteral("profile3")).toString();
    }

    for (int i = 0; i < urls.size(); ++i) {
        QString channel = settings.value(QStringLiteral("camera/channel_ch%1").arg(i + 1), QString::number(i)).toString();
        QString profile = defaultProfile;
        const QString channelProfile =
            settings.value(QStringLiteral("camera/profile_ch%1").arg(i + 1)).toString();
        if (!channelProfile.isEmpty()) {
            profile = channelProfile;
        }
        const int slash = profile.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            channel = profile.left(slash);
            profile = profile.mid(slash + 1);
        }
        if (cameraIpValue.isEmpty() || channel.isEmpty() || profile.isEmpty()) {
            urls[i].clear();
            continue;
        }
        QString auth;
        if (!user.isEmpty()) {
            auth = user;
            if (!password.isEmpty()) auth += QLatin1Char(':') + password;
            auth += QLatin1Char('@');
        }
        urls[i] = QStringLiteral("rtsp://%1%2:%3/%4/%5/media.smp")
            .arg(auth, cameraIpValue).arg(port).arg(channel, profile);
    }
    return urls;
}
