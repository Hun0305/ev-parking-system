#pragma once

#include "ivaareamodels.h"

class IvaAreaOptionsParser
{
public:
    static bool parse(const QJsonDocument &document,
                      IvaAreaOptions &options,
                      QString &errorMessage);
};
