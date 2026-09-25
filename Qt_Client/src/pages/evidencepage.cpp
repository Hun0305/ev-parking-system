#include "evidencepage.h"

#include "api/imageloader.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

class EvidenceImageLabel : public QLabel
{
public:
    explicit EvidenceImageLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(320, 230);
        setText(QStringLiteral("Select a parking slot to load evidence"));
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
            size() - QSize(16, 16), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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

QIcon evidenceHelpIcon()
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
} // namespace

EvidencePage::EvidencePage(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(12);

    auto *slotPanel = new QFrame(this);
    slotPanel->setObjectName(QStringLiteral("evidenceSlotPanel"));
    slotPanel->setFixedWidth(245);
    slotPanel->setStyleSheet(QStringLiteral(
        "QFrame#evidenceSlotPanel { background:white; border:1px solid #c7cdd4; "
        "border-radius:6px; }"));
    auto *slotLayout = new QVBoxLayout(slotPanel);
    slotLayout->setContentsMargins(12, 12, 12, 12);
    slotLayout->setSpacing(8);
    auto *slotTitle = new QLabel(QStringLiteral("Parking slots"), slotPanel);
    slotTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:800;color:#263238;"));
    slotLayout->addWidget(slotTitle);
    m_slotSearch = new QLineEdit(slotPanel);
    m_slotSearch->setPlaceholderText(QStringLiteral("Search slot"));
    slotLayout->addWidget(m_slotSearch);
    m_slotList = new QListWidget(slotPanel);
    m_slotList->setObjectName(QStringLiteral("evidenceSlotList"));
    m_slotList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_slotList->setSpacing(2);
    slotLayout->addWidget(m_slotList, 1);
    auto *refreshButton = new QPushButton(QStringLiteral("Refresh evidence"), slotPanel);
    refreshButton->setObjectName(QStringLiteral("evidenceRefreshButton"));
    slotLayout->addWidget(refreshButton);
    rootLayout->addWidget(slotPanel);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    auto *summaryFrame = new QFrame(content);
    summaryFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:white; border:1px solid #c7cdd4; border-radius:6px; }"));
    auto *summaryLayout = new QHBoxLayout(summaryFrame);
    summaryLayout->setContentsMargins(14, 10, 14, 10);
    auto *summaryTextLayout = new QVBoxLayout;
    m_summaryLabel = new QLabel(QStringLiteral("Evidence gallery"), summaryFrame);
    m_summaryLabel->setObjectName(QStringLiteral("evidenceSummaryLabel"));
    m_summaryLabel->setStyleSheet(QStringLiteral(
        "border:none;font-size:17px;font-weight:800;color:#263238;"));
    m_statusLabel = new QLabel(
        QStringLiteral("Select a slot to compare its first and latest captures."), summaryFrame);
    m_statusLabel->setObjectName(QStringLiteral("evidenceStatusLabel"));
    m_statusLabel->setStyleSheet(QStringLiteral("border:none;color:#546e7a;"));
    summaryTextLayout->addWidget(m_summaryLabel);
    summaryTextLayout->addWidget(m_statusLabel);
    summaryLayout->addLayout(summaryTextLayout, 1);
    auto *helpButton = new QPushButton(QStringLiteral("도움말"), summaryFrame);
    helpButton->setObjectName(QStringLiteral("evidenceHelpButton"));
    helpButton->setAccessibleName(QStringLiteral("Evidence 도움말"));
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setToolTip(QStringLiteral("Evidence 화면 사용 방법 보기"));
    helpButton->setIcon(evidenceHelpIcon());
    helpButton->setIconSize(QSize(22, 22));
    helpButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:#455a64; border:none; "
        "border-radius:5px; padding:5px 8px; font-weight:700; }"
        "QPushButton:hover { background:#fff3e0; color:#e65100; }"
        "QPushButton:pressed { background:#ffe0b2; }"));
    summaryLayout->addWidget(helpButton, 0, Qt::AlignTop);
    contentLayout->addWidget(summaryFrame);

    auto *comparisonLayout = new QHBoxLayout;
    comparisonLayout->setSpacing(10);
    auto createCaptureCard = [this](const QString &title,
                                    EvidenceImageLabel *&imageLabel,
                                    QLabel *&titleLabel,
                                    QLabel *&metadataLabel,
                                    QPushButton *&openButton) {
        auto *group = new QGroupBox(title, this);
        auto *layout = new QVBoxLayout(group);
        layout->setSpacing(7);
        titleLabel = new QLabel(title, group);
        titleLabel->setStyleSheet(QStringLiteral("font-size:15px;font-weight:800;color:#263238;"));
        imageLabel = new EvidenceImageLabel(group);
        metadataLabel = new QLabel(QStringLiteral("No capture loaded"), group);
        metadataLabel->setWordWrap(true);
        metadataLabel->setMinimumHeight(38);
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

    QGroupBox *firstGroup = createCaptureCard(
        QStringLiteral("First capture"), m_firstImageLabel, m_firstTitleLabel,
        m_firstMetadataLabel, m_firstOpenButton);
    QGroupBox *selectedGroup = createCaptureCard(
        QStringLiteral("Latest capture"), m_selectedImageLabel, m_selectedTitleLabel,
        m_selectedMetadataLabel, m_selectedOpenButton);
    m_firstTitleLabel->setObjectName(QStringLiteral("evidenceFirstTitle"));
    m_selectedTitleLabel->setObjectName(QStringLiteral("evidenceSelectedTitle"));
    m_firstOpenButton->setObjectName(QStringLiteral("evidenceFirstOpenButton"));
    m_selectedOpenButton->setObjectName(QStringLiteral("evidenceSelectedOpenButton"));
    comparisonLayout->addWidget(firstGroup, 1);
    comparisonLayout->addWidget(selectedGroup, 1);
    contentLayout->addLayout(comparisonLayout, 1);

    auto *timelineGroup = new QGroupBox(QStringLiteral("Capture timeline"), content);
    auto *timelineLayout = new QVBoxLayout(timelineGroup);
    m_captureTable = new QTableWidget(0, 5, timelineGroup);
    m_captureTable->setObjectName(QStringLiteral("evidenceCaptureTable"));
    m_captureTable->setHorizontalHeaderLabels({
        QStringLiteral("Capture"), QStringLiteral("Time"), QStringLiteral("Reason"),
        QStringLiteral("OCR"), QStringLiteral("Available")});
    m_captureTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_captureTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_captureTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_captureTable->verticalHeader()->setVisible(false);
    m_captureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_captureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_captureTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_captureTable->setMaximumHeight(190);
    timelineLayout->addWidget(m_captureTable);
    contentLayout->addWidget(timelineGroup);
    rootLayout->addWidget(content, 1);

    connect(m_slotList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                handleSlotChanged(current);
            });
    connect(m_slotSearch, &QLineEdit::textChanged,
            this, &EvidencePage::filterSlots);
    connect(refreshButton, &QPushButton::clicked,
            this, &EvidencePage::requestCurrentEvidence);
    connect(helpButton, &QPushButton::clicked,
            this, &EvidencePage::showHelpDialog);
    connect(m_captureTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                renderSelectedCapture(currentRow);
            });
    connect(m_firstOpenButton, &QPushButton::clicked, this,
            [this]() { showFullImage(m_firstImageLabel, m_firstTitleLabel->text()); });
    connect(m_selectedOpenButton, &QPushButton::clicked, this,
            [this]() { showFullImage(m_selectedImageLabel, m_selectedTitleLabel->text()); });
}

