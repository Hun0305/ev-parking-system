#include "imagecomparepage.h"

#include "api/imageloader.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

class ImageCompareImageLabel : public QLabel
{
public:
    explicit ImageCompareImageLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(320, 230);
        setText(QStringLiteral("Select a capture to compare"));
        setStyleSheet(QStringLiteral(
            "QLabel { background:#111820; color:#b0bec5; border:1px solid #455a64; "
            "border-radius:5px; padding:8px; }"));
    }

    void setSourcePixmap(const QPixmap &pixmap)
    {
        m_sourcePixmap = pixmap;
        updateScaledPixmap();
    }

    const QPixmap &sourcePixmap() const
    {
        return m_sourcePixmap;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateScaledPixmap();
    }

private:
    void updateScaledPixmap()
    {
        if (m_sourcePixmap.isNull()) {
            setPixmap(QPixmap());
            return;
        }
        setPixmap(m_sourcePixmap.scaled(
            size() - QSize(16, 16), Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    }

    QPixmap m_sourcePixmap;
};

namespace {
QString captureTimeText(const QDateTime &timestamp)
{
    return timestamp.isValid()
        ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("Time unavailable");
}

QString captureReasonText(const QString &reason)
{
    if (reason.isEmpty() || reason == QStringLiteral("EVIDENCE")) {
        return QStringLiteral("Evidence capture");
    }
    QString text = reason;
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    return text;
}

int slotNumber(const QString &slotId)
{
    bool ok = false;
    const int number = slotId.section(QLatin1Char('-'), -1).toInt(&ok);
    return ok ? number : 0;
}
} // namespace

ImageComparePage::ImageComparePage(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(12);

    auto *slotPanel = new QFrame(this);
    slotPanel->setObjectName(QStringLiteral("imageCompareSlotPanel"));
    slotPanel->setFixedWidth(245);
    slotPanel->setStyleSheet(QStringLiteral(
        "QFrame#imageCompareSlotPanel { background:white; border:1px solid #c7cdd4; "
        "border-radius:6px; }"));
    auto *slotLayout = new QVBoxLayout(slotPanel);
    slotLayout->setContentsMargins(12, 12, 12, 12);
    slotLayout->setSpacing(8);
    auto *slotTitle = new QLabel(QStringLiteral("Parking slots"), slotPanel);
    slotTitle->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:800;color:#263238;"));
    slotLayout->addWidget(slotTitle);
    m_slotSearch = new QLineEdit(slotPanel);
    m_slotSearch->setPlaceholderText(QStringLiteral("Search slot"));
    slotLayout->addWidget(m_slotSearch);
    m_slotList = new QListWidget(slotPanel);
    m_slotList->setObjectName(QStringLiteral("imageCompareSlotList"));
    m_slotList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_slotList->setSpacing(2);
    slotLayout->addWidget(m_slotList, 1);
    auto *refreshButton = new QPushButton(
        QStringLiteral("Refresh images"), slotPanel);
    refreshButton->setObjectName(QStringLiteral("imageCompareRefreshButton"));
    slotLayout->addWidget(refreshButton);
    rootLayout->addWidget(slotPanel);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    auto *summaryFrame = new QFrame(content);
    summaryFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:white; border:1px solid #c7cdd4; border-radius:6px; }"));
    auto *summaryLayout = new QVBoxLayout(summaryFrame);
    summaryLayout->setContentsMargins(14, 10, 14, 10);
    m_summaryLabel = new QLabel(QStringLiteral("Image quality comparison"),
                                summaryFrame);
    m_summaryLabel->setObjectName(QStringLiteral("imageCompareSummaryLabel"));
    m_summaryLabel->setStyleSheet(QStringLiteral(
        "border:none;font-size:17px;font-weight:800;color:#263238;"));
    m_statusLabel = new QLabel(
        QStringLiteral("Select a slot and capture to compare original and enhanced images."),
        summaryFrame);
    m_statusLabel->setObjectName(QStringLiteral("imageCompareStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("border:none;color:#546e7a;"));
    summaryLayout->addWidget(m_summaryLabel);
    summaryLayout->addWidget(m_statusLabel);
    contentLayout->addWidget(summaryFrame);

    auto *comparisonLayout = new QHBoxLayout;
    comparisonLayout->setSpacing(10);
    auto createCard = [this](const QString &title,
                             ImageCompareImageLabel *&imageLabel,
                             QLabel *&titleLabel,
                             QLabel *&metadataLabel,
                             QPushButton *&openButton) {
        auto *group = new QGroupBox(title, this);
        auto *layout = new QVBoxLayout(group);
        layout->setSpacing(7);
        titleLabel = new QLabel(title, group);
        titleLabel->setStyleSheet(QStringLiteral(
            "font-size:15px;font-weight:800;color:#263238;"));
        imageLabel = new ImageCompareImageLabel(group);
        metadataLabel = new QLabel(QStringLiteral("No image loaded"), group);
        metadataLabel->setWordWrap(true);
        metadataLabel->setMinimumHeight(42);
        metadataLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
        openButton = new QPushButton(QStringLiteral("Open full image"), group);
        openButton->setEnabled(false);
        layout->addWidget(titleLabel);
        layout->addWidget(imageLabel, 1);
        layout->addWidget(metadataLabel);
        auto *buttonLayout = new QHBoxLayout;
        buttonLayout->addStretch();
        buttonLayout->addWidget(openButton);
        layout->addLayout(buttonLayout);
        return group;
    };

    QGroupBox *originalGroup = createCard(
        QStringLiteral("Original"), m_originalImageLabel, m_originalTitleLabel,
        m_originalMetadataLabel, m_originalOpenButton);
    QGroupBox *enhancedGroup = createCard(
        QStringLiteral("Enhanced"), m_enhancedImageLabel, m_enhancedTitleLabel,
        m_enhancedMetadataLabel, m_enhancedOpenButton);
    m_originalImageLabel->setObjectName(
        QStringLiteral("imageCompareOriginalImage"));
    m_enhancedImageLabel->setObjectName(
        QStringLiteral("imageCompareEnhancedImage"));
    m_originalTitleLabel->setObjectName(
        QStringLiteral("imageCompareOriginalTitle"));
    m_enhancedTitleLabel->setObjectName(
        QStringLiteral("imageCompareEnhancedTitle"));
    m_originalOpenButton->setObjectName(
        QStringLiteral("imageCompareOriginalOpenButton"));
    m_enhancedOpenButton->setObjectName(
        QStringLiteral("imageCompareEnhancedOpenButton"));
    comparisonLayout->addWidget(originalGroup, 1);
    comparisonLayout->addWidget(enhancedGroup, 1);
    contentLayout->addLayout(comparisonLayout, 1);

    auto *captureGroup = new QGroupBox(
        QStringLiteral("Capture selection"), content);
    auto *captureLayout = new QVBoxLayout(captureGroup);
    m_captureTable = new QTableWidget(0, 5, captureGroup);
    m_captureTable->setObjectName(QStringLiteral("imageCompareCaptureTable"));
    m_captureTable->setHorizontalHeaderLabels({
        QStringLiteral("Capture"), QStringLiteral("Time"),
        QStringLiteral("Reason"), QStringLiteral("Original"),
        QStringLiteral("Enhanced")});
    m_captureTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    m_captureTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::ResizeToContents);
    m_captureTable->verticalHeader()->setVisible(false);
    m_captureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_captureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_captureTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_captureTable->setMaximumHeight(190);
    captureLayout->addWidget(m_captureTable);
    contentLayout->addWidget(captureGroup);
    rootLayout->addWidget(content, 1);

    connect(m_slotList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                handleSlotChanged(current);
            });
    connect(m_slotSearch, &QLineEdit::textChanged,
            this, &ImageComparePage::filterSlots);
    connect(refreshButton, &QPushButton::clicked,
            this, &ImageComparePage::requestCurrentComparison);
    connect(m_captureTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                renderSelectedCapture(currentRow);
            });
    connect(m_originalOpenButton, &QPushButton::clicked, this,
            [this]() {
                showFullImage(m_originalImageLabel,
                              m_originalTitleLabel->text());
            });
    connect(m_enhancedOpenButton, &QPushButton::clicked, this,
            [this]() {
                showFullImage(m_enhancedImageLabel,
                              m_enhancedTitleLabel->text());
            });
}

