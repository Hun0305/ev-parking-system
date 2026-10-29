#include "evidencepage.h"

#include "api/imageloader.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QBuffer>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

class EvidenceImageLabel : public QLabel
{
public:
    explicit EvidenceImageLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(440, 280);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        setText(QStringLiteral("Select a parking slot to load evidence"));
        setStyleSheet(QStringLiteral(
            "QLabel { background:#111820; color:#b0bec5; border:1px solid #455a64; "
            "border-radius:6px; padding:8px; font-weight:700; }"));
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

    QSize sizeHint() const override
    {
        return QSize(620, 320);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(440, 280);
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

QString captureCountText(int imageCount)
{
    if (imageCount < 0) {
        return QStringLiteral("Not loaded");
    }
    return QStringLiteral("%1 image group%2")
        .arg(imageCount)
        .arg(imageCount == 1 ? QString() : QStringLiteral("s"));
}

QString localTimelinePairKey(const QString &slotId,
                             const ParkingCaptureGroup &capture)
{
    if (capture.sessionId > 0) {
        return QStringLiteral("%1|session:%2")
            .arg(slotId)
            .arg(capture.sessionId);
    }

    const ParkingImageResource *variant =
        preferredParkingCaptureVariant(capture);
    return QStringLiteral("%1|unpaired:%2|%3|%4")
        .arg(slotId)
        .arg(capture.imageId)
        .arg(capture.timestamp.toString(Qt::ISODateWithMs),
             variant ? variant->url.toString() : QString());
}

QString capturePairCellText(const ParkingCaptureGroup &capture)
{
    const QString captureName = capture.imageId >= 0
        ? QStringLiteral("#%1").arg(capture.imageId)
        : QStringLiteral("Capture");
    return QStringLiteral("%1 · %2")
        .arg(captureName, captureTimeText(capture.timestamp));
}

QString pairValueText(const QString &firstValue,
                      const QString &latestValue,
                      const QString &fallback = QStringLiteral("-"))
{
    const QString first = firstValue.trimmed().isEmpty()
        ? fallback : firstValue.trimmed();
    const QString latest = latestValue.trimmed().isEmpty()
        ? fallback : latestValue.trimmed();
    return first == latest ? first
                           : QStringLiteral("%1 → %2").arg(first, latest);
}

QString pairAvailabilityText(const ParkingCaptureGroup &first,
                             const ParkingCaptureGroup &latest)
{
    const ParkingImageResource *firstVariant =
        preferredParkingCaptureVariant(first);
    const ParkingImageResource *latestVariant =
        preferredParkingCaptureVariant(latest);
    const bool firstAvailable = firstVariant && !firstVariant->url.isEmpty();
    const bool latestAvailable = latestVariant && !latestVariant->url.isEmpty();
    if (firstAvailable && latestAvailable) {
        return QStringLiteral("FIRST / LATEST");
    }
    if (firstAvailable) {
        return QStringLiteral("FIRST only");
    }
    if (latestAvailable) {
        return QStringLiteral("LATEST only");
    }
    return QStringLiteral("Unavailable");
}

QString safeFileToken(QString value, const QString &fallback)
{
    value = value.trimmed();
    value.replace(QRegularExpression(
                      QStringLiteral("[^\\p{L}\\p{N}._-]+")),
                  QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("_+")),
                  QStringLiteral("_"));
    value = value.trimmed();
    while (value.startsWith(QLatin1Char('.'))
           || value.startsWith(QLatin1Char('_'))) {
        value.remove(0, 1);
    }
    while (value.endsWith(QLatin1Char('.'))
           || value.endsWith(QLatin1Char('_'))) {
        value.chop(1);
    }
    if (value.isEmpty()) {
        value = fallback;
    }
    return value.left(48);
}

QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString downloadedImageExtension(const QByteArray &data,
                                  const QString &contentType)
{
    QImage image;
    if (!image.loadFromData(data)) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QString format = QString::fromLatin1(
        QImageReader::imageFormat(&buffer)).toLower();
    if (format == QStringLiteral("jpeg")) {
        return QStringLiteral("jpg");
    }
    if (format == QStringLiteral("png")
        || format == QStringLiteral("webp")
        || format == QStringLiteral("bmp")) {
        return format;
    }
    const QString normalizedType = contentType.section(QLatin1Char(';'), 0, 0)
                                       .trimmed().toLower();
    if (normalizedType == QStringLiteral("image/jpeg")) {
        return QStringLiteral("jpg");
    }
    if (normalizedType.startsWith(QStringLiteral("image/"))) {
        const QString subtype = safeFileToken(
            normalizedType.mid(6), QStringLiteral("img"));
        if (subtype != QStringLiteral("img")) {
            return subtype;
        }
    }
    return QStringLiteral("img");
}

bool sameImageResources(const QList<ParkingImageResource> &left,
                        const QList<ParkingImageResource> &right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (qsizetype index = 0; index < left.size(); ++index) {
        const ParkingImageResource &leftImage = left.at(index);
        const ParkingImageResource &rightImage = right.at(index);
        if (leftImage.url != rightImage.url
            || leftImage.timestamp != rightImage.timestamp
            || leftImage.role != rightImage.role
            || leftImage.processing != rightImage.processing
            || leftImage.imageId != rightImage.imageId
            || leftImage.sessionId != rightImage.sessionId
            || leftImage.enhancementType != rightImage.enhancementType
            || leftImage.ocrResult != rightImage.ocrResult
            || leftImage.evidenceReason != rightImage.evidenceReason) {
            return false;
        }
    }
    return true;
}

QString slotStateAccent(SlotState state)
{
    switch (state) {
    case SlotState::Vacant:
        return QStringLiteral("#2e7d32");
    case SlotState::Occupied:
        return QStringLiteral("#ef6c00");
    case SlotState::OvertimeAlert:
    case SlotState::NonEvAlert:
        return QStringLiteral("#c62828");
    case SlotState::SensorError:
        return QStringLiteral("#6a1b9a");
    case SlotState::Acked:
        return QStringLiteral("#1565c0");
    default:
        return QStringLiteral("#546e7a");
    }
}

QString slotStateSurface(SlotState state)
{
    switch (state) {
    case SlotState::Vacant:
        return QStringLiteral("#e8f5e9");
    case SlotState::Occupied:
        return QStringLiteral("#fff3e0");
    case SlotState::OvertimeAlert:
    case SlotState::NonEvAlert:
        return QStringLiteral("#ffebee");
    case SlotState::SensorError:
        return QStringLiteral("#f3e5f5");
    case SlotState::Acked:
        return QStringLiteral("#e3f2fd");
    default:
        return QStringLiteral("#f5f7f9");
    }
}

QString statePillStyle(SlotState state)
{
    return QStringLiteral(
               "QLabel { background:%1; color:%2; border:1px solid %2; "
               "border-radius:6px; padding:5px 8px; font-weight:900; }")
        .arg(slotStateSurface(state), slotStateAccent(state));
}

QFrame *createMetricCard(QWidget *parent,
                         const QString &title,
                         const QString &objectName,
                         QLabel **valueLabel)
{
    auto *card = new QFrame(parent);
    card->setObjectName(objectName + QStringLiteral("Card"));
    card->setStyleSheet(QStringLiteral(
        "QFrame { background:#f8fafb; border:1px solid #cfd8dc; border-radius:6px; }"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(2);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(QStringLiteral(
        "border:none; color:#607d8b; font-size:11px; font-weight:800;"));
    *valueLabel = new QLabel(QStringLiteral("-"), card);
    (*valueLabel)->setObjectName(objectName);
    (*valueLabel)->setMinimumWidth(80);
    (*valueLabel)->setWordWrap(true);
    (*valueLabel)->setStyleSheet(QStringLiteral(
        "border:none; color:#17212b; font-size:13px; font-weight:900;"));
    (*valueLabel)->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(titleLabel);
    layout->addWidget(*valueLabel);
    return card;
}

} // namespace

EvidencePage::EvidencePage(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(12);
    rootLayout->addLayout(createPageHeader(
        this, QStringLiteral("Evidence"),
        QStringLiteral("Operator review for session captures and event evidence")));

    auto *bodyLayout = new QHBoxLayout;
    bodyLayout->setSpacing(12);

    auto *leftSidebar = new QWidget(this);
    leftSidebar->setObjectName(QStringLiteral("evidenceLeftSidebar"));
    leftSidebar->setFixedWidth(245);
    auto *leftLayout = new QVBoxLayout(leftSidebar);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto *filterPanel = new QFrame(leftSidebar);
    filterPanel->setObjectName(QStringLiteral("evidenceFilterPanel"));
    filterPanel->setFixedWidth(245);
    filterPanel->setStyleSheet(QStringLiteral(
        "QFrame#evidenceFilterPanel { background:#f8fafb; border:1px solid #c7cdd4; "
        "border-radius:6px; }"));
    auto *filterLayout = new QVBoxLayout(filterPanel);
    filterLayout->setContentsMargins(12, 12, 12, 12);
    filterLayout->setSpacing(8);
    auto *filterTitle = new QLabel(QStringLiteral("Evidence filters"), filterPanel);
    filterTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:800;color:#263238;"));
    filterLayout->addWidget(filterTitle);
    auto *filterHint = new QLabel(
        QStringLiteral("Timeline is ordered by capture time. Slots narrow the result; they are not the navigation."),
        filterPanel);
    filterHint->setWordWrap(true);
    filterHint->setStyleSheet(QStringLiteral("color:#546e7a;font-size:12px;"));
    filterLayout->addWidget(filterHint);
    auto *slotLabel = new QLabel(QStringLiteral("Slot"), filterPanel);
    slotLabel->setStyleSheet(QStringLiteral("font-weight:800;color:#455a64;"));
    filterLayout->addWidget(slotLabel);
    m_slotFilter = new QComboBox(filterPanel);
    m_slotFilter->setObjectName(QStringLiteral("evidenceSlotFilter"));
    m_slotFilter->setFixedHeight(32);
    auto *plateLabel = new QLabel(QStringLiteral("Plate"), filterPanel);
    plateLabel->setStyleSheet(QStringLiteral("font-weight:800;color:#455a64;"));
    filterLayout->addWidget(m_slotFilter);
    filterLayout->addWidget(plateLabel);
    m_plateFilter = new QLineEdit(filterPanel);
    m_plateFilter->setObjectName(QStringLiteral("evidencePlateFilter"));
    m_plateFilter->setPlaceholderText(QStringLiteral("Any plate"));
    m_plateFilter->setFixedHeight(32);
    filterLayout->addWidget(m_plateFilter);
    auto *reasonLabel = new QLabel(QStringLiteral("Reason"), filterPanel);
    reasonLabel->setStyleSheet(QStringLiteral("font-weight:800;color:#455a64;"));
    filterLayout->addWidget(reasonLabel);
    m_reasonFilter = new QLineEdit(filterPanel);
    m_reasonFilter->setObjectName(QStringLiteral("evidenceReasonFilter"));
    m_reasonFilter->setPlaceholderText(QStringLiteral("Any reason"));
    m_reasonFilter->setFixedHeight(32);
    filterLayout->addWidget(m_reasonFilter);
    filterLayout->addStretch(1);
    auto *autoRefreshHint = new QLabel(
        QStringLiteral("Auto refresh · every 5 seconds while open"), filterPanel);
    autoRefreshHint->setObjectName(QStringLiteral("evidenceAutoRefreshHint"));
    autoRefreshHint->setWordWrap(true);
    autoRefreshHint->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:11px;font-weight:700;"));
    filterLayout->addWidget(autoRefreshHint);
    auto *refreshButton = new QPushButton(QStringLiteral("Refresh now"), filterPanel);
    refreshButton->setObjectName(QStringLiteral("evidenceRefreshButton"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setFixedHeight(34);
    refreshButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238; color:white; border:1px solid #455a64; "
        "border-radius:6px; padding:6px 10px; font-weight:800; }"
        "QPushButton:hover { background:#37474f; border-color:#fb8c00; }"
        "QPushButton:pressed { background:#1c252a; }"));
    filterLayout->addWidget(refreshButton);

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("evidenceContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    auto *summaryFrame = new QFrame(leftSidebar);
    summaryFrame->setObjectName(QStringLiteral("evidenceSummaryFrame"));
    summaryFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    summaryFrame->setStyleSheet(QStringLiteral(
        "QFrame { background:white; border:1px solid #c7cdd4; border-radius:6px; }"));
    auto *summaryLayout = new QVBoxLayout(summaryFrame);
    summaryLayout->setContentsMargins(14, 10, 14, 10);
    summaryLayout->setSpacing(8);
    auto *summaryTextLayout = new QVBoxLayout;
    m_summaryLabel = new QLabel(QStringLiteral("Evidence timeline"), summaryFrame);
    m_summaryLabel->setObjectName(QStringLiteral("evidenceSummaryLabel"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet(QStringLiteral(
        "border:none;font-size:15px;font-weight:800;color:#263238;"));
    m_statusLabel = new QLabel(
        QStringLiteral("Showing loaded evidence in capture-time order."), summaryFrame);
    m_statusLabel->setObjectName(QStringLiteral("evidenceStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("border:none;color:#546e7a;"));
    summaryTextLayout->addWidget(m_summaryLabel);
    summaryTextLayout->addWidget(m_statusLabel);
    summaryLayout->addLayout(summaryTextLayout);

    auto *metricGrid = new QGridLayout;
    metricGrid->setSpacing(8);
    metricGrid->addWidget(createMetricCard(summaryFrame, QStringLiteral("Slot"),
                                           QStringLiteral("evidenceSlotMetric"),
                                           &m_slotMetricLabel), 0, 0);
    metricGrid->addWidget(createMetricCard(summaryFrame, QStringLiteral("Plate"),
                                           QStringLiteral("evidencePlateMetric"),
                                           &m_plateMetricLabel), 0, 1);
    metricGrid->addWidget(createMetricCard(summaryFrame, QStringLiteral("Session"),
                                           QStringLiteral("evidenceSessionMetric"),
                                           &m_sessionMetricLabel), 1, 0);
    metricGrid->addWidget(createMetricCard(summaryFrame, QStringLiteral("Captures"),
                                           QStringLiteral("evidenceCaptureMetric"),
                                           &m_captureMetricLabel), 1, 1);
    summaryLayout->addLayout(metricGrid);
    auto *helpButton = createPageHelpButton(
        this, summaryFrame,
        {QStringLiteral("evidence"), QStringLiteral("Evidence"),
         QStringLiteral("Evidence 사용 안내"),
         QStringLiteral("주정차 증거를 확인하는 기본 흐름입니다."),
         QStringLiteral(
             "<b>1. 시간순 확인</b><br>가장 최근 캡처가 타임라인 위에 표시됩니다.<br><br>"
             "<b>2. 필터 적용</b><br>슬롯, 차량번호, 사유로 시간순 결과를 좁힙니다.<br><br>"
             "<b>3. 캡처 선택</b><br>촬영 시간, 사유, OCR 결과와 이미지 종류를 확인합니다.<br><br>"
             "<b>4. 원본 이미지 열기</b><br><i>Open full image</i>로 원본 크기 사진을 확인합니다."),
         QStringLiteral(
             "※ 사진이 표시되지 않으면 서버에 저장된 증거가 없거나 아직 이미지가 전달되지 않은 상태입니다.\n"
             "   촬영 사유는 서버 metadata가 제공될 때 표시됩니다.")});
    summaryLayout->addWidget(helpButton, 0, Qt::AlignRight);
    // Keep the compact summary above the filters in the fixed-width sidebar.
    // The capture content can now start at the top of the body and use the
    // vertical space previously consumed by the full-width summary row.
    leftLayout->addWidget(summaryFrame);
    leftLayout->addWidget(filterPanel, 1);
    bodyLayout->addWidget(leftSidebar);

    auto *comparisonLayout = new QHBoxLayout;
    comparisonLayout->setSpacing(10);
    auto createCaptureCard = [this](const QString &title,
                                    EvidenceImageLabel *&imageLabel,
                                    QLabel *&titleLabel,
                                    QLabel *&metadataLabel,
                                    QPushButton *&openButton) {
        auto *group = new QGroupBox(title, this);
        group->setMinimumWidth(0);
        group->setMaximumWidth(700);
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        group->setStyleSheet(QStringLiteral(
            "QGroupBox { background:white; border:1px solid #c7cdd4; border-radius:6px; "
            "margin-top:12px; font-weight:800; color:#263238; }"
            "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }"));
        auto *layout = new QVBoxLayout(group);
        layout->setContentsMargins(10, 12, 10, 10);
        layout->setSpacing(7);
        titleLabel = new QLabel(title, group);
        titleLabel->setVisible(false);
        imageLabel = new EvidenceImageLabel(group);
        metadataLabel = new QLabel(QStringLiteral("No capture loaded"), group);
        metadataLabel->setWordWrap(true);
        metadataLabel->setFixedHeight(42);
        metadataLabel->setStyleSheet(QStringLiteral(
            "color:#455a64; background:#f8fafb; border:1px solid #d6dde3; "
            "border-radius:5px; padding:6px;"));
        openButton = new QPushButton(QStringLiteral("Open full frame"), group);
        openButton->setEnabled(false);
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setStyleSheet(QStringLiteral(
            "QPushButton { background:#f5f7f9; color:#263238; border:1px solid #b0bec5; "
            "border-radius:5px; padding:5px 10px; font-weight:800; }"
            "QPushButton:hover:enabled { background:#e3f2fd; border-color:#1976d2; }"
            "QPushButton:disabled { color:#90a4ae; background:#eceff1; }"));
        openButton->setFixedHeight(34);
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
    m_firstMetadataLabel->setObjectName(QStringLiteral("evidenceFirstMetadata"));
    m_selectedMetadataLabel->setObjectName(QStringLiteral("evidenceSelectedMetadata"));
    m_firstImageLabel->setObjectName(QStringLiteral("evidenceFirstImage"));
    m_selectedImageLabel->setObjectName(QStringLiteral("evidenceSelectedImage"));
    m_firstOpenButton->setObjectName(QStringLiteral("evidenceFirstOpenButton"));
    m_selectedOpenButton->setObjectName(QStringLiteral("evidenceSelectedOpenButton"));
    comparisonLayout->addStretch(1);
    comparisonLayout->addWidget(firstGroup, 1);
    comparisonLayout->addWidget(selectedGroup, 1);
    comparisonLayout->addStretch(1);
    contentLayout->addLayout(comparisonLayout, 1);

    auto *timelineGroup = new QGroupBox(
        QStringLiteral("Capture timeline  ▼"), content);
    timelineGroup->setObjectName(QStringLiteral("evidenceTimelineGroup"));
    timelineGroup->setCheckable(true);
    timelineGroup->setChecked(true);
    timelineGroup->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Maximum);
    timelineGroup->setToolTip(
        QStringLiteral("Click the title to collapse or expand the capture timeline"));
    timelineGroup->setStyleSheet(QStringLiteral(
        "QGroupBox { background:white; border:1px solid #c7cdd4; border-radius:6px; "
        "margin-top:12px; font-weight:800; color:#263238; }"
        "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }"));
    auto *timelineLayout = new QVBoxLayout(timelineGroup);
    timelineLayout->setContentsMargins(10, 12, 10, 10);
    auto *timelineContent = new QWidget(timelineGroup);
    timelineContent->setObjectName(QStringLiteral("evidenceTimelineContent"));
    auto *timelineContentLayout = new QVBoxLayout(timelineContent);
    timelineContentLayout->setContentsMargins(0, 0, 0, 0);
    timelineContentLayout->setSpacing(7);
    auto *downloadLayout = new QHBoxLayout;
    m_downloadSelectionLabel = new QLabel(
        QStringLiteral("Select one or more timeline pairs"), timelineContent);
    m_downloadSelectionLabel->setObjectName(
        QStringLiteral("evidenceDownloadSelectionLabel"));
    m_downloadSelectionLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:11px;font-weight:700;"));
    m_downloadButton = new QPushButton(
        QStringLiteral("Download selected pairs"), timelineContent);
    m_downloadButton->setObjectName(
        QStringLiteral("evidenceDownloadSelectedButton"));
    m_downloadButton->setEnabled(false);
    m_downloadButton->setCursor(Qt::PointingHandCursor);
    m_downloadButton->setFixedHeight(32);
    m_downloadButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238;color:white;border:none;border-radius:5px;"
        "padding:5px 12px;font-weight:800; }"
        "QPushButton:hover:enabled { background:#1565c0; }"
        "QPushButton:disabled { background:#cfd8dc;color:#78909c; }"));
    downloadLayout->addWidget(m_downloadSelectionLabel);
    downloadLayout->addStretch(1);
    downloadLayout->addWidget(m_downloadButton);
    timelineContentLayout->addLayout(downloadLayout);

    m_captureTable = new QTableWidget(0, 7, timelineContent);
    m_captureTable->setObjectName(QStringLiteral("evidenceCaptureTable"));
    m_captureTable->setHorizontalHeaderLabels({
        QStringLiteral("Session"), QStringLiteral("Slot"),
        QStringLiteral("First capture"), QStringLiteral("Latest capture"),
        QStringLiteral("OCR"), QStringLiteral("Reason"),
        QStringLiteral("Available")});
    m_captureTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_captureTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_captureTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_captureTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_captureTable->verticalHeader()->setVisible(false);
    m_captureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_captureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_captureTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_captureTable->setMinimumHeight(300);
    m_captureTable->setMaximumHeight(420);
    m_captureTable->setAlternatingRowColors(true);
    m_captureTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background:white; alternate-background-color:#f8fafb; "
        "border:1px solid #d6dde3; gridline-color:#e5eaee; selection-background-color:#e3f2fd; "
        "selection-color:#17212b; }"
        "QHeaderView::section { background:#eceff1; color:#263238; border:none; "
        "border-right:1px solid #cfd8dc; padding:6px; font-weight:800; }"));
    timelineContentLayout->addWidget(m_captureTable);
    timelineLayout->addWidget(timelineContent, 1);
    contentLayout->addWidget(timelineGroup);
    connect(timelineGroup, &QGroupBox::toggled, this,
            [timelineGroup, timelineContent](bool expanded) {
                timelineContent->setVisible(expanded);
                timelineGroup->setTitle(
                    expanded ? QStringLiteral("Capture timeline  ▼")
                             : QStringLiteral("Capture timeline  ▶"));
                timelineGroup->setMaximumHeight(
                    expanded ? QWIDGETSIZE_MAX : 38);
            });
    bodyLayout->addWidget(content, 1);
    rootLayout->addLayout(bodyLayout, 1);

    updateSummaryMetrics(QString(), SlotState::Vacant, QString(), -1, -1, QString());

    connect(refreshButton, &QPushButton::clicked,
            this, &EvidencePage::requestCurrentEvidence);
    connect(m_slotFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { showLocalEvidenceSnapshot(m_latestState); });
    connect(m_plateFilter, &QLineEdit::textChanged, this,
            [this](const QString &) { showLocalEvidenceSnapshot(m_latestState); });
    connect(m_reasonFilter, &QLineEdit::textChanged, this,
            [this](const QString &) { showLocalEvidenceSnapshot(m_latestState); });
    connect(m_captureTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
                const QTableWidgetItem *captureItem =
                    m_captureTable->item(currentRow, 0);
                renderSelectedCapture(captureItem
                    ? captureItem->data(Qt::UserRole).toInt() : -1);
            });
    connect(m_captureTable, &QTableWidget::itemSelectionChanged,
            this, &EvidencePage::updateDownloadButtonState);
    connect(m_downloadButton, &QPushButton::clicked,
            this, &EvidencePage::chooseEvidenceDownloadDirectory);
    connect(m_firstOpenButton, &QPushButton::clicked, this,
            [this]() { showFullImage(m_firstImageLabel, m_firstTitleLabel->text()); });
    connect(m_selectedOpenButton, &QPushButton::clicked, this,
            [this]() { showFullImage(m_selectedImageLabel, m_selectedTitleLabel->text()); });

    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setObjectName(QStringLiteral("evidenceAutoRefreshTimer"));
    m_autoRefreshTimer->setInterval(5000);
    m_autoRefreshTimer->setTimerType(Qt::CoarseTimer);
    connect(m_autoRefreshTimer, &QTimer::timeout, this,
            [this]() { requestEvidenceRefresh(false); });
}

void EvidencePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_autoRefreshTimer && !m_autoRefreshTimer->isActive()) {
        m_autoRefreshTimer->start();
    }
}

