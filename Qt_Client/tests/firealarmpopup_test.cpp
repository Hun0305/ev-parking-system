#include "dialogs/firealarmpopup.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QPushButton>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QPointer<FireAlarmPopup> popup = new FireAlarmPopup(
        QStringLiteral("CH1"), QStringLiteral("fire-FLAME01-17001"));
    int checkCount = 0;
    QString checkedChannel;
    QString checkedAlarm;
    QObject::connect(popup, &FireAlarmPopup::checkRequested, &app,
                     [&](const QString &channelId, const QString &alarmId) {
        ++checkCount;
        checkedChannel = channelId;
        checkedAlarm = alarmId;
    });

    popup->show();
    app.processEvents();
    if (!popup || !popup->isVisible()) return 1;

    QPushButton *checkButton = popup->findChild<QPushButton *>(
        QStringLiteral("fireAlarmCheckButton"));
    if (!checkButton || !checkButton->isEnabled()) return 2;
    checkButton->click();
    app.processEvents();
    if (checkCount != 1) return 3;
    if (checkedChannel != QStringLiteral("CH1")) return 4;
    if (checkedAlarm != QStringLiteral("fire-FLAME01-17001")) return 5;
    if (popup && popup->isVisible()) return 6;

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QPointer<FireAlarmPopup> serverClosedPopup = new FireAlarmPopup(
        QStringLiteral("CH2"), QStringLiteral("alarm-02"));
    serverClosedPopup->show();
    app.processEvents();
    if (!serverClosedPopup || !serverClosedPopup->isVisible()) return 7;
    serverClosedPopup->dismiss();
    app.processEvents();
    if (serverClosedPopup && serverClosedPopup->isVisible()) return 8;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    return 0;
}
