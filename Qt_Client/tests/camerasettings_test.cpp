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
        initial.sync();
    }

    CameraSettings settings(configPath);
    QString savedIp;
    QString error;
    if (!require(settings.saveCameraIp(QStringLiteral("10.40.2.77"), savedIp, error),
                 "a full IPv4 address from another subnet must be accepted")) return 1;
    if (!require(savedIp == QStringLiteral("10.40.2.77"),
                 "saved address must not preserve an old subnet prefix")) return 1;
    if (!require(settings.cameraIp() == QStringLiteral("10.40.2.77"),
                 "the complete IPv4 address must be persisted")) return 1;

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