void EvidencePage::hideEvent(QHideEvent *event)
{
    if (m_autoRefreshTimer) {
        m_autoRefreshTimer->stop();
    }
    QWidget::hideEvent(event);
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
    connect(m_imageLoader, &ImageLoader::imageDataLoaded,
            this, &EvidencePage::handleDownloadedImage);
    connect(m_imageLoader, &ImageLoader::imageDataFailed,
            this, &EvidencePage::handleDownloadFailure);
}

void EvidencePage::render(const ParkingViewState &state)
{
    const bool evidenceChanged = mergeEvidenceCache(state);
    m_currentSnapshotSlotIds.clear();
    for (auto it = state.evSlots.cbegin(); it != state.evSlots.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty()) {
            m_currentSnapshotSlotIds.insert(slotId);
        }
    }
    for (auto it = state.parkingSlots.cbegin(); it != state.parkingSlots.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty()) {
            m_currentSnapshotSlotIds.insert(slotId);
        }
    }
    m_latestState = m_evidenceCacheState;
    rebuildTimelineFilters(m_latestState);
    // State updates arrive for slot and alarm changes too. Rebuilding the
    // capture cards for each of those updates clears the in-flight image
    // request before it can complete. Keep the selected capture intact until
    // the local evidence cache itself changes.
    if (evidenceChanged || !m_localTimelineInitialized) {
        showLocalEvidenceSnapshot(m_latestState);
    }
}

