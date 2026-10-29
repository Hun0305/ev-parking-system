#include "widgets/pagehelp.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget page;
    auto *button = createPageHelpButton(
        &page, &page,
        {QStringLiteral("sample"), QStringLiteral("Sample"),
         QStringLiteral("Sample 사용 안내"),
         QStringLiteral("공통 도움말 동작을 확인합니다."),
         QStringLiteral("<b>1. 확인</b><br>공통 도움말 본문"),
         QStringLiteral("※ 공통 주의사항")});

    if (!button || button->objectName() != QStringLiteral("sampleHelpButton")
        || button->icon().isNull()
        || button->accessibleName() != QStringLiteral("Sample 도움말")) {
        return 1;
    }

    button->click();
    QApplication::processEvents();
    QDialog *dialog = page.findChild<QDialog *>(
        QStringLiteral("sampleHelpDialog"));
    if (!dialog || !dialog->isModal()) return 2;

    QLabel *steps = dialog->findChild<QLabel *>(
        QStringLiteral("sampleHelpSteps"));
    QLabel *note = dialog->findChild<QLabel *>(
        QStringLiteral("sampleHelpNote"));
    if (!steps || !steps->text().contains(QStringLiteral("공통 도움말 본문"))
        || !note || !note->text().contains(QStringLiteral("공통 주의사항"))) {
        return 3;
    }

    button->click();
    QApplication::processEvents();
    if (page.findChildren<QDialog *>(QStringLiteral("sampleHelpDialog")).size()
        != 1) {
        return 4;
    }

    dialog->close();
    QApplication::processEvents();
    return 0;
}
