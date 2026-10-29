#include "RtspVideoItem.h"
#include "pages/dashboardpage.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtQml/qqml.h>

#include <cmath>

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QApplication app(argc, argv);
    qmlRegisterType<RtspVideoItem>("Rtsp", 1, 0, "RtspVideoItem");

    DashboardPage page(QStringList(4), QStringList(4));
    const QList<QQuickWidget *> videoViews = page.findChildren<QQuickWidget *>();
    if (videoViews.size() != 4 || !videoViews.first()->rootObject()
        || std::abs(videoViews.first()->rootObject()
                        ->property("fontScale").toDouble() - 1.0) > 0.01) {
        return 4;
    }
    app.setProperty("uiFontScalePercent", 200);
    QEvent fontChange(QEvent::FontChange);
    QApplication::sendEvent(&page, &fontChange);
    QApplication::processEvents();
    if (std::abs(videoViews.first()->rootObject()
                     ->property("fontScale").toDouble() - 2.0) > 0.01) {
        return 5;
    }
    QFrame *channel1 = page.findChild<QFrame *>(
        QStringLiteral("dashboardVideoChannelCH1"));
    QFrame *channel3 = page.findChild<QFrame *>(
        QStringLiteral("dashboardVideoChannelCH3"));
    if (!channel1 || !channel3
        || !page.showExpandedChannel(QStringLiteral("CH3"))
        || !channel1->isHidden() || channel3->isHidden()
        || page.showExpandedChannel(QStringLiteral("CH5"))) return 3;
    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("dashboardHelpButton"));
    if (!helpButton || helpButton->icon().isNull()
        || helpButton->text() != QStringLiteral("Dashboard 안내")) return 1;

    helpButton->click();
    QApplication::processEvents();
    QDialog *dialog = page.findChild<QDialog *>(
        QStringLiteral("dashboardHelpDialog"));
    QWidget *videoGrid = dialog
        ? dialog->findChild<QWidget *>(QStringLiteral("dashboardHelpVideoGrid"))
        : nullptr;
    QWidget *summaryCards = dialog
        ? dialog->findChild<QWidget *>(QStringLiteral("dashboardHelpSummaryCards"))
        : nullptr;
    QWidget *eventFlow = dialog
        ? dialog->findChild<QWidget *>(QStringLiteral("dashboardHelpEventFlow"))
        : nullptr;
    QLabel *troubleshooting = dialog
        ? dialog->findChild<QLabel *>(
              QStringLiteral("dashboardHelpTroubleshooting"))
        : nullptr;
    if (!dialog || !videoGrid || !summaryCards || !eventFlow
        || !troubleshooting
        || videoGrid->findChildren<QFrame *>().size() < 4
        || summaryCards->findChildren<QFrame *>().size() < 4
        || !troubleshooting->text().contains(
            QStringLiteral("System > Diagnostics"))) {
        return 2;
    }

    dialog->close();
    QApplication::processEvents();
    return 0;
}
