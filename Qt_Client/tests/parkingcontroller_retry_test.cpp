#include "controllers/parkingcontroller.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    const QString sharedPath = directory.filePath(QStringLiteral("client_config.ini"));
    QFile config(sharedPath);
    if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream stream(&config);
    stream << "[api]\n"
           << "enabled=true\n"
           << "base_url=http://127.0.0.1:1\n"
           << "slots_path=/api/v1/parking-slots\n"
           << "slot_detail_path=/api/v1/parking-slots/{slot_id}\n"
           << "timeout_ms=200\n"
           << "reconnect_interval_ms=1000\n"
           << "max_reconnect_interval_ms=1000\n"
           << "allow_insecure_http=true\n"
           << "[mqtt]\n"
           << "enabled=true\n"
           << "follow_api_host=false\n"
           << "host=172.20.35.123\n"
           << "port=1883\n"
           << "client_id=qt-client\n"
           << "reconnect_interval_ms=1000\n";
    config.close();

    const QString localPath =
        directory.filePath(QStringLiteral("client_config.local.ini"));
    ParkingController controller(
        sharedPath, localPath);
    int connectingCount = 0;
    bool retryScheduled = false;
    bool serverAddressUpdating = false;
    bool mqttFollowedNewApiHost = false;
    bool staleMqttHostRetried = false;
    bool synchronizedSettingsPersisted = false;
    bool mqttClientIdGenerated = false;
    bool mqttClientIdStable = false;
    QObject::connect(&controller, &ParkingController::serverConnectionChanged,
                     &app, [&](const QString &status, bool) {
        if (status == QStringLiteral("Connecting...")) ++connectingCount;
        if (status.contains(QStringLiteral("retry in"))) retryScheduled = true;
        if (retryScheduled && connectingCount >= 2
            && mqttFollowedNewApiHost && !staleMqttHostRetried
            && synchronizedSettingsPersisted
            && mqttClientIdGenerated && mqttClientIdStable) {
            app.exit(0);
        }
    });
    QObject::connect(&controller, &ParkingController::statusMessageChanged,
                     &app, [&](const QString &status) {
        if (!serverAddressUpdating) return;
        if (status == QStringLiteral("MQTT connecting: 172.20.32.123:1883")) {
            mqttFollowedNewApiHost = true;
        }
        if (status.contains(QStringLiteral("172.20.35.123"))) {
            staleMqttHostRetried = true;
        }
    });
    QTimer::singleShot(4000, &app, [&]() {
        app.exit(retryScheduled && connectingCount >= 2
                         && mqttFollowedNewApiHost && !staleMqttHostRetried
                         && synchronizedSettingsPersisted
                         && mqttClientIdGenerated && mqttClientIdStable
                     ? 0
                     : 1);
    });
    controller.start();
    QSettings generatedSettings(localPath, QSettings::IniFormat);
    generatedSettings.sync();
    const QString generatedClientId =
        generatedSettings.value(QStringLiteral("mqtt/client_id"))
            .toString().trimmed();
    mqttClientIdGenerated = generatedClientId.size() <= 23
        && QRegularExpression(QStringLiteral("^qt-client-[0-9a-f]{12}$"))
               .match(generatedClientId).hasMatch();

    serverAddressUpdating = true;
    controller.updateServerBaseUrl(QStringLiteral("http://172.20.32.123:1"));

    QSettings localSettings(localPath, QSettings::IniFormat);
    synchronizedSettingsPersisted =
        localSettings.value(QStringLiteral("api/base_url")).toString()
            == QStringLiteral("http://172.20.32.123:1")
        && localSettings.value(QStringLiteral("mqtt/host")).toString()
            == QStringLiteral("172.20.32.123")
        && localSettings.value(QStringLiteral("mqtt/follow_api_host")).toBool();
    mqttClientIdStable =
        localSettings.value(QStringLiteral("mqtt/client_id")).toString()
        == generatedClientId;
    return app.exec();
}