void EvidencePage::setImageLoader(ImageLoader *imageLoader)
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
                EvidenceImageLabel *target = m_requestTargets.take(requestId);
                if (!target || target->property("evidenceRequestId").toString() != requestId) {
                    return;
                }
                target->setText(QString());
                target->setSourcePixmap(pixmap);
                if (target == m_firstImageLabel) {
                    m_firstOpenButton->setEnabled(true);
                } else if (target == m_selectedImageLabel) {
                    m_selectedOpenButton->setEnabled(true);
                }
            });
    connect(m_imageLoader, &ImageLoader::imageFailed, this,
            [this](const QString &requestId, const QString &message) {
                EvidenceImageLabel *target = m_requestTargets.take(requestId);
                if (!target || target->property("evidenceRequestId").toString() != requestId) {
                    return;
                }
                target->setSourcePixmap(QPixmap());
                target->setText(QStringLiteral("Image load failed\n%1").arg(message));
            });
}

void EvidencePage::render(const ParkingViewState &state)
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
                     static_cast<int>(state.slotImages.value(slot.slotId).size())});
    }
    for (const ParkingSlotInfo &slot : state.parkingSlots) {
        rows.append({slot.slotId, slot.state,
                     static_cast<int>(state.slotImages.value(slot.slotId).size())});
    }
    std::sort(rows.begin(), rows.end(), [](const SlotRow &left, const SlotRow &right) {
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
            QStringLiteral("%1\n%2  ·  %3 image%4")
                .arg(slot.id, slotStateText(slot.state))
                .arg(slot.imageCount)
                .arg(slot.imageCount == 1 ? QString() : QStringLiteral("s")),
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
        if (fallbackRow < 0 && (slot.imageCount > 0
                                || slot.state != SlotState::Vacant)) {
            fallbackRow = row;
        }
    }
    if (preferredRow < 0) {
        preferredRow = fallbackRow >= 0 ? fallbackRow : (rows.isEmpty() ? -1 : 0);
    }
    if (preferredRow >= 0) {
        m_slotList->setCurrentRow(preferredRow);
        m_currentSlotId = m_slotList->item(preferredRow)->data(Qt::UserRole).toString();
    } else {
        m_currentSlotId.clear();
    }
    filterSlots(m_slotSearch->text());
}

