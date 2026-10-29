#pragma once

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

struct PageHelpSpec
{
    QString pageKey;
    QString pageName;
    QString title;
    QString introduction;
    QString stepsHtml;
    QString note;
};

inline QIcon pageHelpIcon()
{
    constexpr qreal scale = 2.0;
    QPixmap pixmap(QSize(22, 22) * scale);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(scale);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor orange(QStringLiteral("#fb8c00"));
    QPen outline(orange, 1.8);
    outline.setCapStyle(Qt::RoundCap);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(2.5, 2.5, 17.0, 17.0));

    QFont questionFont = painter.font();
    questionFont.setBold(true);
    questionFont.setPointSizeF(11.0);
    painter.setFont(questionFont);
    painter.drawText(QRectF(0.0, 0.0, 22.0, 21.0),
                     Qt::AlignCenter, QStringLiteral("?"));
    return QIcon(pixmap);
}

inline void openPageHelpDialog(QWidget *page, const PageHelpSpec &spec)
{
    if (!page || spec.pageKey.trimmed().isEmpty()) return;

    const QString dialogName = spec.pageKey + QStringLiteral("HelpDialog");
    if (QDialog *existing = page->findChild<QDialog *>(dialogName)) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(page);
    dialog->setObjectName(dialogName);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(spec.title);
    dialog->setModal(true);
    dialog->setMinimumSize(560, 360);
    dialog->resize(650, 600);

    auto *dialogLayout = new QVBoxLayout(dialog);
    dialogLayout->setContentsMargins(14, 14, 14, 14);
    dialogLayout->setSpacing(10);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setObjectName(spec.pageKey + QStringLiteral("HelpScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(14);

    auto *titleLabel = new QLabel(spec.title, content);
    titleLabel->setObjectName(spec.pageKey + QStringLiteral("HelpTitle"));
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size:20px;font-weight:800;color:#263238;"));
    layout->addWidget(titleLabel);

    auto *introLabel = new QLabel(spec.introduction, content);
    introLabel->setObjectName(spec.pageKey + QStringLiteral("HelpIntroduction"));
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(QStringLiteral("color:#546e7a;"));
    layout->addWidget(introLabel);

    auto *stepsFrame = new QFrame(content);
    stepsFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#f7f9fa; border:1px solid #d9e0e5; "
        "border-radius:7px; }"));
    auto *stepsLayout = new QVBoxLayout(stepsFrame);
    stepsLayout->setContentsMargins(16, 14, 16, 14);
    auto *stepsLabel = new QLabel(stepsFrame);
    stepsLabel->setObjectName(spec.pageKey + QStringLiteral("HelpSteps"));
    stepsLabel->setTextFormat(Qt::RichText);
    stepsLabel->setWordWrap(true);
    stepsLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#263238;line-height:145%;"));
    stepsLabel->setText(spec.stepsHtml);
    stepsLayout->addWidget(stepsLabel);
    layout->addWidget(stepsFrame);

    if (!spec.note.trimmed().isEmpty()) {
        auto *noteLabel = new QLabel(spec.note, content);
        noteLabel->setObjectName(spec.pageKey + QStringLiteral("HelpNote"));
        noteLabel->setWordWrap(true);
        noteLabel->setStyleSheet(QStringLiteral(
            "background:#fff8e1;color:#5d4037;border:1px solid #ffe082;"
            "border-radius:6px;padding:10px;"));
        layout->addWidget(noteLabel);
    }
    layout->addStretch();

    scrollArea->setWidget(content);
    dialogLayout->addWidget(scrollArea, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(spec.pageKey + QStringLiteral("HelpButtons"));
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     dialog, &QDialog::close);
    dialogLayout->addWidget(buttons);
    dialog->open();
}

inline QPushButton *createPageHelpButton(QWidget *page,
                                         QWidget *buttonParent,
                                         const PageHelpSpec &spec)
{
    auto *button = new QPushButton(QStringLiteral("도움말"), buttonParent);
    button->setObjectName(spec.pageKey + QStringLiteral("HelpButton"));
    button->setAccessibleName(spec.pageName + QStringLiteral(" 도움말"));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(spec.pageName + QStringLiteral(" 화면 사용 방법 보기"));
    button->setIcon(pageHelpIcon());
    button->setIconSize(QSize(22, 22));
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:#455a64; border:none; "
        "border-radius:5px; padding:5px 8px; font-weight:700; }"
        "QPushButton:hover { background:#fff3e0; color:#e65100; }"
        "QPushButton:pressed { background:#ffe0b2; }"));
    QObject::connect(button, &QPushButton::clicked, page,
                     [page, spec]() { openPageHelpDialog(page, spec); });
    return button;
}