bool EvidencePage::mergeEvidenceCache(const ParkingViewState &state)
{
    // A status-poll response is a current-state view, not an evidence-history
    // response. Keep the last non-empty image list observed for each slot
    // while the client is running, so a later poll that omits `images` cannot
    // make a capture disappear from this v0.1 local timeline.
    bool evidenceChanged = false;
    for (auto it = state.evSlots.cbegin(); it != state.evSlots.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty()) {
            m_evidenceCacheState.evSlots.insert(slotId, it.value());
        }
    }
    for (auto it = state.parkingSlots.cbegin(); it != state.parkingSlots.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty()) {
            m_evidenceCacheState.parkingSlots.insert(slotId, it.value());
        }
    }
    for (auto it = state.slotPlateNumbers.cbegin(); it != state.slotPlateNumbers.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty()) {
            m_evidenceCacheState.slotPlateNumbers.insert(slotId, it.value());
        }
    }
    for (auto it = state.slotImages.cbegin(); it != state.slotImages.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        if (!slotId.isEmpty() && !it.value().isEmpty()) {
            const QList<ParkingImageResource> cachedImages =
                m_evidenceCacheState.slotImages.value(slotId);
            if (!sameImageResources(cachedImages, it.value())) {
                m_evidenceCacheState.slotImages.insert(slotId, it.value());
                evidenceChanged = true;
            }
        }
    }
    m_evidenceCacheState.generatedAt = state.generatedAt;
    m_evidenceCacheState.apiEnabled = state.apiEnabled;
    return evidenceChanged;
}