void EvidencePage::showEvidence(
    const QString &slotId,
    SlotState state,
    const QString &plateNumber,
    const QList<ParkingImageResource> &images)
{
    if (!m_currentSlotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    m_currentSlotId = slotId;
    m_plateNumber = plateNumber.isEmpty() ? QStringLiteral("-") : plateNumber;
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures = buildParkingCaptureGroups(images);
    m_summaryLabel->setText(QStringLiteral("%1  |  %2  |  Plate: %3")
                                .arg(slotId, slotStateText(state), m_plateNumber));
    m_statusLabel->setText(
        m_captures.isEmpty()
            ? QStringLiteral("No evidence images are available for the active parking session.")
            : QStringLiteral("%1 capture%2 loaded. Select a timeline row to compare it with the first capture.")
                  .arg(m_captures.size())
                  .arg(m_captures.size() == 1 ? QString() : QStringLiteral("s")));
    renderCaptureTable();
    renderFirstCapture();
    renderSelectedCapture(m_captures.size() > 1 ? m_captures.size() - 1 : -1);
}

void EvidencePage::showLoading(const QString &slotId)
{
    if (!slotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures.clear();
    m_captureTable->setRowCount(0);
    m_summaryLabel->setText(QStringLiteral("%1 evidence").arg(slotId));
    m_statusLabel->setText(QStringLiteral("Loading active session and evidence images..."));
    clearCaptureCard(m_firstImageLabel, m_firstTitleLabel, m_firstMetadataLabel,
                     m_firstOpenButton, QStringLiteral("First capture"),
                     QStringLiteral("Loading..."));
    clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                     m_selectedMetadataLabel, m_selectedOpenButton,
                     QStringLiteral("Latest capture"), QStringLiteral("Loading..."));
}

void EvidencePage::showError(const QString &slotId, const QString &message)
{
    if (!slotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    m_statusLabel->setText(QStringLiteral("Could not load evidence: %1").arg(message));
    clearCaptureCard(m_firstImageLabel, m_firstTitleLabel, m_firstMetadataLabel,
                     m_firstOpenButton, QStringLiteral("First capture"),
                     QStringLiteral("Evidence request failed"));
    clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                     m_selectedMetadataLabel, m_selectedOpenButton,
                     QStringLiteral("Latest capture"),
                     QStringLiteral("Evidence request failed"));
}

bool EvidencePage::selectSlot(const QString &rawSlotId)
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

        const bool selectionChanged = m_slotList->currentItem() != item;
        m_slotList->setCurrentItem(item);
        m_slotList->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        if (!selectionChanged) {
            m_currentSlotId = slotId;
            requestCurrentEvidence();
        }
        return true;
    }
    return false;
}

QString EvidencePage::currentSlotId() const
{
    return m_currentSlotId;
}

int EvidencePage::captureCount() const
{
    return m_captures.size();
}

void EvidencePage::requestCurrentEvidence()
{
    if (m_currentSlotId.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No parking slot is available."));
        return;
    }
    showLoading(m_currentSlotId);
    emit evidenceRequested(m_currentSlotId);
}

void EvidencePage::handleSlotChanged(QListWidgetItem *current)
{
    if (!current) {
        return;
    }
    m_currentSlotId = current->data(Qt::UserRole).toString();
    requestCurrentEvidence();
}

