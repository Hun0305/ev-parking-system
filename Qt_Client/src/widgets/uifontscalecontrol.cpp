#include "uifontscalecontrol.h"

#include "services/uifontscale.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

UiFontScaleControl::UiFontScaleControl(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("globalUiFontScaleControl"));
    setProperty("uiFontScaleFixed", true);
    setAccessibleName(QStringLiteral("Application font size"));
    setToolTip(QStringLiteral("Application font size · minimum 90%, maximum 200%"));
    setFixedSize(126, 34);
    setStyleSheet(QStringLiteral(
        "QWidget#globalUiFontScaleControl { background:#ffffff;"
        "border:1px solid #8ca7b3;border-radius:6px; }"
        "QToolButton { background:transparent;color:#294b5a;border:none;"
        "font-size:16px;font-weight:900; }"
        "QToolButton:hover:!disabled { background:#edf4f7;color:#ef7d00; }"
        "QToolButton:disabled { color:#b8c3c8;background:#f4f6f7; }"
        "QLabel { color:#294b5a;border-left:1px solid #d5dee3;"
        "border-right:1px solid #d5dee3;font-size:11px;font-weight:800; }"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_decreaseButton = new QToolButton(this);
    m_decreaseButton->setObjectName(QStringLiteral("decreaseUiFontScaleButton"));
    m_decreaseButton->setText(QString::fromUtf8("−"));
    m_decreaseButton->setAccessibleName(QStringLiteral("Decrease application font size"));
    m_decreaseButton->setFixedSize(34, 32);
    m_decreaseButton->setAutoRepeat(true);
    m_decreaseButton->setAutoRepeatDelay(350);
    m_valueLabel = new QLabel(this);
    m_valueLabel->setObjectName(QStringLiteral("uiFontScaleValueLabel"));
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setFixedSize(58, 32);
    m_increaseButton = new QToolButton(this);
    m_increaseButton->setObjectName(QStringLiteral("increaseUiFontScaleButton"));
    m_increaseButton->setText(QStringLiteral("+"));
    m_increaseButton->setAccessibleName(QStringLiteral("Increase application font size"));
    m_increaseButton->setFixedSize(34, 32);
    m_increaseButton->setAutoRepeat(true);
    m_increaseButton->setAutoRepeatDelay(350);
    layout->addWidget(m_decreaseButton);
    layout->addWidget(m_valueLabel);
    layout->addWidget(m_increaseButton);
    setPercent(100);

    connect(m_decreaseButton, &QToolButton::clicked, this, [this]() {
        setPercent(m_percent - UiFontScale::StepPercent);
        emit percentChangeRequested(m_percent);
    });
    connect(m_increaseButton, &QToolButton::clicked, this, [this]() {
        setPercent(m_percent + UiFontScale::StepPercent);
        emit percentChangeRequested(m_percent);
    });
}

void UiFontScaleControl::setPercent(int percent)
{
    m_percent = UiFontScale::normalizePercent(percent);
    updateControlState();
}

void UiFontScaleControl::updateControlState()
{
    m_valueLabel->setText(QStringLiteral("%1%").arg(m_percent));
    m_decreaseButton->setEnabled(m_percent > UiFontScale::MinimumPercent);
    m_increaseButton->setEnabled(m_percent < UiFontScale::MaximumPercent);
    setToolTip(QStringLiteral(
        "Application font size %1% · minimum 90%, maximum 200%")
                   .arg(m_percent));
}
