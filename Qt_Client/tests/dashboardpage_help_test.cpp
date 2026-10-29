#include "RtspVideoItem.h"
#include "pages/dashboardpage.h"

#include <QApplication>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtQml/qqml.h>

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QApplication app(argc, argv);
    qmlRegisterType<RtspVideoItem>("Rtsp", 1, 0, "RtspVideoItem");

    DashboardPage page(QStringList(4), QStringList(4));
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
            QStringLiteral("Debug > Overview"))) {
        return 2;
    }

    dialog->close();
    QApplication::processEvents();
    return 0;
}
