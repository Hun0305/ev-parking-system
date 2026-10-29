#include "iva/wiseaiconfigclient.h"
#include "iva/ivaareaupdatebuilder.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTimer>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

int runLiveTest(const QString &configPath)
{
    QSettings settings(configPath, QSettings::IniFormat);
    WiseAiConnectionOptions options;
    options.baseUrl = QUrl(QStringLiteral("https://%1")
                               .arg(qEnvironmentVariable("HANWHA_CAMERA_IP")));
    options.username = settings.value(QStringLiteral("camera/username")).toString();
    options.password = settings.value(QStringLiteral("camera/password")).toString();
    options.pinnedCertificateSha256 = qEnvironmentVariable(
        "HANWHA_HTTPS_CERT_SHA256");

    WiseAiConfigClient client(options);
    QEventLoop loop;
    int result = 1;
    IvaAreaOptions liveOptions;
    WiseAiCapabilities liveCapabilities;
    QObject::connect(&client, &WiseAiConfigClient::optionsReceived,
                     &loop, [&](const IvaAreaOptions &receivedOptions) {
        liveOptions = receivedOptions;
    });
    QObject::connect(&client, &WiseAiConfigClient::capabilitiesReceived,
                     &loop, [&](const WiseAiCapabilities &capabilities) {
        liveCapabilities = capabilities;
    });
    QObject::connect(&client, &WiseAiConfigClient::configurationReceived,
                     &loop, [&](const IvaAreaConfiguration &configuration) {
        for (const IvaChannelDefinition &channel : configuration.channels) {
            const IvaChannelOptions *channelOptions = liveOptions.forChannel(
                channel.channel);
            if (!channelOptions) {
                std::cerr << "FAIL: live camera options missing channel "
                          << channel.channel << '\n';
                loop.quit();
                return;
            }
            const IvaChannelCapability *capability = liveCapabilities.forChannel(
                channel.channel);
            if (!capability || !capability->ivaAreaSupported
                || !capability->maxResolution.isValid()) {
                std::cerr << "FAIL: live camera capability missing IVA coordinate space for channel "
                          << channel.channel << '\n';
                loop.quit();
                return;
            }
            QList<IvaAreaDefinition> areas;
            for (const IvaAreaDefinition &area : configuration.areas) {
                if (area.channel == channel.channel) areas.append(area);
            }
            QJsonObject payload;
            QString validationError;
            if (!IvaAreaUpdateBuilder::buildChannelPayload(
                    channel.channel, channel.enabled, areas, *channelOptions,
                    payload, validationError)) {
                std::cerr << "FAIL: live channel cannot form a safe PUT payload: "
                          << validationError.toStdString() << '\n';
                loop.quit();
                return;
            }
        }
        std::cout << "PASS: live WiseAI HTTPS Digest response parsed areas="
                  << configuration.areas.size()
                  << " warnings=" << configuration.warnings.size()
                  << " and every channel passed capability/PUT validation\n";
        result = 0;
        loop.quit();
    });
    QObject::connect(&client, &WiseAiConfigClient::requestFailed,
                     &loop, [&](const QString &message) {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, [&]() {
        std::cerr << "FAIL: live WiseAI request timed out\n";
        client.cancel();
        loop.quit();
    });
    client.fetchConfiguration();
    loop.exec();
    return result;
}

int runLiveWriteRoundTrip(const QString &configPath)
{
    if (qEnvironmentVariable("HANWHA_LIVE_WRITE_TEST") != QStringLiteral("YES")) {
        std::cerr << "FAIL: set HANWHA_LIVE_WRITE_TEST=YES for the explicit live write test\n";
        return 1;
    }

    QSettings settings(configPath, QSettings::IniFormat);
    WiseAiConnectionOptions connection;
    connection.baseUrl = QUrl(QStringLiteral("https://%1")
                                  .arg(qEnvironmentVariable("HANWHA_CAMERA_IP")));
    connection.username = settings.value(QStringLiteral("camera/username")).toString();
    connection.password = settings.value(QStringLiteral("camera/password")).toString();
    connection.pinnedCertificateSha256 = qEnvironmentVariable(
        "HANWHA_HTTPS_CERT_SHA256");
    connection.timeoutMs = 20000;

    enum class Phase { Loading, ApplyingTemporaryValue, RestoringOriginal };
    WiseAiConfigClient client(connection);
    QEventLoop loop;
    Phase phase = Phase::Loading;
    int result = 1;
    int channel = -1;
    int ruleIndex = -1;
    int originalDuration = -1;
    int temporaryDuration = -1;
    bool channelEnabled = false;
    QList<IvaAreaDefinition> originalAreas;
    QList<IvaAreaDefinition> temporaryAreas;
    IvaAreaOptions liveOptions;

    QObject::connect(&client, &WiseAiConfigClient::optionsReceived,
                     &loop, [&](const IvaAreaOptions &receivedOptions) {
        liveOptions = receivedOptions;
    });
    QObject::connect(&client, &WiseAiConfigClient::configurationReceived,
                     &loop, [&](const IvaAreaConfiguration &configuration) {
        if (phase != Phase::Loading) return;
        for (const IvaAreaDefinition &candidate : configuration.areas) {
            const IvaChannelOptions *channelOptions = liveOptions.forChannel(
                candidate.channel);
            if (!candidate.detectionModes.isEmpty() || !channelOptions
                || !channelOptions->appearanceDuration.contains(
                    candidate.appearanceDuration)) {
                continue;
            }
            const int alternative = candidate.appearanceDuration
                    < channelOptions->appearanceDuration.maximum
                ? candidate.appearanceDuration + 1
                : candidate.appearanceDuration - 1;
            if (!channelOptions->appearanceDuration.contains(alternative)) continue;
            channel = candidate.channel;
            ruleIndex = candidate.areaIndex;
            originalDuration = candidate.appearanceDuration;
            temporaryDuration = alternative;
            break;
        }
        if (channel < 0) {
            std::cerr << "FAIL: no inactive rule is available for a safe round-trip test\n";
            loop.quit();
            return;
        }
        for (const IvaChannelDefinition &candidate : configuration.channels) {
            if (candidate.channel == channel) {
                channelEnabled = candidate.enabled;
                break;
            }
        }
        for (const IvaAreaDefinition &area : configuration.areas) {
            if (area.channel != channel) continue;
            originalAreas.append(area);
            IvaAreaDefinition edited = area;
            if (edited.areaIndex == ruleIndex) {
                edited.appearanceDuration = temporaryDuration;
            }
            temporaryAreas.append(edited);
        }
        std::cout << "TEST: inactive CH" << channel + 1
                  << " rule " << ruleIndex << " appearanceDuration "
                  << originalDuration << " -> " << temporaryDuration << '\n';
        phase = Phase::ApplyingTemporaryValue;
        QTimer::singleShot(0, &client, [&]() {
            client.applyChannelConfiguration(channel, channelEnabled,
                                             temporaryAreas);
        });
    });
    QObject::connect(&client, &WiseAiConfigClient::applySucceeded,
                     &loop,
                     [&](int appliedChannel,
                         const IvaAreaConfiguration &verifiedConfiguration) {
        Q_UNUSED(verifiedConfiguration)
        if (appliedChannel != channel) {
            std::cerr << "FAIL: unexpected channel completed the live write test\n";
            loop.quit();
            return;
        }
        if (phase == Phase::ApplyingTemporaryValue) {
            std::cout << "PASS: temporary value PUT and camera GET verification succeeded\n";
            phase = Phase::RestoringOriginal;
            QTimer::singleShot(0, &client, [&]() {
                client.applyChannelConfiguration(channel, channelEnabled,
                                                 originalAreas);
            });
            return;
        }
        std::cout << "PASS: original value restored and verified from camera ("
                  << temporaryDuration << " -> " << originalDuration << ")\n";
        result = 0;
        loop.quit();
    });
    QObject::connect(&client, &WiseAiConfigClient::applyFailed,
                     &loop,
                     [&](int failedChannel, const QString &message,
                         bool rollbackSucceeded) {
        std::cerr << "FAIL: live write phase="
                  << (phase == Phase::ApplyingTemporaryValue ? "temporary" : "restore")
                  << " channel=" << failedChannel
                  << " rollbackVerified=" << (rollbackSucceeded ? "true" : "false")
                  << " message=" << message.toStdString() << '\n';
        loop.quit();
    });
    QObject::connect(&client, &WiseAiConfigClient::requestFailed,
                     &loop, [&](const QString &message) {
        std::cerr << "FAIL: live setup request failed: "
                  << message.toStdString() << '\n';
        loop.quit();
    });
    QTimer::singleShot(120000, &loop, [&]() {
        std::cerr << "FAIL: live write round-trip timed out; camera state must be refreshed\n";
        client.cancel();
        loop.quit();
    });
    client.fetchConfiguration();
    loop.exec();
    return result;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() == 3 && arguments.at(1) == QStringLiteral("--live")) {
        return runLiveTest(arguments.at(2));
    }
    if (arguments.size() == 3
        && arguments.at(1) == QStringLiteral("--live-write-roundtrip")) {
        return runLiveWriteRoundTrip(arguments.at(2));
    }

    const QByteArray expected = QByteArray::fromHex(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    if (!require(WiseAiConfigClient::normalizeCertificateSha256(
                     QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                                    "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF"))
                     == expected,
                 "colon-separated SHA-256 pin must normalize")) return 1;
    if (!require(WiseAiConfigClient::formatCertificateSha256(expected)
                     == QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                                       "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF"),
                 "certificate digest must use a readable uppercase format")) return 1;
    if (!require(WiseAiConfigClient::normalizeCertificateSha256(
                     QStringLiteral("not-a-fingerprint")).isEmpty(),
                 "invalid certificate pin must be rejected")) return 1;

    WiseAiConnectionOptions insecureOptions;
    insecureOptions.baseUrl = QUrl(QStringLiteral("http://192.0.2.10"));
    insecureOptions.username = QStringLiteral("test");
    WiseAiConfigClient insecureClient(insecureOptions);
    int failures = 0;
    QString error;
    QObject::connect(&insecureClient, &WiseAiConfigClient::requestFailed,
                     [&](const QString &message) {
        ++failures;
        error = message;
    });
    insecureClient.fetchConfiguration();
    if (!require(failures == 1 && error.contains(QStringLiteral("HTTPS")),
                 "plain HTTP camera URLs must fail before a network request")) return 1;

    std::cout << "PASS: WiseAI client enforces HTTPS and explicit certificate pinning\n";
    return 0;
}
