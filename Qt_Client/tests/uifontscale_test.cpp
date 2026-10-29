#include "services/uifontscale.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("uifontscale-test-XXXXXX")));
    if (!directory.isValid()) return 1;
    const QString configPath = directory.filePath(QStringLiteral("client_config.local.ini"));
    {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/font_scale_percent"), 110);
        settings.sync();
        if (settings.status() != QSettings::NoError) return 9;
    }

    const qreal basePointSize = app.font().pointSizeF();
    if (UiFontScale::loadPercent(configPath) != 110) return 10;
    UiFontScale::initialize(app, configPath);
    if (UiFontScale::currentPercent() != 110) return 2;
    if (UiFontScale::loadPercent(configPath) != 110) return 8;
    if (basePointSize > 0.0
        && std::abs(app.font().pointSizeF() - basePointSize * 1.1) > 0.05) {
        return 3;
    }

    QLabel label(QStringLiteral("Application font preview"));
    label.setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
    label.show();
    QApplication::processEvents();
    if (!label.styleSheet().contains(QStringLiteral("font-size:22px"))) return 4;

    QString error;
    if (!UiFontScale::setPercent(105, &error) || !error.isEmpty()) return 11;
    QApplication::processEvents();
    if (UiFontScale::currentPercent() != 105
        || !label.styleSheet().contains(QStringLiteral("font-size:21px"))
        || UiFontScale::loadPercent(configPath) != 105) return 12;
    if (!UiFontScale::setPercent(90, &error) || !error.isEmpty()) return 5;
    QApplication::processEvents();
    if (UiFontScale::currentPercent() != 90
        || !label.styleSheet().contains(QStringLiteral("font-size:18px"))
        || UiFontScale::loadPercent(configPath) != 90) return 6;

    label.setStyleSheet(QStringLiteral("font-size:10px;color:#263238;"));
    QApplication::processEvents();
    if (!label.styleSheet().contains(QStringLiteral("font-size:9px"))) return 7;
    if (UiFontScale::normalizePercent(20) != 90
        || UiFontScale::normalizePercent(103) != 105
        || UiFontScale::normalizePercent(500) != 200) return 13;
    return 0;
}