void EvidencePage::rebuildTimelineFilters(const ParkingViewState &state)
{
    if (!m_slotFilter) {
        return;
    }

    QStringList slotIds;
    for (auto it = state.evSlots.cbegin(); it != state.evSlots.cend(); ++it) {
        slotIds.append(normalizeParkingSlotId(it.key()));
    }
    for (auto it = state.parkingSlots.cbegin(); it != state.parkingSlots.cend(); ++it) {
        slotIds.append(normalizeParkingSlotId(it.key()));
    }
    for (auto it = state.slotImages.cbegin(); it != state.slotImages.cend(); ++it) {
        slotIds.append(normalizeParkingSlotId(it.key()));
    }
    slotIds.removeAll(QString());
    std::sort(slotIds.begin(), slotIds.end(), [](const QString &left,
                                                  const QString &right) {
        const bool leftEv = left.startsWith(QStringLiteral("EV-"));
        const bool rightEv = right.startsWith(QStringLiteral("EV-"));
        if (leftEv != rightEv) {
            return leftEv;
        }
        return slotNumber(left) < slotNumber(right);
    });
    slotIds.removeDuplicates();

    const QString selectedSlotId = m_slotFilter->currentData().toString();
    QSignalBlocker blocker(m_slotFilter);
    m_slotFilter->clear();
    m_slotFilter->addItem(QStringLiteral("All slots"), QString());
    for (const QString &slotId : slotIds) {
        m_slotFilter->addItem(slotId, slotId);
    }
    const int selectedIndex = m_slotFilter->findData(selectedSlotId);
    m_slotFilter->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}

