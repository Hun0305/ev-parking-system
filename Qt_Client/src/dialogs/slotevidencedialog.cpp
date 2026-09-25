#include "slotevidencedialog.h"

#include "api/imageloader.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

SlotEvidenceDialog::SlotEvidenceDialog(const QString &slotId, SlotState state,
                                       const QString &plateNumber,
                                       const QList<ParkingImageResource> &images,
                                       ImageLoader *imageLoader, QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("%1 image evidence").arg(slotId));
    resize(1040, 720);
    auto *rootLayout = new QVBoxLayout(this);
    auto *summary = new QLabel(QStringLiteral("%1 | %2 | Plate: %3")
                                   .arg(slotId, slotStateText(state), plateNumber), this);
    summary->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700;"));
    rootLayout->addWidget(summary);

    const QStringList keys = {QStringLiteral("VEHICLE_ORIGINAL"), QStringLiteral("VEHICLE_ENHANCED"),
                              QStringLiteral("PLATE_ORIGINAL"), QStringLiteral("PLATE_ENHANCED")};
    const QStringList titles = {QStringLiteral("Vehicle - Original"), QStringLiteral("Vehicle - Enhanced"),
                                QStringLiteral("Plate - Original"), QStringLiteral("Plate - Enhanced")};
    auto *grid = new QGridLayout;
    QHash<QString, QLabel *> targets;
    for (int index = 0; index < keys.size(); ++index) {
        auto *group = new QGroupBox(titles.at(index), this);
        auto *layout = new QVBoxLayout(group);
        auto *imageLabel = new QLabel(QStringLiteral("Image not available"), group);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setMinimumSize(400, 240);
        imageLabel->setStyleSheet(QStringLiteral("background:#111820;color:#b0bec5;border:1px solid #455a64;"));
        layout->addWidget(imageLabel);
        grid->addWidget(group, index / 2, index % 2);
        targets.insert(keys.at(index), imageLabel);
    }
    rootLayout->addLayout(grid, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
    if (!imageLoader) return;

    const QString requestPrefix = QStringLiteral("%1:%2").arg(slotId).arg(reinterpret_cast<quintptr>(this));
    QHash<QString, QLabel *> requestTargets;
    QHash<QString, QUrl> requestUrls;
    for (const ParkingImageResource &image : images) {
        const QString key = image.role.toUpper() + QLatin1Char('_') + image.processing.toUpper();
        if (!targets.contains(key) || image.url.isEmpty()) continue;
        const QString requestId = requestPrefix + QLatin1Char(':') + key;
        targets.value(key)->setText(QStringLiteral("Loading..."));
        requestTargets.insert(requestId, targets.value(key));
        requestUrls.insert(requestId, image.url);
    }
    connect(imageLoader, &ImageLoader::imageLoaded, this,
            [requestTargets](const QString &requestId, const QPixmap &pixmap) {
                if (QLabel *target = requestTargets.value(requestId, nullptr)) {
                    target->setPixmap(pixmap.scaled(target->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                }
            });
    connect(imageLoader, &ImageLoader::imageFailed, this,
            [requestTargets](const QString &requestId, const QString &message) {
                if (QLabel *target = requestTargets.value(requestId, nullptr)) {
                    target->setText(QStringLiteral("Image load failed\n%1").arg(message));
                }
            });
    for (auto it = requestUrls.cbegin(); it != requestUrls.cend(); ++it) imageLoader->load(it.key(), it.value());
}