void EvidencePage::filterSlots(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_slotList->count(); ++row) {
        QListWidgetItem *item = m_slotList->item(row);
        item->setHidden(!needle.isEmpty()
                        && !item->text().contains(needle, Qt::CaseInsensitive));
    }
}

void EvidencePage::renderCaptureTable()
{
    QSignalBlocker blocker(m_captureTable);
    m_captureTable->setRowCount(m_captures.size());
    for (int row = 0; row < m_captures.size(); ++row) {
        const ParkingCaptureGroup &capture = m_captures.at(row);
        QStringList variants;
        for (const ParkingImageResource &variant : capture.variants) {
            const QString name = variant.processing.isEmpty()
                ? QStringLiteral("Image") : variant.processing;
            if (!variants.contains(name, Qt::CaseInsensitive)) {
                variants.append(name);
            }
        }
        const QString captureName = capture.imageId >= 0
            ? QStringLiteral("#%1").arg(capture.imageId)
            : QStringLiteral("#%1").arg(row + 1);
        const QStringList values = {
            captureName,
            captureTimeText(capture.timestamp),
            captureReasonText(capture.reason),
            capture.ocrResult.isEmpty() ? QStringLiteral("-") : capture.ocrResult,
            variants.join(QStringLiteral(" / "))};
        for (int column = 0; column < values.size(); ++column) {
            m_captureTable->setItem(row, column,
                                    new QTableWidgetItem(values.at(column)));
        }
    }
    if (!m_captures.isEmpty()) {
        m_captureTable->setCurrentCell(m_captures.size() - 1, 0);
    }
}

void EvidencePage::renderFirstCapture()
{
    const ParkingCaptureGroup *capture = m_captures.isEmpty()
        ? nullptr : &m_captures.first();
    renderCaptureCard(capture, QStringLiteral("First capture"), m_firstImageLabel,
                      m_firstTitleLabel, m_firstMetadataLabel,
                      m_firstOpenButton, QStringLiteral("first"));
}

void EvidencePage::renderSelectedCapture(int row)
{
    if (row < 0 || row >= m_captures.size()) {
        clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                         m_selectedMetadataLabel, m_selectedOpenButton,
                         QStringLiteral("Latest capture"),
                         m_captures.isEmpty()
                             ? QStringLiteral("No later capture available")
                             : QStringLiteral("Select a capture from the timeline"));
        return;
    }
    const QString heading = row == m_captures.size() - 1
        ? QStringLiteral("Latest capture") : QStringLiteral("Selected capture");
    renderCaptureCard(&m_captures.at(row), heading, m_selectedImageLabel,
                      m_selectedTitleLabel, m_selectedMetadataLabel,
                      m_selectedOpenButton, QStringLiteral("selected"));
}

void EvidencePage::renderCaptureCard(
    const ParkingCaptureGroup *capture,
    const QString &heading,
    EvidenceImageLabel *imageLabel,
    QLabel *titleLabel,
    QLabel *metadataLabel,
    QPushButton *openButton,
    const QString &requestRole)
{
    if (!capture) {
        clearCaptureCard(imageLabel, titleLabel, metadataLabel, openButton,
                         heading, QStringLiteral("No capture available"));
        return;
    }
    const ParkingImageResource *variant =
        preferredParkingCaptureVariant(*capture);
    const QString reason = captureReasonText(capture->reason);
    titleLabel->setText(QStringLiteral("%1 · %2").arg(heading, reason));
    // OCR 은 HALL_30S 캡처 한 장에만 돌아서 다른 증거 이미지 행에는
    // ocr_result 가 없다. 그 경우 세션에서 확정된 번호판으로 폴백하되,
    // 이 이미지에서 직접 읽은 값이 아님을 (session) 으로 구분한다.
    QString ocrText = capture->ocrResult;
    if (ocrText.isEmpty()) {
        ocrText = (m_plateNumber.isEmpty() || m_plateNumber == QStringLiteral("-"))
            ? QStringLiteral("-")
            : QStringLiteral("%1 (session)").arg(m_plateNumber);
    }
    QString metadata = QStringLiteral("%1  |  OCR: %2")
        .arg(captureTimeText(capture->timestamp), ocrText);
    if (variant && !variant->processing.isEmpty()) {
        metadata += QStringLiteral("  |  %1").arg(variant->processing);
    }
    metadataLabel->setText(metadata);
    imageLabel->setSourcePixmap(QPixmap());
    imageLabel->setProperty("evidenceRequestId", QString());
    openButton->setEnabled(false);

    if (!variant || variant->url.isEmpty()) {
        imageLabel->setText(QStringLiteral("Image URL is not available"));
        return;
    }
    if (!m_imageLoader) {
        imageLabel->setText(QStringLiteral("Image loader is not available"));
        return;
    }

    imageLabel->setText(QStringLiteral("Loading image..."));
    const QString requestId = QStringLiteral("evidence:%1:%2:%3")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(m_requestGeneration)
        .arg(requestRole + QLatin1Char(':') + QString::number(++m_requestSequence));
    imageLabel->setProperty("evidenceRequestId", requestId);
    m_requestTargets.insert(requestId, imageLabel);
    m_imageLoader->load(requestId, variant->url);
}

