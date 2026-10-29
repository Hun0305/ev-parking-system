#include "widgets/uifontscalecontrol.h"
#include "services/uifontscale.h"

#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolButton>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir uiConfigDirectory(
        QDir::current().filePath(QStringLiteral("font-control-test-XXXXXX")));
    if (!uiConfigDirectory.isValid()) return 8;
    UiFontScale::initialize(
        app, uiConfigDirectory.filePath(QStringLiteral("client_config.local.ini")));
    QWidget header;
    header.setObjectName(QStringLiteral("compactHeaderTestSurface"));
    header.setFixedSize(678, 34);
    header.setStyleSheet(QStringLiteral(
        "QWidget#compactHeaderTestSurface { background:#f5f7f9; }"));
    auto *headerLayout = new QHBoxLayout(&header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);
    auto addPlaceholder = [&](int width, const QString &color) {
        auto *placeholder = new QWidget(&header);
        placeholder->setFixedSize(width, 34);
        placeholder->setStyleSheet(
            QStringLiteral("background:%1;border-radius:6px;").arg(color));
        headerLayout->addWidget(placeholder);
        return placeholder;
    };
    QWidget *monitor = addPlaceholder(42, QStringLiteral("#cfd8dc"));
    QWidget *status = addPlaceholder(260, QStringLiteral("#eceff1"));
    headerLayout->addStretch();
    UiFontScaleControl control(&header);
    headerLayout->addWidget(&control);
    QWidget *help = addPlaceholder(112, QStringLiteral("#263238"));
    QWidget *notifications = addPlaceholder(62, QStringLiteral("#eceff1"));
    auto *decrease = control.findChild<QToolButton *>(
        QStringLiteral("decreaseUiFontScaleButton"));
    auto *increase = control.findChild<QToolButton *>(
        QStringLiteral("increaseUiFontScaleButton"));
    auto *value = control.findChild<QLabel *>(
        QStringLiteral("uiFontScaleValueLabel"));
    if (control.percent() != 100
        || control.size() != QSize(126, 34)
        || !decrease || !increase || !value
        || value->text() != QStringLiteral("100%")
        || !decrease->isEnabled() || !increase->isEnabled()) {
        return 1;
    }

    int requestedPercent = -1;
    QObject::connect(&control, &UiFontScaleControl::percentChangeRequested,
                     &app, [&](int percent) { requestedPercent = percent; });
    increase->click();
    if (requestedPercent != 105 || control.percent() != 105
        || value->text() != QStringLiteral("105%")) return 2;
    control.setPercent(200);
    if (control.percent() != 200 || increase->isEnabled()
        || value->text() != QStringLiteral("200%")) return 3;
    increase->click();
    if (control.percent() != 200) return 4;
    control.setPercent(90);
    if (control.percent() != 90 || decrease->isEnabled()
        || !increase->isEnabled()) return 5;
    control.setPercent(87);
    if (control.percent() != 90) return 6;
    control.setPercent(240);
    if (control.percent() != 200) return 7;

    QString error;
    if (!UiFontScale::setPercent(200, &error)) return 9;
    control.setPercent(UiFontScale::currentPercent());
    header.show();
    QApplication::processEvents();
    if (QFontMetrics(value->font()).horizontalAdvance(value->text()) > value->width()
        || QFontMetrics(value->font()).height() > value->height()
        || QFontMetrics(decrease->font()).horizontalAdvance(decrease->text()) > decrease->width()
        || QFontMetrics(decrease->font()).height() > decrease->height()
        || QFontMetrics(increase->font()).horizontalAdvance(increase->text()) > increase->width()
        || QFontMetrics(increase->font()).height() > increase->height()) return 10;
    const QList<QWidget *> orderedWidgets = {
        monitor, status, &control, help, notifications,
    };
    for (int index = 1; index < orderedWidgets.size(); ++index) {
        if (orderedWidgets.at(index - 1)->geometry().right()
            >= orderedWidgets.at(index)->geometry().left()) return 12;
    }

    const QString captureDir = qEnvironmentVariable("SYSTEM_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        if (!control.grab().save(
                QDir(captureDir).filePath(QStringLiteral("global-font-control-200.png")))) {
            return 11;
        }
        if (!header.grab().save(
                QDir(captureDir).filePath(QStringLiteral("global-header-200.png")))) {
            return 13;
        }
    }
    return 0;
}
