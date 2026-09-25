#include "firealarmpopup.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

FireAlarmPopup::FireAlarmPopup(const QString &channelId,
                               const QString &alarmId,
                               QWidget *parent)
    : QDialog(parent)
    , m_channelId(channelId)
    , m_alarmId(alarmId)
{
    setObjectName(QStringLiteral("fireAlarmPopup_%1").arg(channelId));
    setWindowTitle(QStringLiteral("Camera channel alert"));
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    auto *title = new QLabel(
        QStringLiteral("Potential fire detected on %1").arg(channelId), this);
    title->setObjectName(QStringLiteral("fireAlarmPopupTitle"));
    title->setStyleSheet(QStringLiteral(
        "color:#b71c1c; font-size:18px; font-weight:800;"));
    layout->addWidget(title);

    auto *message = new QLabel(
        QStringLiteral("Verify the camera feed, then press Check."), this);
    message->setWordWrap(true);
    layout->addWidget(message);

    auto *checkButton = new QPushButton(QStringLiteral("Check"), this);
    checkButton->setObjectName(QStringLiteral("fireAlarmCheckButton"));
    checkButton->setDefault(true);
    checkButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#b71c1c; color:white; border:none; "
        "border-radius:5px; padding:10px 24px; font-weight:800; }"
        "QPushButton:hover { background:#c62828; }"
        "QPushButton:pressed { background:#7f0000; }"));
    layout->addWidget(checkButton, 0, Qt::AlignRight);

    connect(checkButton, &QPushButton::clicked, this, [this, checkButton]() {
        checkButton->setEnabled(false);
        hide();
        emit checkRequested(m_channelId, m_alarmId);
        deleteLater();
    });
}

void FireAlarmPopup::dismiss()
{
    QDialog::done(QDialog::Rejected);
}

void FireAlarmPopup::reject()
{
    // The operator must use Check. Pi ACK or FIRE_CLEARED closes the popup
    // through the controller-driven UI flow.
}
