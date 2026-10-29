#include "iva/ivaareacapabilityparser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

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
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("capabilities"),
         QJsonArray{
             QJsonObject{{QStringLiteral("channel"), 0},
                         {QStringLiteral("ivaarea"), true},
                         {QStringLiteral("maxResolution"),
                          QJsonObject{{QStringLiteral("width"), 2592},
                                      {QStringLiteral("height"), 1520}}},
                         {QStringLiteral("vendorFlag"), true}},
             QJsonObject{{QStringLiteral("channel"), 1},
                         {QStringLiteral("ivaarea"), false},
                         {QStringLiteral("maxResolution"),
                          QJsonObject{{QStringLiteral("width"), 1920},
                                      {QStringLiteral("height"), 1080}}}}}}});

    WiseAiCapabilities capabilities;
    QString error;
    if (!require(IvaAreaCapabilityParser::parse(document, capabilities, error),
                 "actual WiseAI capability structure must parse")) return 1;
    const IvaChannelCapability *channel0 = capabilities.forChannel(0);
    const IvaChannelCapability *channel1 = capabilities.forChannel(1);
    if (!require(channel0 && channel0->ivaAreaSupported
                     && channel0->maxResolution == QSize(2592, 1520),
                 "channel 0 IVA coordinate resolution must normalize")) return 1;
    if (!require(channel1 && !channel1->ivaAreaSupported
                     && channel1->maxResolution == QSize(1920, 1080),
                 "unsupported channels must remain visible")) return 1;
    if (!require(channel0->rawCapability.value(QStringLiteral("vendorFlag")).toBool(),
                 "unknown capability fields must be preserved")) return 1;

    WiseAiCapabilities unchanged = capabilities;
    const QJsonDocument invalid(QJsonObject{
        {QStringLiteral("capabilities"),
         QJsonArray{QJsonObject{{QStringLiteral("channel"), 0},
                                {QStringLiteral("ivaarea"), true}}}}});
    if (!require(!IvaAreaCapabilityParser::parse(invalid, unchanged, error),
                 "missing maxResolution must fail")) return 1;
    if (!require(unchanged.channels.size() == 2,
                 "failed parsing must retain last-good capabilities")) return 1;

    std::cout << "PASS: WiseAI channel capability and IVA coordinate space are validated\n";
    return 0;
}