void EvidencePage::showLocalEvidenceSnapshot(const ParkingViewState &state)
{
    m_latestState = state;
    m_localTimelineInitialized = true;
    m_localTimelineMode = true;
    m_currentEventId.clear();
    m_currentSessionId = -1;
    const QString selectedSlotId = m_slotFilter
        ? m_slotFilter->currentData().toString() : QString();
    const QString plateNeedle = m_plateFilter
        ? m_plateFilter->text().trimmed() : QString();
    const QString reasonNeedle = m_reasonFilter
        ? m_reasonFilter->text().trimmed() : QString();
    m_currentSlotId = selectedSlotId;
    m_plateNumber.clear();
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures.clear();
    m_localTimelineEntries.clear();
    m_allLocalTimelineEntries.clear();
    m_localTimelinePairs.clear();

    for (auto it = state.slotImages.cbegin(); it != state.slotImages.cend(); ++it) {
        const QString slotId = normalizeParkingSlotId(it.key());
        const QVector<ParkingCaptureGroup> groups = buildParkingCaptureGroups(it.value());
        m_slotCaptureCounts.insert(slotId, groups.size());
        for (const ParkingCaptureGroup &capture : groups) {
            LocalTimelineEntry entry;
            entry.slotId = slotId;
            entry.plateNumber = plateForSlot(state, slotId);
            entry.state = stateForSlot(state, slotId);
            entry.capture = capture;
            m_allLocalTimelineEntries.append(entry);
            if (!selectedSlotId.isEmpty() && entry.slotId != selectedSlotId) {
                continue;
            }
            if (!plateNeedle.isEmpty()
                && !entry.plateNumber.contains(plateNeedle, Qt::CaseInsensitive)
                && !entry.capture.ocrResult.contains(plateNeedle,
                                                     Qt::CaseInsensitive)) {
                continue;
            }
            if (!reasonNeedle.isEmpty()
                && !captureReasonText(entry.capture.reason).contains(
                    reasonNeedle, Qt::CaseInsensitive)) {
                continue;
            }
            m_localTimelineEntries.append(entry);
        }
    }

    const auto timelineLess = [](const LocalTimelineEntry &left,
                                 const LocalTimelineEntry &right) {
        if (left.capture.timestamp.isValid() != right.capture.timestamp.isValid()) {
            // The table is rendered in reverse order (newest first), so keep
            // timestamp-less legacy records at the beginning of this backing
            // list and therefore at the bottom of the visible timeline.
            return !left.capture.timestamp.isValid();
        }
        if (left.capture.timestamp.isValid()
            && left.capture.timestamp != right.capture.timestamp) {
            return left.capture.timestamp < right.capture.timestamp;
        }
        if (left.slotId != right.slotId) {
            return left.slotId < right.slotId;
        }
        return left.capture.imageId < right.capture.imageId;
    };
    std::stable_sort(m_allLocalTimelineEntries.begin(),
                     m_allLocalTimelineEntries.end(), timelineLess);
    std::stable_sort(m_localTimelineEntries.begin(),
                     m_localTimelineEntries.end(), timelineLess);
    m_captures.reserve(m_localTimelineEntries.size());
    for (const LocalTimelineEntry &entry : m_localTimelineEntries) {
        m_captures.append(entry.capture);
    }

    QSet<QString> visiblePairKeys;
    for (const LocalTimelineEntry &entry : m_localTimelineEntries) {
        visiblePairKeys.insert(localTimelinePairKey(entry.slotId,
                                                    entry.capture));
    }
    QHash<QString, int> pairIndexes;
    for (int entryIndex = 0;
         entryIndex < m_allLocalTimelineEntries.size(); ++entryIndex) {
        const LocalTimelineEntry &entry =
            m_allLocalTimelineEntries.at(entryIndex);
        const QString key = localTimelinePairKey(entry.slotId,
                                                 entry.capture);
        if (!visiblePairKeys.contains(key)) {
            continue;
        }
        int pairIndex = pairIndexes.value(key, -1);
        if (pairIndex < 0) {
            LocalTimelinePair pair;
            pair.key = key;
            pair.slotId = entry.slotId;
            pair.plateNumber = entry.plateNumber;
            pair.state = entry.state;
            pair.sessionId = entry.capture.sessionId;
            pair.firstEntryIndex = entryIndex;
            pairIndex = m_localTimelinePairs.size();
            m_localTimelinePairs.append(pair);
            pairIndexes.insert(key, pairIndex);
        }
        LocalTimelinePair &pair = m_localTimelinePairs[pairIndex];
        pair.latestEntryIndex = entryIndex;
        ++pair.captureCount;
    }
    std::stable_sort(
        m_localTimelinePairs.begin(), m_localTimelinePairs.end(),
        [this](const LocalTimelinePair &left,
               const LocalTimelinePair &right) {
        const LocalTimelineEntry &leftLatest =
            m_allLocalTimelineEntries.at(left.latestEntryIndex);
        const LocalTimelineEntry &rightLatest =
            m_allLocalTimelineEntries.at(right.latestEntryIndex);
        if (leftLatest.capture.timestamp.isValid()
            != rightLatest.capture.timestamp.isValid()) {
            return !leftLatest.capture.timestamp.isValid();
        }
        if (leftLatest.capture.timestamp.isValid()
            && leftLatest.capture.timestamp
                != rightLatest.capture.timestamp) {
            return leftLatest.capture.timestamp
                < rightLatest.capture.timestamp;
        }
        if (left.slotId != right.slotId) {
            return left.slotId < right.slotId;
        }
        return left.sessionId < right.sessionId;
    });

    m_summaryLabel->setText(QStringLiteral("Qt local evidence timeline"));
    m_summaryLabel->setStyleSheet(statePillStyle(SlotState::Acked));
    m_statusLabel->setText(m_captures.isEmpty()
        ? QStringLiteral("No loaded evidence matches the current filters.")
        : QStringLiteral("%1 session pair%2 from %3 cached capture groups, ordered by latest captured time (newest first). Historical sessions are not fetched in v0.1.")
              .arg(m_localTimelinePairs.size())
              .arg(m_localTimelinePairs.size() == 1
                       ? QString() : QStringLiteral("s"))
              .arg(m_captures.size()));
    updateSummaryMetrics(selectedSlotId.isEmpty()
                             ? QStringLiteral("All slots") : selectedSlotId,
                         selectedSlotId.isEmpty()
                             ? SlotState::Acked
                             : stateForSlot(state, selectedSlotId),
                         selectedSlotId.isEmpty()
                             ? QString() : plateForSlot(state, selectedSlotId),
                         -1, m_captures.size(),
                         QStringLiteral("qt-local-cache"));
    renderCaptureTable();
    renderSelectedCapture(m_localTimelinePairs.isEmpty()
                              ? -1 : m_localTimelinePairs.size() - 1);
}

void EvidencePage::showEvidence(
    const QString &slotId,
    SlotState state,
    const QString &plateNumber,
    const QList<ParkingImageResource> &images)
{
    const QString normalizedSlotId = normalizeParkingSlotId(slotId);
    if (normalizedSlotId.isEmpty()) {
        return;
    }
    const bool hadSlot = m_evidenceCacheState.evSlots.contains(normalizedSlotId)
        || m_evidenceCacheState.parkingSlots.contains(normalizedSlotId);
    const SlotState previousState = stateForSlot(
        m_evidenceCacheState, normalizedSlotId);
    const QString previousPlate = plateForSlot(
        m_evidenceCacheState, normalizedSlotId);
    bool evidenceChanged = false;
    if (!images.isEmpty()) {
        evidenceChanged = !sameImageResources(
            m_evidenceCacheState.slotImages.value(normalizedSlotId), images);
        if (evidenceChanged) {
            m_evidenceCacheState.slotImages.insert(normalizedSlotId, images);
        }
    }
    m_evidenceCacheState.slotPlateNumbers.insert(normalizedSlotId, plateNumber);
    if (normalizedSlotId.startsWith(QStringLiteral("EV-"))) {
        EvSlotInfo slot = m_evidenceCacheState.evSlots.value(normalizedSlotId);
        slot.slotId = normalizedSlotId;
        slot.state = state;
        slot.plateNumber = plateNumber;
        m_evidenceCacheState.evSlots.insert(normalizedSlotId, slot);
    } else {
        ParkingSlotInfo slot = m_evidenceCacheState.parkingSlots.value(normalizedSlotId);
        slot.slotId = normalizedSlotId;
        slot.state = state;
        m_evidenceCacheState.parkingSlots.insert(normalizedSlotId, slot);
    }
    m_latestState = m_evidenceCacheState;
    rebuildTimelineFilters(m_latestState);
    const bool slotContextChanged = !hadSlot
        || previousState != state
        || previousPlate != plateNumber;
    if (evidenceChanged || slotContextChanged
        || !m_localTimelineInitialized || m_captures.isEmpty()) {
        showLocalEvidenceSnapshot(m_latestState);
    }
}

void EvidencePage::showLoading(const QString &slotId)
{
    if (!slotId.isEmpty() && slotId != m_currentSlotId) {
        return;
    }
    resetLocalTimeline();
    m_currentEventId.clear();
    m_currentSessionId = -1;
    ++m_requestGeneration;
    m_requestTargets.clear();
    m_captures.clear();
    m_localTimelinePairs.clear();
    m_captureTable->setRowCount(0);
    m_summaryLabel->setText(QStringLiteral("Loading parking evidence"));
    m_summaryLabel->setStyleSheet(statePillStyle(SlotState::Vacant));
    m_statusLabel->setText(QStringLiteral("Loading active session and evidence images..."));
    updateSummaryMetrics(slotId, SlotState::Vacant, QString(), -1, -1, QString());
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
    updateSummaryMetrics(slotId, SlotState::SensorError, m_plateNumber, -1,
                         m_captures.size(), QString());
    clearCaptureCard(m_firstImageLabel, m_firstTitleLabel, m_firstMetadataLabel,
                     m_firstOpenButton, QStringLiteral("First capture"),
                     QStringLiteral("Evidence request failed"));
    clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                     m_selectedMetadataLabel, m_selectedOpenButton,
                     QStringLiteral("Latest capture"),
                     QStringLiteral("Evidence request failed"));
}

void EvidencePage::openEvent(const QString &eventId, const QString &rawSlotId)
{
    const QString slotId = normalizeParkingSlotId(rawSlotId);
    if (m_slotFilter) {
        const int index = m_slotFilter->findData(slotId);
        if (index >= 0) {
            m_slotFilter->setCurrentIndex(index);
        }
    }
    m_currentEventId = eventId.trimmed();
    m_currentSlotId = slotId;
    m_currentSessionId = -1;
    m_statusLabel->setText(
        QStringLiteral("Resolving event evidence into the time-ordered timeline..."));
}

void EvidencePage::showEventEvidence(
    const QString &eventId,
    const QString &slotId,
    qint64 sessionId,
    SlotState state,
    const QString &plateNumber,
    const QList<ParkingImageResource> &images)
{
    if (eventId != m_currentEventId) {
        return;
    }
    QList<ParkingImageResource> sessionImages = images;
    if (sessionId > 0) {
        for (ParkingImageResource &image : sessionImages) {
            if (image.sessionId <= 0) {
                image.sessionId = sessionId;
            }
        }
    }
    m_currentSessionId = sessionId;
    showEvidence(slotId, state, plateNumber, sessionImages);
    m_currentEventId = eventId;
}