void ImageComparePage::setImageLoader(ImageLoader *imageLoader)
{
    if (m_imageLoader == imageLoader) {
        return;
    }
    if (m_imageLoader) {
        disconnect(m_imageLoader, nullptr, this, nullptr);
    }
    m_imageLoader = imageLoader;
    if (!m_imageLoader) {
        return;
    }

    connect(m_imageLoader, &ImageLoader::imageLoaded, this,
            [this](const QString &requestId, const QPixmap &pixmap) {
                ImageCompareImageLabel *target =
                    m_requestTargets.take(requestId);
                if (!target
                    || target->property("imageCompareRequestId").toString()
                           != requestId) {
                    return;
                }
                target->setText(QString());
                target->setSourcePixmap(pixmap);
                if (target == m_originalImageLabel) {
                    m_originalOpenButton->setEnabled(true);
                } else if (target == m_enhancedImageLabel) {
                    m_enhancedOpenButton->setEnabled(true);
                }
            });
    connect(m_imageLoader, &ImageLoader::imageFailed, this,
            [this](const QString &requestId, const QString &message) {
                ImageCompareImageLabel *target =
                    m_requestTargets.take(requestId);
                if (!target
                    || target->property("imageCompareRequestId").toString()
                           != requestId) {
                    return;
                }
                target->setSourcePixmap(QPixmap());
                target->setText(
                    QStringLiteral("Image load failed\n%1").arg(message));
            });
}

