#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

struct IvaAreaDefinition
{
    int containerIndex = -1;
    int definitionIndex = -1;
    int channel = -1;
    bool channelEnabled = false;
    int areaIndex = -1;
    QString name;
    QString videoSourceToken;
    QStringList detectionModes;
    QStringList objectTypeFilter;
    QList<QPointF> areaCoordinates;
    int appearanceDuration = -1;
    int intrusionDuration = -1;
    int loiteringDuration = -1;
    QJsonObject rawContainer;
    QJsonObject rawDefinition;
};

struct IvaChannelDefinition
{
    int containerIndex = -1;
    int channel = -1;
    bool enabled = false;
    QJsonObject rawContainer;
};

struct IvaAreaConfiguration
{
    QJsonDocument rawDocument;
    QList<IvaChannelDefinition> channels;
    QList<IvaAreaDefinition> areas;
    QStringList warnings;
};

struct IvaIntegerRange
{
    int minimum = -1;
    int maximum = -1;

    bool isValid() const
    {
        return minimum >= 0 && maximum >= minimum;
    }

    bool contains(int value) const
    {
        return isValid() && value >= minimum && value <= maximum;
    }
};

struct IvaChannelOptions
{
    int channel = -1;
    IvaIntegerRange areaIndex;
    IvaIntegerRange areaCoordinateCount;
    IvaIntegerRange appearanceDuration;
    IvaIntegerRange intrusionDuration;
    IvaIntegerRange loiteringDuration;
    QStringList detectionModes;
    QStringList objectTypeFilters;
    QJsonObject rawOptions;
};

struct IvaAreaOptions
{
    QJsonDocument rawDocument;
    QList<IvaChannelOptions> channels;

    const IvaChannelOptions *forChannel(int channel) const
    {
        for (const IvaChannelOptions &options : channels) {
            if (options.channel == channel) {
                return &options;
            }
        }
        return nullptr;
    }
};

struct IvaChannelCapability
{
    int channel = -1;
    bool ivaAreaSupported = false;
    QSize maxResolution;
    QJsonObject rawCapability;
};

struct WiseAiCapabilities
{
    QJsonDocument rawDocument;
    QList<IvaChannelCapability> channels;

    const IvaChannelCapability *forChannel(int channel) const
    {
        for (const IvaChannelCapability &capability : channels) {
            if (capability.channel == channel) {
                return &capability;
            }
        }
        return nullptr;
    }
};
