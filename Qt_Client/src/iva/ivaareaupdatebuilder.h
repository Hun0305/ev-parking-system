#pragma once

#include "ivaareamodels.h"

class IvaAreaUpdateBuilder
{
public:
    static bool buildChannelPayload(int channel,
                                    bool enabled,
                                    const QList<IvaAreaDefinition> &areas,
                                    const IvaChannelOptions &options,
                                    QJsonObject &payload,
                                    QString &errorMessage,
                                    const QSize &coordinateResolution = QSize());

    static bool payloadMatchesChannel(const QJsonObject &expectedPayload,
                                      const IvaAreaConfiguration &configuration,
                                      const IvaChannelOptions &options,
                                      QString &errorMessage,
                                      const QSize &coordinateResolution = QSize());
};