void EvidencePage::showEventError(const QString &eventId,
                                  const QString &slotId,
                                  const QString &message)
{
    if (eventId != m_currentEventId) {
        return;
    }
    m_summaryLabel->setText(
        QStringLiteral("Event evidence unavailable"));
    m_summaryLabel->setStyleSheet(statePillStyle(SlotState::SensorError));
    m_statusLabel->setText(
        QStringLiteral("Could not load event evidence: %1").arg(message));
    updateSummaryMetrics(slotId, SlotState::SensorError, m_plateNumber,
                         m_currentSessionId, m_captures.size(), eventId);
    clearCaptureCard(m_firstImageLabel, m_firstTitleLabel,
                     m_firstMetadataLabel, m_firstOpenButton,
                     QStringLiteral("First capture"),
                     QStringLiteral("Event evidence request failed"));
    clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                     m_selectedMetadataLabel, m_selectedOpenButton,
                     QStringLiteral("Latest capture"),
                     QStringLiteral("Event evidence request failed"));
}

bool EvidencePage::selectSlot(const QString &rawSlotId)
{
    const QString slotId = normalizeParkingSlotId(rawSlotId);
    if (slotId.isEmpty() || !m_slotFilter) {
        return false;
    }
    const int index = m_slotFilter->findData(slotId);
    if (index < 0) {
        return false;
    }
    m_currentEventId.clear();
    m_currentSessionId = -1;
    m_slotFilter->setCurrentIndex(index);
    showLocalEvidenceSnapshot(m_latestState);
    return true;
}

QString EvidencePage::currentSlotId() const
{
    return m_currentSlotId;
}

QString EvidencePage::currentEventId() const
{
    return m_currentEventId;
}

int EvidencePage::captureCount() const
{
    return m_captures.size();
}

bool EvidencePage::downloadSelectedPairsTo(const QString &directoryPath)
{
    if (m_downloadInProgress) {
        m_statusLabel->setText(
            QStringLiteral("An evidence download is already in progress."));
        return false;
    }
    if (!m_imageLoader || !m_captureTable
        || !m_captureTable->selectionModel()) {
        m_statusLabel->setText(
            QStringLiteral("Image download is not available."));
        return false;
    }

    const QModelIndexList selectedRows =
        m_captureTable->selectionModel()->selectedRows(0);
    if (selectedRows.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("Select one or more timeline pairs to download."));
        return false;
    }

    QVector<LocalTimelinePair> selectedPairs;
    QSet<QString> pairKeys;
    for (const QModelIndex &selectedRow : selectedRows) {
        const QTableWidgetItem *item = m_captureTable->item(
            selectedRow.row(), 0);
        if (!item) {
            continue;
        }
        const int pairIndex = item->data(Qt::UserRole).toInt();
        if (pairIndex < 0
            || pairIndex >= m_localTimelinePairs.size()) {
            continue;
        }
        const LocalTimelinePair &pair = m_localTimelinePairs.at(pairIndex);
        if (pair.sessionId <= 0) {
            m_statusLabel->setText(QStringLiteral(
                "Every selected row needs a session ID before it can be downloaded."));
            return false;
        }
        if (!pairKeys.contains(pair.key)) {
            pairKeys.insert(pair.key);
            selectedPairs.append(pair);
        }
    }
    if (selectedPairs.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("No downloadable session pair was selected."));
        return false;
    }

    QVector<EvidenceDownloadItem> downloads;
    for (const LocalTimelinePair &selectedPair : selectedPairs) {
        if (selectedPair.firstEntryIndex < 0
            || selectedPair.firstEntryIndex
                >= m_allLocalTimelineEntries.size()
            || selectedPair.latestEntryIndex < 0
            || selectedPair.latestEntryIndex
                >= m_allLocalTimelineEntries.size()) {
            m_statusLabel->setText(
                QStringLiteral("A selected session pair is no longer available."));
            return false;
        }
        const LocalTimelineEntry &firstEntry =
            m_allLocalTimelineEntries.at(selectedPair.firstEntryIndex);
        const LocalTimelineEntry &latestEntry =
            m_allLocalTimelineEntries.at(selectedPair.latestEntryIndex);

        const auto appendDownload =
            [&downloads](const LocalTimelineEntry &entry,
                         const QString &position) -> bool {
            const ParkingImageResource *variant =
                preferredParkingCaptureVariant(entry.capture);
            if (!variant || variant->url.isEmpty()) {
                return false;
            }
            EvidenceDownloadItem item;
            item.slotId = entry.slotId;
            item.sessionId = entry.capture.sessionId;
            item.position = position;
            item.captureId = entry.capture.imageId;
            item.capturedAt = entry.capture.timestamp;
            item.ocr = entry.capture.ocrResult.trimmed();
            item.plateNumber = entry.plateNumber.trimmed();
            if (item.ocr.isEmpty()) {
                item.ocr = QStringLiteral("unconfirmed");
            }
            item.reason = captureReasonText(entry.capture.reason);
            item.sourceUrl = variant->url;
            const QString timeToken = item.capturedAt.isValid()
                ? item.capturedAt.toLocalTime().toString(
                      QStringLiteral("yyyyMMdd_HHmmss"))
                : QStringLiteral("time-unknown");
            item.fileStem = QStringLiteral(
                "%1_session-%2_%3_time-%4_ocr-%5_reason-%6")
                    .arg(safeFileToken(item.slotId, QStringLiteral("slot")))
                    .arg(item.sessionId)
                    .arg(position.toLower())
                    .arg(timeToken)
                    .arg(safeFileToken(item.ocr,
                                       QStringLiteral("unconfirmed")))
                    .arg(safeFileToken(item.reason,
                                       QStringLiteral("reason-unknown")));
            downloads.append(item);
            return true;
        };
        if (!appendDownload(firstEntry, QStringLiteral("FIRST"))
            || !appendDownload(latestEntry, QStringLiteral("LATEST"))) {
            m_statusLabel->setText(QStringLiteral(
                "Each selected session needs downloadable First and Latest image URLs."));
            return false;
        }
    }

    QDir parentDirectory(directoryPath);
    if (!parentDirectory.exists()) {
        m_statusLabel->setText(
            QStringLiteral("The selected download directory does not exist."));
        return false;
    }
    m_downloadDirectory = parentDirectory.absolutePath();
    m_downloadExportId = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    m_downloadManifestRows = {QStringLiteral(
        "slot_id,session_id,position,capture_id,captured_at,ocr,plate_number,reason,"
        "image_file,source_url,status,error")};
    m_downloadedFiles.clear();
    m_downloadFailureCount = 0;
    m_pendingDownloads.clear();
    for (EvidenceDownloadItem &download : downloads) {
        download.requestId = QStringLiteral("evidence-download:%1:%2")
            .arg(reinterpret_cast<quintptr>(this))
            .arg(++m_requestSequence);
        m_pendingDownloads.insert(download.requestId, download);
    }

    m_downloadInProgress = true;
    updateDownloadButtonState();
    m_statusLabel->setText(
        QStringLiteral("Downloading %1 First/Latest evidence image%2...")
            .arg(downloads.size())
            .arg(downloads.size() == 1
                     ? QString() : QStringLiteral("s")));
    for (const EvidenceDownloadItem &download : downloads) {
        m_imageLoader->download(download.requestId, download.sourceUrl);
    }
    return true;
}

void EvidencePage::chooseEvidenceDownloadDirectory()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Download selected evidence pairs"),
        QDir::homePath(), QFileDialog::ShowDirsOnly);
    if (!directoryPath.isEmpty()) {
        downloadSelectedPairsTo(directoryPath);
    }
}

void EvidencePage::updateDownloadButtonState()
{
    const QModelIndexList selectedRows =
        m_captureTable && m_captureTable->selectionModel()
            ? m_captureTable->selectionModel()->selectedRows(0)
            : QModelIndexList{};
    const int selectedCount = selectedRows.size();
    if (m_downloadButton) {
        m_downloadButton->setEnabled(
            !m_downloadInProgress && selectedCount > 0);
    }
    if (m_downloadSelectionLabel) {
        if (m_downloadInProgress) {
            m_downloadSelectionLabel->setText(
                QStringLiteral("Downloading selected First/Latest pairs..."));
        } else if (selectedCount > 0) {
            m_downloadSelectionLabel->setText(
                QStringLiteral("%1 pair%2 selected")
                    .arg(selectedCount)
                    .arg(selectedCount == 1
                             ? QString() : QStringLiteral("s")));
        } else {
            m_downloadSelectionLabel->setText(
                QStringLiteral("Select one or more timeline pairs"));
        }
    }
}

