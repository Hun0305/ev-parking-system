#include "pages/debugpage.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    DebugPage page;

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("debugHelpButton"));
    if (!helpButton || helpButton->icon().isNull()) return 1;

    helpButton->click();
    QApplication::processEvents();
    QDialog *dialog = page.findChild<QDialog *>(
        QStringLiteral("debugHelpDialog"));
    QLabel *steps = dialog
        ? dialog->findChild<QLabel *>(QStringLiteral("debugHelpSteps"))
        : nullptr;
    QLabel *note = dialog
        ? dialog->findChild<QLabel *>(QStringLiteral("debugHelpNote"))
        : nullptr;
    if (!dialog || !steps || !note
        || !steps->text().contains(QStringLiteral("Test Tools"))
        || !note->text().contains(QStringLiteral("simulation sandbox"))) {
        return 2;
    }

    dialog->close();
    QApplication::processEvents();
    return 0;
}
