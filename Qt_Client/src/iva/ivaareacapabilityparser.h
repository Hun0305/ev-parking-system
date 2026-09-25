#pragma once

#include "ivaareamodels.h"

class IvaAreaCapabilityParser
{
public:
    static bool parse(const QJsonDocument &document,
                      WiseAiCapabilities &capabilities,
                      QString &errorMessage);
};