void EvidencePage::handleDownloadedImage(const QString &requestId,
                                         const QByteArray &data,
                                         const QString &contentType)
{
    if (!m_pendingDownloads.contains(requestId)) {
        return;
    }
    const EvidenceDownloadItem item = m_pendingDownloads.take(requestId);
    const QString extension = downloadedImageExtension(data, contentType);
    QString fileName;
    QString error;
    if (extension.isEmpty()) {
        error = QStringLiteral("Downloaded content is not a supported image");
    } else {
        fileName = QStringLiteral("evidence_%1_%2.%3")
                       .arg(m_downloadExportId, item.fileStem, extension);
        QFile file(QDir(m_downloadDirectory).filePath(fileName));
        if (!file.open(QIODevice::WriteOnly)
            || file.write(data) != data.size()) {
            error = QStringLiteral("Could not write the image file");
            file.close();
            QFile::remove(file.fileName());
            fileName.clear();
        } else {
            file.close();
            m_downloadedFiles.append(file.fileName());
        }
    }

    if (!error.isEmpty()) {
        ++m_downloadFailureCount;
    }
    const QString capturedAt = item.capturedAt.isValid()
        ? item.capturedAt.toString(Qt::ISODateWithMs) : QString();
    m_downloadManifestRows.append(QStringList{
        csvField(item.slotId),
        csvField(QString::number(item.sessionId)),
        csvField(item.position),
        csvField(item.captureId >= 0
                     ? QString::number(item.captureId) : QString()),
        csvField(capturedAt),
        csvField(item.ocr),
        csvField(item.plateNumber),
        csvField(item.reason),
        csvField(fileName),
        csvField(item.sourceUrl.toString()),
        csvField(error.isEmpty() ? QStringLiteral("OK")
                                 : QStringLiteral("ERROR")),
        csvField(error)}.join(QLatin1Char(',')));
    if (m_pendingDownloads.isEmpty()) {
        finishEvidenceDownload();
    }
}

void EvidencePage::handleDownloadFailure(const QString &requestId,
                                         const QString &message)
{
    if (!m_pendingDownloads.contains(requestId)) {
        return;
    }
    const EvidenceDownloadItem item = m_pendingDownloads.take(requestId);
    ++m_downloadFailureCount;
    const QString capturedAt = item.capturedAt.isValid()
        ? item.capturedAt.toString(Qt::ISODateWithMs) : QString();
    m_downloadManifestRows.append(QStringList{
        csvField(item.slotId),
        csvField(QString::number(item.sessionId)),
        csvField(item.position),
        csvField(item.captureId >= 0
                     ? QString::number(item.captureId) : QString()),
        csvField(capturedAt),
        csvField(item.ocr),
        csvField(item.plateNumber),
        csvField(item.reason),
        csvField(QString()),
        csvField(item.sourceUrl.toString()),
        csvField(QStringLiteral("ERROR")),
        csvField(message)}.join(QLatin1Char(',')));
    if (m_pendingDownloads.isEmpty()) {
        finishEvidenceDownload();
    }
}

void EvidencePage::finishEvidenceDownload()
{
    const QString metadataPath = QDir(m_downloadDirectory).filePath(
        QStringLiteral("evidence_metadata_%1.csv").arg(m_downloadExportId));
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ++m_downloadFailureCount;
    } else {
        const QByteArray metadata = QByteArrayLiteral("\xEF\xBB\xBF")
            + m_downloadManifestRows.join(QStringLiteral("\r\n")).toUtf8()
            + QByteArrayLiteral("\r\n");
        if (metadataFile.write(metadata) != metadata.size()) {
            ++m_downloadFailureCount;
        } else {
            m_downloadedFiles.append(metadataPath);
        }
        metadataFile.close();
    }

    const bool success = m_downloadFailureCount == 0;
    const QString message = success
        ? QStringLiteral("Downloaded %1 evidence images and metadata to %2")
              .arg(qMax(0, m_downloadedFiles.size() - 1))
              .arg(QDir::toNativeSeparators(m_downloadDirectory))
        : QStringLiteral("Evidence download finished with %1 error%2. See the metadata CSV in %3")
              .arg(m_downloadFailureCount)
              .arg(m_downloadFailureCount == 1
                       ? QString() : QStringLiteral("s"))
              .arg(QDir::toNativeSeparators(m_downloadDirectory));
    m_downloadInProgress = false;
    updateDownloadButtonState();
    m_statusLabel->setText(message);
    emit evidenceDownloadFinished(success, message, m_downloadedFiles);
    m_pendingDownloads.clear();
    m_downloadManifestRows.clear();
}

void EvidencePage::requestCurrentEvidence()
{
    requestEvidenceRefresh(true);
}

void EvidencePage::requestEvidenceRefresh(bool showInitialProgress)
{
    if (!m_currentEventId.isEmpty()) {
        if (showInitialProgress && m_captures.isEmpty()) {
            m_statusLabel->setText(
                QStringLiteral("Loading event evidence..."));
        }
        emit eventEvidenceRequested(m_currentEventId);
        return;
    }

    if (!m_localTimelineInitialized) {
        showLocalEvidenceSnapshot(m_latestState);
    }

    const QString selectedSlotId = m_slotFilter
        ? m_slotFilter->currentData().toString() : QString();
    QSet<QString> requestedSlotIds;
    if (!selectedSlotId.isEmpty()) {
        requestedSlotIds.insert(selectedSlotId);
    } else {
        requestedSlotIds = m_currentSnapshotSlotIds;
    }
    if (requestedSlotIds.isEmpty()) {
        return;
    }

    // The status endpoint is allowed to omit image resources. Populate this
    // timeline from the existing per-slot detail/current-session flow instead
    // of depending on Image Compare to have requested those images first.
    if (showInitialProgress && m_captures.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("Loading current-session evidence for %1 slot%2...")
                .arg(requestedSlotIds.size())
                .arg(requestedSlotIds.size() == 1
                         ? QString() : QStringLiteral("s")));
    }
    for (const QString &slotId : requestedSlotIds) {
        emit slotEvidenceRequested(slotId);
    }
}

void EvidencePage::renderCaptureTable()
{
    QSignalBlocker blocker(m_captureTable);
    m_captureTable->setRowCount(m_localTimelinePairs.size());
    for (int row = 0; row < m_localTimelinePairs.size(); ++row) {
        const int pairIndex = m_localTimelinePairs.size() - 1 - row;
        const LocalTimelinePair &pair = m_localTimelinePairs.at(pairIndex);
        if (pair.firstEntryIndex < 0
            || pair.firstEntryIndex >= m_allLocalTimelineEntries.size()
            || pair.latestEntryIndex < 0
            || pair.latestEntryIndex >= m_allLocalTimelineEntries.size()) {
            continue;
        }
        const ParkingCaptureGroup &first =
            m_allLocalTimelineEntries.at(pair.firstEntryIndex).capture;
        const ParkingCaptureGroup &latest =
            m_allLocalTimelineEntries.at(pair.latestEntryIndex).capture;
        const QStringList values = {
            pair.sessionId > 0
                ? QStringLiteral("Session %1").arg(pair.sessionId)
                : QStringLiteral("Unavailable"),
            pair.slotId.isEmpty() ? QStringLiteral("-") : pair.slotId,
            capturePairCellText(first),
            capturePairCellText(latest),
            pairValueText(first.ocrResult, latest.ocrResult),
            pairValueText(captureReasonText(first.reason),
                          captureReasonText(latest.reason)),
            pairAvailabilityText(first, latest)};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            if (column == 0) {
                item->setData(Qt::UserRole, pairIndex);
            }
            m_captureTable->setItem(row, column, item);
        }
    }
    if (!m_localTimelinePairs.isEmpty()) {
        m_captureTable->setCurrentCell(0, 0);
    }
    updateDownloadButtonState();
}