void ImageComparePage::render(const ParkingViewState &state)
{
    struct SlotRow {
        QString id;
        SlotState state = SlotState::Vacant;
        int imageCount = 0;
    };
    QVector<SlotRow> rows;
    rows.reserve(state.evSlots.size() + state.parkingSlots.size());
    for (const EvSlotInfo &slot : state.evSlots) {
        rows.append({slot.slotId, slot.state,
                     static_cast<int>(
                         state.slotImages.value(slot.slotId).size())});
    }
    for (const ParkingSlotInfo &slot : state.parkingSlots) {
        rows.append({slot.slotId, slot.state,
                     static_cast<int>(
                         state.slotImages.value(slot.slotId).size())});
    }
    std::sort(rows.begin(), rows.end(),
              [](const SlotRow &left, const SlotRow &right) {
        const bool leftEv = left.id.startsWith(QStringLiteral("EV-"));
        const bool rightEv = right.id.startsWith(QStringLiteral("EV-"));
        if (leftEv != rightEv) {
            return leftEv;
        }
        return slotNumber(left.id) < slotNumber(right.id);
    });

    const QString previousSlotId = m_currentSlotId;
    QSignalBlocker blocker(m_slotList);
    m_slotList->clear();
    int preferredRow = -1;
    int fallbackRow = -1;
    for (int row = 0; row < rows.size(); ++row) {
        const SlotRow &slot = rows.at(row);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2  ·  %3 image resource%4")
                .arg(slot.id, slotStateText(slot.state))
                .arg(slot.imageCount)
                .arg(slot.imageCount == 1 ? QString()
                                          : QStringLiteral("s")),
            m_slotList);
        item->setData(Qt::UserRole, slot.id);
        item->setSizeHint(QSize(0, 52));
        if (slot.state == SlotState::OvertimeAlert
            || slot.state == SlotState::NonEvAlert) {
            item->setForeground(QColor(QStringLiteral("#b71c1c")));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        if (slot.id == previousSlotId) {
            preferredRow = row;
        }
        if (fallbackRow < 0
            && (slot.imageCount > 0 || slot.state != SlotState::Vacant)) {
            fallbackRow = row;
        }
    }
    if (preferredRow < 0) {
        preferredRow = fallbackRow >= 0
            ? fallbackRow : (rows.isEmpty() ? -1 : 0);
    }
    if (preferredRow >= 0) {
        m_slotList->setCurrentRow(preferredRow);
        m_currentSlotId =
            m_slotList->item(preferredRow)->data(Qt::UserRole).toString();
    } else {
        m_currentSlotId.clear();
    }
    filterSlots(m_slotSearch->text());
}

