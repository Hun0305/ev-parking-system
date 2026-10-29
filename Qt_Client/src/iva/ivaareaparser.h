#pragma once

#include "ivaareamodels.h"

class IvaAreaParser
{
public:
    static bool parse(const QJsonDocument &document,
                      IvaAreaConfiguration &configuration,
                      QString &errorMessage);
};