void EvidencePage::renderSelectedCapture(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= m_localTimelinePairs.size()) {
        clearCaptureCard(m_firstImageLabel, m_firstTitleLabel,
                         m_firstMetadataLabel, m_firstOpenButton,
                         QStringLiteral("First capture"),
                         QStringLiteral("Select a pair from the timeline"));
        clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                         m_selectedMetadataLabel, m_selectedOpenButton,
                         QStringLiteral("Latest capture"),
                         m_localTimelinePairs.isEmpty()
                              ? QStringLiteral("No capture pair available")
                              : QStringLiteral("Select a pair from the timeline"));
        return;
    }

    const LocalTimelinePair &pair = m_localTimelinePairs.at(pairIndex);
    if (pair.firstEntryIndex < 0
        || pair.firstEntryIndex >= m_allLocalTimelineEntries.size()
        || pair.latestEntryIndex < 0
        || pair.latestEntryIndex >= m_allLocalTimelineEntries.size()) {
        return;
    }
    if (pair.sessionId <= 0) {
        updateSummaryMetrics(pair.slotId, pair.state, pair.plateNumber,
                             -1, 0, QString());
        if (m_sessionMetricLabel) {
            m_sessionMetricLabel->setText(QStringLiteral("Session unavailable"));
        }
        clearCaptureCard(m_firstImageLabel, m_firstTitleLabel,
                         m_firstMetadataLabel, m_firstOpenButton,
                         QStringLiteral("First capture"),
                         QStringLiteral("Session ID is required to form a capture pair"));
        clearCaptureCard(m_selectedImageLabel, m_selectedTitleLabel,
                         m_selectedMetadataLabel, m_selectedOpenButton,
                         QStringLiteral("Latest capture"),
                         QStringLiteral("Session ID is required to form a capture pair"));
        return;
    }

    const ParkingCaptureGroup *firstCapture =
        &m_allLocalTimelineEntries.at(pair.firstEntryIndex).capture;
    const ParkingCaptureGroup *latestCapture =
        &m_allLocalTimelineEntries.at(pair.latestEntryIndex).capture;
    const QString originalPlate = m_plateNumber;
    m_plateNumber = pair.plateNumber;
    const QString pairSuffix = QStringLiteral(" · %1 · Session %2")
        .arg(pair.slotId)
        .arg(pair.sessionId);
    renderCaptureCard(firstCapture,
                      QStringLiteral("First capture%1").arg(pairSuffix),
                      m_firstImageLabel,
                      m_firstTitleLabel, m_firstMetadataLabel,
                      m_firstOpenButton, QStringLiteral("first"));
    renderCaptureCard(latestCapture,
                      QStringLiteral("Latest capture%1").arg(pairSuffix),
                      m_selectedImageLabel,
                      m_selectedTitleLabel, m_selectedMetadataLabel,
                      m_selectedOpenButton, QStringLiteral("selected"));
    m_plateNumber = originalPlate;
    updateSummaryMetrics(pair.slotId, pair.state, pair.plateNumber,
                         pair.sessionId, pair.captureCount, QString());
}

SlotState EvidencePage::stateForSlot(const ParkingViewState &state,
                                     const QString &slotId) const
{
    if (state.evSlots.contains(slotId)) {
        return state.evSlots.value(slotId).state;
    }
    if (state.parkingSlots.contains(slotId)) {
        return state.parkingSlots.value(slotId).state;
    }
    return SlotState::Vacant;
}

QString EvidencePage::plateForSlot(const ParkingViewState &state,
                                   const QString &slotId) const
{
    if (state.evSlots.contains(slotId)) {
        const QString plate = state.evSlots.value(slotId).plateNumber.trimmed();
        if (!plate.isEmpty()) {
            return plate;
        }
    }
    return state.slotPlateNumbers.value(slotId);
}

void EvidencePage::resetLocalTimeline()
{
    m_localTimelineMode = true;
    m_localTimelineEntries.clear();
    m_allLocalTimelineEntries.clear();
    m_localTimelinePairs.clear();
}

void EvidencePage::updateSlotCaptureCount(const QString &slotId, int captureCount)
{
    const QString normalizedSlotId = normalizeParkingSlotId(slotId);
    if (normalizedSlotId.isEmpty()) {
        return;
    }

    m_slotCaptureCounts.insert(normalizedSlotId, captureCount);
}

void EvidencePage::updateSummaryMetrics(const QString &slotId,
                                        SlotState state,
                                        const QString &plateNumber,
                                        qint64 sessionId,
                                        int captureCount,
                                        const QString &eventId)
{
    if (m_slotMetricLabel) {
        m_slotMetricLabel->setText(
            slotId.isEmpty()
                ? QStringLiteral("-")
                : QStringLiteral("%1 · %2").arg(slotId, slotStateText(state)));
        m_slotMetricLabel->setStyleSheet(QStringLiteral(
            "border:none; color:%1; font-size:16px; font-weight:900;")
                                             .arg(slotStateAccent(state)));
    }
    if (m_plateMetricLabel) {
        const QString cleanPlate = plateNumber.trimmed();
        m_plateMetricLabel->setText(
            cleanPlate.isEmpty() || cleanPlate == QStringLiteral("-")
                ? QStringLiteral("Unconfirmed")
                : cleanPlate);
    }
    if (m_sessionMetricLabel) {
        QString sessionText;
        if (sessionId > 0) {
            sessionText = QStringLiteral("Session %1").arg(sessionId);
        } else if (eventId == QStringLiteral("qt-local-cache")) {
            sessionText = QStringLiteral("Qt cache");
        } else if (!eventId.trimmed().isEmpty()) {
            sessionText = QStringLiteral("Event linked");
        } else {
            sessionText = QStringLiteral("Active slot");
        }
        m_sessionMetricLabel->setText(sessionText);
    }
    if (m_captureMetricLabel) {
        m_captureMetricLabel->setText(
            captureCount < 0 ? QStringLiteral("Loading") : captureCountText(captureCount));
    }
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
    titleLabel->setText(heading);
    // OCR 은 HALL_30S 캡처 한 장에만 돌아서 다른 증거 이미지 행에는
    // ocr_result 가 없다. 그 경우 세션에서 확정된 번호판으로 폴백하되,
    // 이 이미지에서 직접 읽은 값이 아님을 (session) 으로 구분한다.
    QString ocrText = capture->ocrResult;
    if (ocrText.isEmpty()) {
        ocrText = (m_plateNumber.isEmpty() || m_plateNumber == QStringLiteral("-"))
            ? QStringLiteral("-")
            : QStringLiteral("%1 (session)").arg(m_plateNumber);
    }
    QString metadata = QStringLiteral("%1")
        .arg(captureTimeText(capture->timestamp));
    if (variant && !variant->processing.isEmpty()) {
        metadata += QStringLiteral("  |  %1").arg(variant->processing);
    }
    if (ocrText != QStringLiteral("-")) {
        metadata += QStringLiteral("  |  OCR available");
    }
    metadataLabel->setText(metadata);

    if (!variant || variant->url.isEmpty()) {
        imageLabel->setSourcePixmap(QPixmap());
        imageLabel->setProperty("evidenceRequestId", QString());
        imageLabel->setProperty("evidenceSourceUrl", QString());
        openButton->setEnabled(false);
        imageLabel->setText(QStringLiteral("Image URL is not available"));
        return;
    }

    const QString sourceUrl = variant->url.toString();
    if (!imageLabel->sourcePixmap().isNull()
        && imageLabel->property("evidenceSourceUrl").toString() == sourceUrl) {
        imageLabel->setText(QString());
        openButton->setEnabled(true);
        return;
    }

    imageLabel->setSourcePixmap(QPixmap());
    imageLabel->setProperty("evidenceRequestId", QString());
    imageLabel->setProperty("evidenceSourceUrl", sourceUrl);
    openButton->setEnabled(false);
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
    imageLabel->setProperty("evidenceSourceUrl", QString());
    imageLabel->setText(message);
    titleLabel->setText(title);
    metadataLabel->setText(QStringLiteral("No capture metadata"));
    openButton->setEnabled(false);
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