void ImageComparePage::showComparison(
    const QString &slotId,
    SlotState state,
    const QString &plateNumber,
    const QList<ParkingImageResource> &images)
{
    if (!m_currentSlotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    m_currentSlotId = slotId;
    m_plateNumber = plateNumber;
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures = buildParkingCaptureGroups(images);
    m_summaryLabel->setText(
        QStringLiteral("%1  |  %2  |  Plate: %3")
            .arg(slotId, slotStateText(state),
                 plateNumber.isEmpty() ? QStringLiteral("-") : plateNumber));
    renderCaptureTable();

    int selectedRow = m_captures.isEmpty() ? -1 : m_captures.size() - 1;
    for (int row = m_captures.size() - 1; row >= 0; --row) {
        const ParkingCaptureGroup &capture = m_captures.at(row);
        if (parkingCaptureVariant(capture, QStringLiteral("ORIGINAL"))
            && parkingCaptureVariant(capture, QStringLiteral("ENHANCED"))) {
            selectedRow = row;
            break;
        }
    }
    if (selectedRow >= 0) {
        QSignalBlocker blocker(m_captureTable);
        m_captureTable->setCurrentCell(selectedRow, 0);
    }
    renderSelectedCapture(selectedRow);
}

void ImageComparePage::showLoading(const QString &slotId)
{
    if (!slotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures.clear();
    m_selectedCaptureRow = -1;
    m_captureTable->setRowCount(0);
    m_summaryLabel->setText(
        QStringLiteral("%1 image comparison").arg(slotId));
    m_statusLabel->setText(
        QStringLiteral("Loading active session image variants..."));
    clearVariantCard(m_originalImageLabel, m_originalTitleLabel,
                     m_originalMetadataLabel, m_originalOpenButton,
                     QStringLiteral("Original"), QStringLiteral("Loading..."));
    clearVariantCard(m_enhancedImageLabel, m_enhancedTitleLabel,
                     m_enhancedMetadataLabel, m_enhancedOpenButton,
                     QStringLiteral("Enhanced"), QStringLiteral("Loading..."));
}

void ImageComparePage::showError(const QString &slotId,
                                 const QString &message)
{
    if (!slotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    m_statusLabel->setText(
        QStringLiteral("Could not load image comparison: %1").arg(message));
    clearVariantCard(m_originalImageLabel, m_originalTitleLabel,
                     m_originalMetadataLabel, m_originalOpenButton,
                     QStringLiteral("Original"),
                     QStringLiteral("Image request failed"));
    clearVariantCard(m_enhancedImageLabel, m_enhancedTitleLabel,
                     m_enhancedMetadataLabel, m_enhancedOpenButton,
                     QStringLiteral("Enhanced"),
                     QStringLiteral("Image request failed"));
}

bool ImageComparePage::selectSlot(const QString &rawSlotId)
{
    const QString slotId = normalizeParkingSlotId(rawSlotId);
    if (slotId.isEmpty()) {
        return false;
    }
    m_slotSearch->clear();
    for (int row = 0; row < m_slotList->count(); ++row) {
        QListWidgetItem *item = m_slotList->item(row);
        if (item->data(Qt::UserRole).toString() != slotId) {
            continue;
        }
        const bool changed = m_slotList->currentItem() != item;
        m_slotList->setCurrentItem(item);
        m_slotList->scrollToItem(
            item, QAbstractItemView::PositionAtCenter);
        if (!changed) {
            m_currentSlotId = slotId;
            requestCurrentComparison();
        }
        return true;
    }
    return false;
}

QString ImageComparePage::currentSlotId() const
{
    return m_currentSlotId;
}

int ImageComparePage::captureCount() const
{
    return m_captures.size();
}

qint64 ImageComparePage::selectedImageId() const
{
    return m_selectedCaptureRow >= 0
            && m_selectedCaptureRow < m_captures.size()
        ? m_captures.at(m_selectedCaptureRow).imageId : -1;
}

void ImageComparePage::requestCurrentComparison()
{
    if (m_currentSlotId.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No parking slot is available."));
        return;
    }
    showLoading(m_currentSlotId);
    emit comparisonRequested(m_currentSlotId);
}

void ImageComparePage::handleSlotChanged(QListWidgetItem *current)
{
    if (!current) {
        return;
    }
    m_currentSlotId = current->data(Qt::UserRole).toString();
    requestCurrentComparison();
}

void ImageComparePage::filterSlots(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_slotList->count(); ++row) {
        QListWidgetItem *item = m_slotList->item(row);
        item->setHidden(!needle.isEmpty()
                        && !item->text().contains(
                            needle, Qt::CaseInsensitive));
    }
}

void ImageComparePage::renderCaptureTable()
{
    QSignalBlocker blocker(m_captureTable);
    m_captureTable->setRowCount(m_captures.size());
    for (int row = 0; row < m_captures.size(); ++row) {
        const ParkingCaptureGroup &capture = m_captures.at(row);
        const bool hasOriginal = parkingCaptureVariant(
            capture, QStringLiteral("ORIGINAL"));
        const bool hasEnhanced = parkingCaptureVariant(
            capture, QStringLiteral("ENHANCED"));
        const QString captureName = capture.imageId >= 0
            ? QStringLiteral("#%1").arg(capture.imageId)
            : QStringLiteral("#%1").arg(row + 1);
        const QStringList values = {
            captureName,
            captureTimeText(capture.timestamp),
            captureReasonText(capture.reason),
            hasOriginal ? QStringLiteral("READY") : QStringLiteral("N/A"),
            hasEnhanced ? QStringLiteral("READY") : QStringLiteral("N/A")};
        for (int column = 0; column < values.size(); ++column) {
            m_captureTable->setItem(
                row, column, new QTableWidgetItem(values.at(column)));
        }
    }
}

void ImageComparePage::renderSelectedCapture(int row)
{
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_selectedCaptureRow =
        row >= 0 && row < m_captures.size() ? row : -1;
    if (m_selectedCaptureRow < 0) {
        m_statusLabel->setText(
            m_captures.isEmpty()
                ? QStringLiteral("No image captures are available for the active parking session.")
                : QStringLiteral("Select a capture from the table."));
        clearVariantCard(m_originalImageLabel, m_originalTitleLabel,
                         m_originalMetadataLabel, m_originalOpenButton,
                         QStringLiteral("Original"),
                         QStringLiteral("No capture selected"));
        clearVariantCard(m_enhancedImageLabel, m_enhancedTitleLabel,
                         m_enhancedMetadataLabel, m_enhancedOpenButton,
                         QStringLiteral("Enhanced"),
                         QStringLiteral("No capture selected"));
        return;
    }

    const ParkingCaptureGroup &capture =
        m_captures.at(m_selectedCaptureRow);
    const bool hasOriginal = parkingCaptureVariant(
        capture, QStringLiteral("ORIGINAL"));
    const bool hasEnhanced = parkingCaptureVariant(
        capture, QStringLiteral("ENHANCED"));
    const QString captureName = capture.imageId >= 0
        ? QStringLiteral("capture #%1").arg(capture.imageId)
        : QStringLiteral("selected capture");
    if (hasOriginal && hasEnhanced) {
        m_statusLabel->setText(QStringLiteral(
            "%1 has both variants. Compare the same captured moment side by side.")
            .arg(captureName));
    } else if (!hasEnhanced) {
        m_statusLabel->setText(QStringLiteral(
            "%1 has no enhanced image yet. The original is preserved and shown when available.")
            .arg(captureName));
    } else {
        m_statusLabel->setText(QStringLiteral(
            "%1 has no original image. The enhanced image is not treated as a replacement.")
            .arg(captureName));
    }

    renderVariantCard(&capture, QStringLiteral("ORIGINAL"),
                      m_originalImageLabel, m_originalTitleLabel,
                      m_originalMetadataLabel, m_originalOpenButton,
                      QStringLiteral("original"));
    renderVariantCard(&capture, QStringLiteral("ENHANCED"),
                      m_enhancedImageLabel, m_enhancedTitleLabel,
                      m_enhancedMetadataLabel, m_enhancedOpenButton,
                      QStringLiteral("enhanced"));
}

void ImageComparePage::renderVariantCard(
    const ParkingCaptureGroup *capture,
    const QString &processing,
    ImageCompareImageLabel *imageLabel,
    QLabel *titleLabel,
    QLabel *metadataLabel,
    QPushButton *openButton,
    const QString &requestRole)
{
    const QString title = processing == QStringLiteral("ORIGINAL")
        ? QStringLiteral("Original") : QStringLiteral("Enhanced");
    if (!capture) {
        clearVariantCard(imageLabel, titleLabel, metadataLabel, openButton,
                         title, QStringLiteral("No capture available"));
        return;
    }

    const ParkingImageResource *variant = parkingCaptureVariant(
        *capture, processing);
    titleLabel->setText(
        QStringLiteral("%1 · %2").arg(title,
                                      captureReasonText(capture->reason)));
    // OCR 은 HALL_30S 캡처 한 장에만 돌아서 다른 캡처 행에는 ocr_result 가
    // 없다. 그 경우 세션에서 확정된 번호판으로 폴백하되, 이 이미지에서 직접
    // 읽은 값이 아님을 (session) 으로 구분한다.
    QString ocrText = capture->ocrResult;
    if (ocrText.isEmpty()) {
        ocrText = m_plateNumber.isEmpty()
            ? QStringLiteral("-")
            : QStringLiteral("%1 (session)").arg(m_plateNumber);
    }
    QString metadata = QStringLiteral("%1  |  OCR: %2")
        .arg(captureTimeText(capture->timestamp), ocrText);
    if (variant && !variant->enhancementType.isEmpty()
        && processing == QStringLiteral("ENHANCED")) {
        metadata += QStringLiteral("  |  %1").arg(variant->enhancementType);
    }
    metadataLabel->setText(metadata);
    imageLabel->setSourcePixmap(QPixmap());
    imageLabel->setProperty("imageCompareRequestId", QString());
    openButton->setEnabled(false);

    if (!variant) {
        imageLabel->setText(
            processing == QStringLiteral("ENHANCED")
                ? QStringLiteral("Enhanced image is not available yet")
                : QStringLiteral("Original image is not available"));
        return;
    }
    if (variant->url.isEmpty()) {
        imageLabel->setText(QStringLiteral("Image URL is not available"));
        return;
    }
    if (!m_imageLoader) {
        imageLabel->setText(QStringLiteral("Image loader is not available"));
        return;
    }

    imageLabel->setText(QStringLiteral("Loading image..."));
    const QString requestId = QStringLiteral("image-compare:%1:%2:%3")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(m_requestGeneration)
        .arg(requestRole + QLatin1Char(':')
             + QString::number(++m_requestSequence));
    imageLabel->setProperty("imageCompareRequestId", requestId);
    m_requestTargets.insert(requestId, imageLabel);
    m_imageLoader->load(requestId, variant->url);
}

void ImageComparePage::clearVariantCard(
    ImageCompareImageLabel *imageLabel,
    QLabel *titleLabel,
    QLabel *metadataLabel,
    QPushButton *openButton,
    const QString &title,
    const QString &message)
{
    imageLabel->setSourcePixmap(QPixmap());
    imageLabel->setProperty("imageCompareRequestId", QString());
    imageLabel->setText(message);
    titleLabel->setText(title);
    metadataLabel->setText(QStringLiteral("No image metadata"));
    openButton->setEnabled(false);
}

void ImageComparePage::showFullImage(ImageCompareImageLabel *source,
                                     const QString &title)
{
    if (!source || source->sourcePixmap().isNull()) {
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(1100, 760);
    auto *layout = new QVBoxLayout(dialog);
    auto *scrollArea = new QScrollArea(dialog);
    auto *imageLabel = new QLabel(scrollArea);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(source->sourcePixmap());
    imageLabel->resize(source->sourcePixmap().size());
    scrollArea->setWidget(imageLabel);
    scrollArea->setAlignment(Qt::AlignCenter);
    layout->addWidget(scrollArea, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}
