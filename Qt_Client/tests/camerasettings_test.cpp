#include "services/camerasettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDir;
    if (!require(temporaryDir.isValid(), "temporary config directory must be created")) return 1;

    const QString configPath = temporaryDir.filePath(QStringLiteral("camera_config.ini"));
    {
        QSettings initial(configPath, QSettings::IniFormat);
        initial.setValue(QStringLiteral("camera/camera_ip"), QStringLiteral("192.168.1.20"));
        initial.setValue(QStringLiteral("camera/username"), QStringLiteral("local-user"));
        initial.setValue(QStringLiteral("camera/password"), QStringLiteral("local-password"));
        initial.setValue(QStringLiteral("camera/rtsp_port"), 554);
        initial.setValue(QStringLiteral("camera/channel_ch1"), 0);
        initial.setValue(QStringLiteral("camera/high_profile"), QStringLiteral("profile2"));
        initial.setValue(QStringLiteral("camera/low_profile"), QStringLiteral("profile3"));
        initial.setValue(QStringLiteral("camera/https_certificate_sha256"),
                         QStringLiteral("AA:BB"));
        initial.sync();
    }

    CameraSettings settings(configPath);
    QString savedIp;
    QString error;
    if (!require(settings.saveCameraCredentials(
                     QStringLiteral("10.40.2.77"), QStringLiteral("saved-user"),
                     QStringLiteral("saved-password"), savedIp, error),
                 "camera IP and credentials must be saved together")) return 1;
    if (!require(savedIp == QStringLiteral("10.40.2.77"),
                 "saved address must not preserve an old subnet prefix")) return 1;
    if (!require(settings.cameraIp() == QStringLiteral("10.40.2.77"),
                 "the complete IPv4 address must be persisted")) return 1;
    if (!require(settings.cameraUsername() == QStringLiteral("saved-user")
                     && settings.cameraPassword() == QStringLiteral("saved-password"),
                 "saved camera credentials must be reloaded from the local config")) return 1;
    const QStringList savedRtspUrls = settings.rtspUrls(QStringLiteral("profile2"));
    if (!require(savedRtspUrls.value(0)
                     == QStringLiteral("rtsp://saved-user:saved-password@10.40.2.77:554/0/profile2/media.smp"),
                 "RTSP URL generation must use the newly saved camera credentials")) return 1;
    if (!require(settings.httpsCertificateSha256() == QStringLiteral("AA:BB"),
                 "camera certificate pin must be read from the local-only config")) return 1;

    error.clear();
    if (!require(!settings.saveCameraCredentials(
                     QStringLiteral("10.40.2.77"), QString(),
                     QStringLiteral("saved-password"), savedIp, error),
                 "an empty camera username must be rejected")) return 1;
    error.clear();
    if (!require(!settings.saveCameraCredentials(
                     QStringLiteral("10.40.2.77"), QStringLiteral("saved-user"),
                     QString(), savedIp, error),
                 "an empty camera password must be rejected")) return 1;
    error.clear();
    if (!require(!settings.saveCameraIp(QStringLiteral("172.20.35"), savedIp, error),
                 "an incomplete address must be rejected")) return 1;
    if (!require(settings.cameraIp() == QStringLiteral("10.40.2.77"),
                 "invalid input must not overwrite the saved address")) return 1;

    error.clear();
    if (!require(!settings.saveCameraIp(QStringLiteral("999.1.1.1"), savedIp, error),
                 "an out-of-range IPv4 address must be rejected")) return 1;

    std::cout << "PASS: camera IP uses a complete validated IPv4 address without a fixed subnet\n";
    return 0;
}