void EvidencePage::clearCaptureCard(
    EvidenceImageLabel *imageLabel,
    QLabel *titleLabel,
    QLabel *metadataLabel,
    QPushButton *openButton,
    const QString &title,
    const QString &message)
{
    imageLabel->setSourcePixmap(QPixmap());
    imageLabel->setProperty("evidenceRequestId", QString());
    imageLabel->setText(message);
    titleLabel->setText(title);
    metadataLabel->setText(QStringLiteral("No capture metadata"));
    openButton->setEnabled(false);
}

void EvidencePage::showHelpDialog()
{
    if (QDialog *existing = findChild<QDialog *>(
            QStringLiteral("evidenceHelpDialog"))) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("evidenceHelpDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Evidence 사용 안내"));
    dialog->setModal(true);
    dialog->setMinimumWidth(560);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);

    auto *titleLabel = new QLabel(QStringLiteral("Evidence 사용 안내"), dialog);
    titleLabel->setObjectName(QStringLiteral("evidenceHelpTitle"));
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size:20px;font-weight:800;color:#263238;"));
    layout->addWidget(titleLabel);

    auto *introLabel = new QLabel(
        QStringLiteral("주정차 증거를 확인하는 기본 흐름입니다."), dialog);
    introLabel->setStyleSheet(QStringLiteral("color:#546e7a;"));
    layout->addWidget(introLabel);

    auto *stepsFrame = new QFrame(dialog);
    stepsFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:#f7f9fa; border:1px solid #d9e0e5; "
        "border-radius:7px; }"));
    auto *stepsLayout = new QVBoxLayout(stepsFrame);
    stepsLayout->setContentsMargins(16, 14, 16, 14);
    auto *stepsLabel = new QLabel(stepsFrame);
    stepsLabel->setObjectName(QStringLiteral("evidenceHelpSteps"));
    stepsLabel->setTextFormat(Qt::RichText);
    stepsLabel->setWordWrap(true);
    stepsLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#263238;line-height:145%;"));
    stepsLabel->setText(QStringLiteral(
        "<b>1. 슬롯 선택</b><br>왼쪽 목록에서 확인할 주차 슬롯을 선택합니다.<br><br>"
        "<b>2. 캡처 비교</b><br><i>First capture</i>와 <i>Latest capture</i>를 비교합니다.<br><br>"
        "<b>3. 타임라인 확인</b><br>촬영 시간, 사유, OCR 결과와 이미지 종류를 확인합니다.<br><br>"
        "<b>4. 원본 이미지 열기</b><br><i>Open full image</i>로 원본 크기 사진을 확인합니다."));
    stepsLayout->addWidget(stepsLabel);
    layout->addWidget(stepsFrame);

    auto *noteLabel = new QLabel(
        QStringLiteral("※ 사진이 표시되지 않으면 서버에 저장된 증거가 없거나 아직 이미지가 전달되지 않은 상태입니다.\n"
                       "   촬영 사유는 서버 metadata가 제공될 때 표시됩니다."),
        dialog);
    noteLabel->setObjectName(QStringLiteral("evidenceHelpNote"));
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet(QStringLiteral(
        "background:#fff8e1;color:#5d4037;border:1px solid #ffe082;"
        "border-radius:6px;padding:10px;"));
    layout->addWidget(noteLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(QStringLiteral("evidenceHelpButtons"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);

    dialog->open();
}

void EvidencePage::showFullImage(EvidenceImageLabel *source, const QString &title)
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
