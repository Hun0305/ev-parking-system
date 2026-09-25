#include "ivasettingspage.h"

#include "iva/ivavideocanvas.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QPolygonF>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {
class UniformChecklistWidget final : public QListWidget
{
public:
    explicit UniformChecklistWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
    }

    void synchronizeGrid()
    {
        if (m_synchronizing) return;
        m_synchronizing = true;
        constexpr int minimumCellWidth = 155;
        constexpr int cellHeight = 34;
        const int availableWidth = qMax(1, viewport()->width() - 2);
        const int columns = qBound(1, availableWidth / minimumCellWidth, 4);
        const int cellWidth = qMax(minimumCellWidth, availableWidth / columns);
        const QSize cellSize(cellWidth, cellHeight);
        setGridSize(cellSize);
        for (int row = 0; row < count(); ++row) item(row)->setSizeHint(cellSize);
        const int rows = count() == 0 ? 1 : (count() + columns - 1) / columns;
        setFixedHeight(rows * cellHeight + frameWidth() * 2 + 6);
        m_synchronizing = false;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        synchronizeGrid();
    }

private:
    bool m_synchronizing = false;
};

QIcon checklistIcon(const QString &value, bool detectionMode)
{
    const QString normalized = value.toLower();
    QString name;
    if (detectionMode) {
        if (normalized.contains(QStringLiteral("intrusion"))) {
            name = QStringLiteral("detection-intrusion");
        } else if (normalized.contains(QStringLiteral("loiter"))) {
            name = QStringLiteral("detection-loitering");
        } else if (normalized.contains(QStringLiteral("enter"))
                   || normalized.contains(QStringLiteral("exit"))) {
            name = QStringLiteral("detection-enter");
        } else {
            name = QStringLiteral("detection-generic");
        }
    } else if (normalized.contains(QStringLiteral("bicycle"))) {
        name = QStringLiteral("object-bicycle");
    } else if (normalized.contains(QStringLiteral("motorcycle"))) {
        name = QStringLiteral("object-motorcycle");
    } else if (normalized.contains(QStringLiteral("truck"))) {
        name = QStringLiteral("object-truck");
    } else if (normalized.contains(QStringLiteral("bus"))) {
        name = QStringLiteral("object-bus");
    } else if (normalized.contains(QStringLiteral("car"))) {
        name = QStringLiteral("object-car");
    } else if (normalized.contains(QStringLiteral("person"))) {
        name = QStringLiteral("object-person");
    } else {
        name = QStringLiteral("object-generic");
    }
    return QIcon(QStringLiteral(":/resources/icons/iva/%1.svg").arg(name));
}

QString objectDisplayName(const QString &value)
{
    return value.startsWith(QStringLiteral("Vehicle."))
        ? value.section(QLatin1Char('.'), -1) : value;
}

QIcon objectIconStrip(const QStringList &objects)
{
    constexpr int iconSize = 16;
    constexpr int gap = 3;
    const int count = qMin(objects.size(), 6);
    if (count <= 0) return checklistIcon(QString(), false);

    QPixmap strip(count * iconSize + (count - 1) * gap, iconSize);
    strip.fill(Qt::transparent);
    QPainter painter(&strip);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int index = 0; index < count; ++index) {
        const QPixmap icon = checklistIcon(objects.at(index), false)
                                 .pixmap(iconSize, iconSize);
        painter.drawPixmap(index * (iconSize + gap), 0, icon);
    }
    return QIcon(strip);
}
}

IvaSettingsPage::IvaSettingsPage(const QString &cameraIp, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    layout->addLayout(createPageHeader(
        this, QStringLiteral("IVA Setup"),
        QStringLiteral("Hanwha WiseAI rule configuration")));
    auto *helpButton = new QPushButton(QStringLiteral("IVA Setup 안내"), this);
    helpButton->setObjectName(QStringLiteral("ivaHelpButton"));
    helpButton->setAccessibleName(QStringLiteral("IVA Setup 카메라 및 Pi 저장 안내"));
    helpButton->setToolTip(QStringLiteral("WiseAI Area 편집과 Pi Crop ROI 저장 방법 보기"));
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setIcon(pageHelpIcon());
    helpButton->setIconSize(QSize(22, 22));
    helpButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238; color:white; border:1px solid #455a64; "
        "border-radius:6px; padding:6px 11px; font-weight:800; }"
        "QPushButton:hover { background:#37474f; border-color:#fb8c00; }"
        "QPushButton:pressed { background:#1c252a; }"));

    auto *connectionBar = new QWidget(this);
    connectionBar->setObjectName(QStringLiteral("ivaConnectionBar"));
    auto *connectionLayout = new QGridLayout(connectionBar);
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    connectionLayout->setHorizontalSpacing(10);
    connectionLayout->setVerticalSpacing(3);
    m_cameraLabel = new QLabel(connectionBar);
    m_cameraLabel->setObjectName(QStringLiteral("ivaCameraAddressLabel"));
    m_cameraLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#263238;font-weight:800;"));
    m_cameraLabel->setVisible(false);
    m_channelSummaryLabel = new QLabel(QStringLiteral("Not loaded"), connectionBar);
    m_channelSummaryLabel->setObjectName(QStringLiteral("ivaChannelSummaryLabel"));
    m_channelSummaryLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#607d8b;font-size:11px;font-weight:700;"));
    m_channelSummaryLabel->setWordWrap(true);
    // The channel buttons below are the single source of truth for per-channel
    // state. Keep the label available for diagnostics/tests, but do not repeat
    // the same ON/OFF/area counts in the connection bar.
    m_channelSummaryLabel->setVisible(false);
    m_statusLabel = new QLabel(
        QStringLiteral("Open this page or refresh to read the camera."), connectionBar);
    m_statusLabel->setObjectName(QStringLiteral("ivaStatusLabel"));
    m_statusLabel->setStyleSheet(QStringLiteral("border:none;color:#455a64;"));
    m_statusLabel->setWordWrap(true);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), connectionBar);
    m_refreshButton->setObjectName(QStringLiteral("refreshIvaConfigurationButton"));
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(QStringLiteral("Reload WiseAI settings from the camera"));
    // Configuration is refreshed automatically while this page is visible;
    // keep the status line full width instead of spending space on a manual
    // refresh button that can be pressed repeatedly.
    m_refreshButton->setVisible(false);
    connectionLayout->addWidget(m_statusLabel, 0, 0, 1, 3);
    connectionLayout->setColumnStretch(1, 1);
    layout->addWidget(connectionBar);

    auto *controlRow = new QWidget(this);
    controlRow->setObjectName(QStringLiteral("ivaTopControlRow"));
    auto *controlLayout = new QHBoxLayout(controlRow);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(10);
    layout->addWidget(controlRow);

    auto *workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter->setObjectName(QStringLiteral("ivaWorkspaceSplitter"));
    auto *videoPanel = new QWidget(workspaceSplitter);
    auto *videoLayout = new QVBoxLayout(videoPanel);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(8);
    auto *channelRow = new QHBoxLayout;
    channelRow->setSpacing(6);
    auto *channelLabel = new QLabel(QStringLiteral("CHANNEL"), controlRow);
    channelLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:800;"));
    channelRow->addWidget(channelLabel);
    auto *channelGroup = new QButtonGroup(this);
    channelGroup->setExclusive(true);
    for (int channel = 0; channel < 4; ++channel) {
        auto *button = new QPushButton(QStringLiteral("CH%1\n--").arg(channel + 1),
                                       controlRow);
        button->setObjectName(QStringLiteral("ivaPreviewChannel%1Button").arg(channel + 1));
        button->setCheckable(true);
        button->setProperty("channel", channel);
        button->setFixedHeight(42);
        button->setMinimumWidth(68);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background:#eef3f6;color:#455a64;border:1px solid #b8c4cc;"
            "border-radius:4px;padding:3px 8px;font-weight:800; }"
            "QPushButton[channelState=\"on\"] { color:#1b5e20; }"
            "QPushButton[channelState=\"off\"] { color:#b71c1c; }"
            "QPushButton:hover { background:#e4eef4;border-color:#2d9cff; }"
            "QPushButton:checked { background:#dceaf5;color:#10212c;"
            "border:2px solid #2d9cff; }"));
        channelGroup->addButton(button, channel);
        channelRow->addWidget(button);
        m_channelButtons.append(button);
        connect(button, &QPushButton::clicked, this, [this, channel]() {
            selectChannel(channel);
            selectMappedParkingArea();
        });
    }
    m_channelButtons.constFirst()->setChecked(true);
    m_channelEnabledCheck = new QCheckBox(QStringLiteral("Channel enabled"), controlRow);
    m_channelEnabledCheck->setObjectName(QStringLiteral("ivaChannelEnabledCheck"));
    m_channelEnabledCheck->setToolTip(QStringLiteral(
        "Enable or disable the selected camera channel when saving WiseAI settings"));
    channelRow->addWidget(m_channelEnabledCheck);
    channelRow->addStretch(1);
    m_discardDraftButton = new QPushButton(QStringLiteral("Discard draft"), controlRow);
    m_discardDraftButton->setObjectName(QStringLiteral("ivaDiscardDraftButton"));
    m_discardDraftButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    m_discardDraftButton->setToolTip(QStringLiteral("Restore the last camera-loaded Area"));
    controlLayout->addLayout(channelRow, 1);
    m_videoCanvas = new IvaVideoCanvas(videoPanel);
    videoLayout->addWidget(m_videoCanvas, 1);
    m_frameStatusLabel = new QLabel(
        QStringLiteral("Refresh the camera, then select a channel."), videoPanel);
    m_frameStatusLabel->setObjectName(QStringLiteral("ivaFrameStatusLabel"));
    m_frameStatusLabel->setWordWrap(true);
    auto *canvasFooter = new QHBoxLayout;
    canvasFooter->setSpacing(10);
    canvasFooter->addWidget(m_frameStatusLabel, 1);
    const auto addLegend = [videoPanel, canvasFooter](const QString &color,
                                                       const QString &text,
                                                       Qt::PenStyle penStyle = Qt::SolidLine) {
        auto *swatch = new QFrame(videoPanel);
        swatch->setFixedSize(18, 10);
        swatch->setStyleSheet(QStringLiteral(
            "QFrame { background:%1;border:2px %2 %1;border-radius:2px; }")
                                  .arg(color,
                                       penStyle == Qt::DashLine
                                           ? QStringLiteral("dashed")
                                           : QStringLiteral("solid")));
        auto *label = new QLabel(text, videoPanel);
        label->setStyleSheet(QStringLiteral(
            "color:#607d8b;font-size:10px;font-weight:700;"));
        canvasFooter->addWidget(swatch);
        canvasFooter->addWidget(label);
    };
    addLegend(QStringLiteral("#76ff03"), QStringLiteral("Camera Area"));
    addLegend(QStringLiteral("#00e5ff"), QStringLiteral("Selected"));
    addLegend(QStringLiteral("#ffca28"), QStringLiteral("Pi ROI"), Qt::DashLine);
    videoLayout->addLayout(canvasFooter);
    workspaceSplitter->addWidget(videoPanel);

    auto *splitter = new QSplitter(Qt::Vertical, workspaceSplitter);
    splitter->setObjectName(QStringLiteral("ivaRuleDetailsSplitter"));
    splitter->setMinimumWidth(460);
    splitter->setMaximumWidth(640);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle:vertical { background:#cfd8dc; margin:2px 0; "
        "border-radius:2px; }"
        "QSplitter::handle:vertical:hover { background:#90a4ae; }"));
    auto *areaListPanel = new QWidget(splitter);
    auto *areaListLayout = new QVBoxLayout(areaListPanel);
    areaListLayout->setContentsMargins(0, 0, 0, 0);
    areaListLayout->setSpacing(5);
    auto *areaListHeader = new QHBoxLayout;
    auto *areaListTitle = new QLabel(QStringLiteral("Camera WiseAI Areas"), areaListPanel);
    areaListTitle->setObjectName(QStringLiteral("ivaCameraWiseAiAreasTitle"));
    areaListTitle->setStyleSheet(QStringLiteral(
        "color:#202124;font-size:14px;font-weight:900;"));
    m_areaCountLabel = new QLabel(QStringLiteral("0 areas"), areaListPanel);
    m_areaCountLabel->setObjectName(QStringLiteral("ivaAreaCountLabel"));
    m_areaCountLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:800;"));
    areaListHeader->addWidget(areaListTitle);
    areaListHeader->addStretch();
    areaListHeader->addWidget(m_areaCountLabel);
    areaListLayout->addLayout(areaListHeader);
    m_areaTable = new QTableWidget(0, 6, areaListPanel);
    m_areaTable->setObjectName(QStringLiteral("ivaAreaTable"));
    m_areaTable->setMinimumHeight(150);
    m_areaTable->setHorizontalHeaderLabels(
        {QStringLiteral("CH"), QStringLiteral("Area"),
         QStringLiteral("Rule"), QStringLiteral("Modes"),
         QStringLiteral("Objects"), QStringLiteral("State")});
    m_areaTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_areaTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_areaTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_areaTable->setAlternatingRowColors(true);
    m_areaTable->verticalHeader()->setVisible(false);
    m_areaTable->verticalHeader()->setDefaultSectionSize(26);
    m_areaTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_areaTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_areaTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_areaTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_areaTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_areaTable->setWordWrap(false);
    m_areaTable->setIconSize(QSize(111, 18));
    m_areaTable->setShowGrid(false);
    m_areaTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background:#f8fafb;alternate-background-color:#eef3f6;"
        "border:1px solid #c7d0d8; }"
        "QTableWidget::item { padding:3px 5px; }"
        "QTableWidget::item:selected { background:#dceaf5;color:#10212c; }"));
    areaListLayout->addWidget(m_areaTable);

    auto *editorScrollArea = new QScrollArea(splitter);
    editorScrollArea->setObjectName(QStringLiteral("ivaRuleEditorScrollArea"));
    editorScrollArea->setWidgetResizable(true);
    editorScrollArea->setFrameShape(QFrame::NoFrame);
    editorScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editorScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *editorGroup = new QWidget;
    editorGroup->setObjectName(QStringLiteral("ivaRuleEditor"));
    editorGroup->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    editorGroup->setMinimumWidth(380);
    auto *editorStack = new QVBoxLayout(editorGroup);
    editorStack->setContentsMargins(2, 2, 6, 8);
    editorStack->setSpacing(9);

    auto *selectedHeader = new QFrame(editorGroup);
    selectedHeader->setObjectName(QStringLiteral("ivaSelectedAreaHeader"));
    selectedHeader->setStyleSheet(QStringLiteral(
        "QFrame#ivaSelectedAreaHeader { background:#f8fafb;border:1px solid #cfd8dc;"
        "border-radius:6px; }"));
    auto *selectedHeaderLayout = new QGridLayout(selectedHeader);
    selectedHeaderLayout->setContentsMargins(10, 8, 10, 8);
    selectedHeaderLayout->setVerticalSpacing(3);
    m_selectedAreaTitleLabel = new QLabel(QStringLiteral("No Area selected"), selectedHeader);
    m_selectedAreaTitleLabel->setObjectName(QStringLiteral("ivaSelectedAreaTitleLabel"));
    m_selectedAreaTitleLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#202124;font-size:15px;font-weight:900;"));
    m_selectedAreaMetaLabel = new QLabel(
        QStringLiteral("Select a camera Area from the table or video."), selectedHeader);
    m_selectedAreaMetaLabel->setObjectName(QStringLiteral("ivaSelectedAreaMetaLabel"));
    m_selectedAreaMetaLabel->setWordWrap(true);
    m_selectedAreaMetaLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#607d8b;font-size:11px;"));
    m_selectedAreaStateLabel = new QLabel(QStringLiteral("NO SELECTION"), selectedHeader);
    m_selectedAreaStateLabel->setObjectName(QStringLiteral("ivaSelectedAreaStateLabel"));
    m_selectedAreaStateLabel->setAlignment(Qt::AlignCenter);
    m_selectedAreaStateLabel->setMinimumSize(82, 24);
    m_cameraSaveStatusLabel = new QLabel(QStringLiteral("Camera save: idle"), selectedHeader);
    m_cameraSaveStatusLabel->setObjectName(QStringLiteral("ivaCameraSaveStatusLabel"));
    m_cameraSaveStatusLabel->setWordWrap(true);
    m_cameraSaveStatusLabel->setMinimumHeight(18);
    m_cameraSaveStatusLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#607d8b;font-size:10px;font-weight:700;"));
    m_piRoiStatusLabel = new QLabel(QStringLiteral("Pi ROI: automatic"), selectedHeader);
    m_piRoiStatusLabel->setObjectName(QStringLiteral("ivaPiRoiStatusLabel"));
    m_piRoiStatusLabel->setWordWrap(true);
    m_piRoiStatusLabel->setMinimumHeight(18);
    m_piRoiStatusLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#607d8b;font-size:10px;font-weight:700;"));
    selectedHeaderLayout->addWidget(m_selectedAreaTitleLabel, 0, 0);
    selectedHeaderLayout->addWidget(m_selectedAreaStateLabel, 0, 1, Qt::AlignRight);
    selectedHeaderLayout->addWidget(m_selectedAreaMetaLabel, 1, 0, 1, 2);
    selectedHeaderLayout->addWidget(m_cameraSaveStatusLabel, 2, 0, 1, 2);
    selectedHeaderLayout->addWidget(m_piRoiStatusLabel, 3, 0, 1, 2);
    selectedHeaderLayout->setColumnStretch(0, 1);
    editorStack->addWidget(selectedHeader);

    auto *ruleGroup = new QGroupBox(QStringLiteral("Rule configuration"), editorGroup);
    auto *editorLayout = new QGridLayout(ruleGroup);
    editorLayout->setColumnStretch(2, 1);
    editorLayout->setHorizontalSpacing(8);
    editorLayout->setVerticalSpacing(6);
    m_indexSpin = new QSpinBox(ruleGroup);
    m_indexSpin->setObjectName(QStringLiteral("ivaRuleIndexSpin"));
    m_indexSpin->setRange(0, 999);
    m_nameEdit = new QLineEdit(ruleGroup);
    m_nameEdit->setObjectName(QStringLiteral("ivaRuleNameEdit"));
    m_nameEdit->setMinimumWidth(240);
    m_nameEdit->setPlaceholderText(QStringLiteral("name1-name4 or camera rule name"));
    editorLayout->addWidget(new QLabel(QStringLiteral("Rule name"), ruleGroup), 0, 0, 1, 4);
    editorLayout->addWidget(m_nameEdit, 1, 0, 1, 4);
    editorLayout->addWidget(new QLabel(QStringLiteral("Area index"), ruleGroup), 2, 0);
    editorLayout->addWidget(m_indexSpin, 2, 1);
    m_areaMappingLabel = new QLabel(QStringLiteral("No parking slot mapping"), ruleGroup);
    m_areaMappingLabel->setObjectName(QStringLiteral("ivaAreaMappingLabel"));
    m_areaMappingLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:700;"));
    editorLayout->addWidget(m_areaMappingLabel, 2, 2, 1, 2);

    m_detectionModesList = new UniformChecklistWidget(ruleGroup);
    m_detectionModesList->setObjectName(QStringLiteral("ivaDetectionModesList"));
    m_objectFiltersList = new UniformChecklistWidget(ruleGroup);
    m_objectFiltersList->setObjectName(QStringLiteral("ivaObjectFiltersList"));
    auto *detectionHeader = new QHBoxLayout;
    detectionHeader->addWidget(new QLabel(QStringLiteral("Detection modes"), ruleGroup));
    detectionHeader->addStretch();
    m_detectionSelectionLabel = new QLabel(QStringLiteral("0 selected"), ruleGroup);
    m_detectionSelectionLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:700;"));
    detectionHeader->addWidget(m_detectionSelectionLabel);
    auto *objectHeader = new QHBoxLayout;
    objectHeader->addWidget(new QLabel(QStringLiteral("Object filters"), ruleGroup));
    objectHeader->addStretch();
    m_allObjectFiltersCheck = new QCheckBox(QStringLiteral("All objects"), ruleGroup);
    m_allObjectFiltersCheck->setObjectName(QStringLiteral("ivaAllObjectFiltersCheck"));
    objectHeader->addWidget(m_allObjectFiltersCheck);
    m_objectSelectionLabel = new QLabel(QStringLiteral("0 selected"), ruleGroup);
    m_objectSelectionLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:700;"));
    objectHeader->addWidget(m_objectSelectionLabel);
    editorLayout->addLayout(detectionHeader, 3, 0, 1, 4);
    editorLayout->addWidget(m_detectionModesList, 4, 0, 1, 4);
    editorLayout->addLayout(objectHeader, 5, 0, 1, 4);
    editorLayout->addWidget(m_objectFiltersList, 6, 0, 1, 4);
    for (QListWidget *list : {m_detectionModesList, m_objectFiltersList}) {
        list->setFlow(QListView::LeftToRight);
        list->setWrapping(true);
        list->setResizeMode(QListView::Adjust);
        list->setMovement(QListView::Static);
        list->setIconSize(QSize(18, 18));
        list->setSpacing(0);
        list->setUniformItemSizes(true);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setFocusPolicy(Qt::NoFocus);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setStyleSheet(QStringLiteral(
            "QListWidget { background:#f8fafb;border:1px solid #cfd8dc;"
            "border-radius:4px;padding:3px;outline:0; }"
            "QListWidget::item { background:#ffffff;color:#263238;"
            "border:1px solid #d7e0e5;border-radius:0;padding:4px 7px; }"
            "QListWidget::item:hover { background:#eef5f9;border-color:#90a4ae; }"
            "QListWidget::indicator { width:16px;height:16px; }"
            "QListWidget::indicator:unchecked { background:#ffffff;"
            "border:1px solid #78909c;border-radius:3px; }"
            "QListWidget::indicator:checked { background:#2d9cff;"
            "border:1px solid #1976d2;border-radius:3px;"
            "image:url(:/resources/icons/iva/check.svg); }"));
    }
    static_cast<UniformChecklistWidget *>(m_detectionModesList)->synchronizeGrid();
    static_cast<UniformChecklistWidget *>(m_objectFiltersList)->synchronizeGrid();

    auto *durationWidget = new QWidget(ruleGroup);
    auto *durationLayout = new QGridLayout(durationWidget);
    durationLayout->setContentsMargins(0, 0, 0, 0);
    durationLayout->setHorizontalSpacing(8);
    m_appearanceDurationSpin = new QSpinBox(durationWidget);
    m_appearanceDurationSpin->setObjectName(QStringLiteral("ivaAppearanceDurationSpin"));
    m_intrusionDurationSpin = new QSpinBox(durationWidget);
    m_intrusionDurationSpin->setObjectName(QStringLiteral("ivaIntrusionDurationSpin"));
    m_loiteringDurationSpin = new QSpinBox(durationWidget);
    m_loiteringDurationSpin->setObjectName(QStringLiteral("ivaLoiteringDurationSpin"));
    for (QSpinBox *spin : {m_appearanceDurationSpin, m_intrusionDurationSpin,
                           m_loiteringDurationSpin}) {
        spin->setRange(0, 3600);
        spin->setSuffix(QStringLiteral(" s"));
    }
    m_appearanceDurationLabel = new QLabel(QStringLiteral("Appearance"), durationWidget);
    m_appearanceDurationLabel->setObjectName(QStringLiteral("ivaAppearanceDurationLabel"));
    m_intrusionDurationLabel = new QLabel(QStringLiteral("Intrusion"), durationWidget);
    m_intrusionDurationLabel->setObjectName(QStringLiteral("ivaIntrusionDurationLabel"));
    m_loiteringDurationLabel = new QLabel(QStringLiteral("Loitering"), durationWidget);
    m_loiteringDurationLabel->setObjectName(QStringLiteral("ivaLoiteringDurationLabel"));
    const QList<QLabel *> durationLabels{
        m_appearanceDurationLabel, m_intrusionDurationLabel,
        m_loiteringDurationLabel};
    const QList<QSpinBox *> durationSpins{
        m_appearanceDurationSpin, m_intrusionDurationSpin,
        m_loiteringDurationSpin};
    for (int column = 0; column < durationSpins.size(); ++column) {
        durationLayout->addWidget(durationLabels.at(column), 0, column);
        durationLayout->addWidget(durationSpins.at(column), 1, column);
        durationLayout->setColumnStretch(column, 1);
    }
    editorLayout->addWidget(durationWidget, 7, 0, 1, 4);
    editorStack->addWidget(ruleGroup);

    m_geometryToggleButton = new QToolButton(editorGroup);
    m_geometryToggleButton->setObjectName(QStringLiteral("ivaGeometryToggleButton"));
    m_geometryToggleButton->setText(QStringLiteral("Advanced geometry"));
    m_geometryToggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_geometryToggleButton->setArrowType(Qt::RightArrow);
    m_geometryToggleButton->setCheckable(true);
    m_geometryToggleButton->setCursor(Qt::PointingHandCursor);
    editorStack->addWidget(m_geometryToggleButton, 0, Qt::AlignLeft);
    m_geometryWidget = new QWidget(editorGroup);
    m_geometryWidget->setObjectName(QStringLiteral("ivaAdvancedGeometry"));
    auto *coordinateLayout = new QVBoxLayout(m_geometryWidget);
    coordinateLayout->setContentsMargins(0, 0, 0, 0);
    coordinateLayout->setSpacing(5);
    m_coordinateTable = new QTableWidget(0, 2, m_geometryWidget);
    m_coordinateTable->setObjectName(QStringLiteral("ivaCoordinateTable"));
    m_coordinateTable->setHorizontalHeaderLabels(
        {QStringLiteral("X"), QStringLiteral("Y")});
    m_coordinateTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_coordinateTable->verticalHeader()->setDefaultSectionSize(24);
    m_coordinateTable->setMinimumHeight(145);
    auto *coordinateButtons = new QHBoxLayout;
    m_addPointButton = new QPushButton(QStringLiteral("Add point"), m_geometryWidget);
    m_addPointButton->setObjectName(QStringLiteral("ivaAddPointButton"));
    m_removePointButton = new QPushButton(QStringLiteral("Remove point"), m_geometryWidget);
    m_removePointButton->setObjectName(QStringLiteral("ivaRemovePointButton"));
    coordinateButtons->addWidget(m_addPointButton);
    coordinateButtons->addWidget(m_removePointButton);
    coordinateButtons->addStretch(1);
    coordinateLayout->addWidget(new QLabel(
        QStringLiteral("Polygon points (camera pixel coordinates)"), m_geometryWidget));
    coordinateLayout->addWidget(m_coordinateTable);
    coordinateLayout->addLayout(coordinateButtons);
    m_geometryWidget->setVisible(false);
    editorStack->addWidget(m_geometryWidget);

    auto *cameraSaveGroup = new QFrame(editorGroup);
    cameraSaveGroup->setObjectName(QStringLiteral("ivaSaveChangesGroup"));
    cameraSaveGroup->setStyleSheet(QStringLiteral(
        "QFrame#ivaSaveChangesGroup { background:#ffffff;border:1px solid #cfd8dc;"
        "border-radius:6px; }"));
    auto *cameraSaveLayout = new QHBoxLayout(cameraSaveGroup);
    cameraSaveLayout->setContentsMargins(5, 4, 5, 4);
    cameraSaveLayout->setSpacing(4);
    m_piSlotCombo = new QComboBox(cameraSaveGroup);
    m_piSlotCombo->setObjectName(QStringLiteral("ivaPiParkingSlotCombo"));
    m_piSlotCombo->setMinimumWidth(72);
    m_piSlotCombo->setMaximumWidth(82);
    m_piSlotCombo->setToolTip(QStringLiteral(
        "Parking Map slots use their channel and IVA Area mapping for WiseAI selection."));
    m_piMappingLabel = new QLabel(QStringLiteral("CH- / Area -"),
                                  cameraSaveGroup);
    m_piMappingLabel->setObjectName(QStringLiteral("ivaPiMappingLabel"));
    m_piMappingLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#455a64;font-size:10px;font-weight:700;"));
    m_piMappingLabel->setWordWrap(false);
    m_piMappingLabel->setMinimumWidth(76);
    m_piMappingLabel->setMaximumWidth(94);
    m_piMappingLabel->setMaximumHeight(18);
    m_applyButton = new QPushButton(QStringLiteral("No changes"),
                                    cameraSaveGroup);
    m_applyButton->setObjectName(QStringLiteral("saveIvaChangesButton"));
    m_applyButton->setIcon(QIcon());
    m_applyButton->setToolTip(QStringLiteral(
        "Save changed Camera WiseAI settings, then automatically save the mapped Pi Crop ROI"));
    m_deleteAreaButton = new QPushButton(QStringLiteral("Delete selected Area"),
                                         cameraSaveGroup);
    m_deleteAreaButton->setObjectName(QStringLiteral("deleteSelectedIvaAreaButton"));
    // These controls are intentionally text-only in the compact row. Their
    // icons consumed the available width and left labels visibly clipped.
    m_deleteAreaButton->setIcon(QIcon());
    cameraSaveLayout->addWidget(m_piSlotCombo);
    cameraSaveLayout->addWidget(m_piMappingLabel);
    m_deleteAreaButton->setText(QStringLiteral("Delete"));
    m_deleteAreaButton->setFixedWidth(62);
    m_deleteAreaButton->setToolTip(QStringLiteral("Delete selected IVA Area"));
    m_discardDraftButton->setText(QStringLiteral("Discard"));
    m_discardDraftButton->setIcon(QIcon());
    m_discardDraftButton->setFixedWidth(72);
    m_discardDraftButton->setToolTip(QStringLiteral("Discard the current camera draft"));
    cameraSaveLayout->addWidget(m_deleteAreaButton);
    cameraSaveLayout->addWidget(m_discardDraftButton);
    m_applyButton->setMinimumWidth(112);
    m_applyButton->setMaximumWidth(132);
    cameraSaveLayout->addWidget(m_applyButton);
    channelRow->addWidget(cameraSaveGroup, 0, Qt::AlignRight);
    cameraSaveGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    cameraSaveGroup->setFixedHeight(42);
    editorStack->addStretch();
    editorScrollArea->setWidget(editorGroup);
    splitter->addWidget(areaListPanel);
    splitter->addWidget(editorScrollArea);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({180, 420});
    workspaceSplitter->addWidget(splitter);
    workspaceSplitter->setStretchFactor(0, 1);
    workspaceSplitter->setStretchFactor(1, 0);
    workspaceSplitter->setSizes({720, 560});
    layout->addWidget(workspaceSplitter, 1);

    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_requestInFlight) return;
        setRequestStarted();
        emit refreshRequested();
    });
    connect(m_areaTable, &QTableWidget::currentCellChanged,
            this, [this](int currentRow) { populateEditor(currentRow); });
    connect(m_addPointButton, &QPushButton::clicked, this, [this]() {
        double x = 0.0;
        double y = 0.0;
        const int lastRow = m_coordinateTable->rowCount() - 1;
        if (lastRow >= 0) {
            x = m_coordinateTable->item(lastRow, 0)->text().toDouble();
            y = m_coordinateTable->item(lastRow, 1)->text().toDouble();
        }
        addCoordinateRow(x, y);
        updateButtons();
    });
    connect(m_removePointButton, &QPushButton::clicked, this, [this]() {
        const int row = m_coordinateTable->currentRow();
        if (row >= 0) {
            m_coordinateTable->removeRow(row);
            setEditorDirtyFeedback(true);
        }
        updateButtons();
    });
    connect(m_coordinateTable, &QTableWidget::currentCellChanged,
            this, [this]() { updateButtons(); });
    connect(m_coordinateTable, &QTableWidget::itemChanged,
            this, [this]() {
        if (!m_updatingEditor) {
            setEditorDirtyFeedback(true);
            updateEditorSummary();
        }
    });
    connect(m_geometryToggleButton, &QToolButton::toggled,
            this, [this](bool expanded) {
        m_geometryToggleButton->setArrowType(
            expanded ? Qt::DownArrow : Qt::RightArrow);
        m_geometryWidget->setVisible(expanded);
    });
    connect(m_channelEnabledCheck, &QCheckBox::toggled,
            this, [this]() {
        if (!m_updatingEditor) setEditorDirtyFeedback();
    });
    connect(m_indexSpin, &QSpinBox::valueChanged, this, [this]() {
        if (!m_updatingEditor) {
            setEditorDirtyFeedback();
            updateButtons();
        }
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_updatingEditor) {
            setEditorDirtyFeedback();
            updateButtons();
        }
    });
    for (QSpinBox *spin : {m_appearanceDurationSpin, m_intrusionDurationSpin,
                           m_loiteringDurationSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, [this]() {
            if (!m_updatingEditor) setEditorDirtyFeedback();
        });
    }
    connect(m_detectionModesList, &QListWidget::itemChanged,
            this, [this]() {
        if (!m_updatingEditor) {
            updateDurationAvailability();
            setEditorDirtyFeedback();
            updateEditorSummary();
        }
    });
    connect(m_objectFiltersList, &QListWidget::itemChanged,
            this, [this]() {
        if (!m_updatingEditor) {
            const QSignalBlocker blocker(m_allObjectFiltersCheck);
            m_allObjectFiltersCheck->setChecked(
                m_objectFiltersList->count() > 0
                && checkedValues(m_objectFiltersList).size()
                       == m_objectFiltersList->count());
            setEditorDirtyFeedback();
            updateEditorSummary();
        }
    });
    connect(m_allObjectFiltersCheck, &QCheckBox::toggled,
            this, [this](bool checked) {
        if (m_updatingEditor) return;
        m_updatingEditor = true;
        for (int row = 0; row < m_objectFiltersList->count(); ++row) {
            m_objectFiltersList->item(row)->setCheckState(
                checked ? Qt::Checked : Qt::Unchecked);
        }
        m_updatingEditor = false;
        setEditorDirtyFeedback();
        updateEditorSummary();
    });
    connect(m_discardDraftButton, &QPushButton::clicked,
            this, &IvaSettingsPage::discardRectangleDraft);
    connect(m_videoCanvas, &IvaVideoCanvas::rectangleDrafted,
            this, [this](const QRectF &rectangle) {
        createRectangleDraft(rectangle);
    });
    connect(m_videoCanvas, &IvaVideoCanvas::rectangleEdited,
            this, [this](const QRectF &rectangle) {
        updateRectangleDraft(rectangle);
    });
    connect(m_videoCanvas, &IvaVideoCanvas::rectangleRejected,
            this, [this](const QString &message) {
        m_statusLabel->setText(message);
        m_statusLabel->setStyleSheet(
            QStringLiteral("color:#b71c1c;font-weight:700;"));
        updateButtons();
    });
    connect(m_videoCanvas, &IvaVideoCanvas::areaSelected,
            this, [this](int areaIndex) {
        for (int row = 0; row < m_configuration.areas.size(); ++row) {
            const IvaAreaDefinition &area = m_configuration.areas.at(row);
            if (area.channel == m_selectedChannel && area.areaIndex == areaIndex) {
                m_areaTable->selectRow(row);
                return;
            }
        }
    });
    connect(m_videoCanvas, &IvaVideoCanvas::frameCompatibilityChanged,
            this, [this](bool compatible, const QString &message) {
        m_frameStatusLabel->setText(message);
        m_frameStatusLabel->setStyleSheet(
            compatible ? QStringLiteral("color:#1b5e20;")
                       : QStringLiteral("color:#b71c1c;"));
        updateButtons();
            });
    connect(m_piSlotCombo, &QComboBox::currentTextChanged,
            this, [this]() {
        const ParkingZoneLayout *mapping = parkingZoneMapping(m_piSlotCombo->currentText());
        if (mapping) {
            m_selectedParkingZoneId = mapping->zoneId;
            m_selectedParkingChannel = mapping->cameraChannel;
            m_selectedParkingIvaAreaId = mapping->ivaAreaId;
            const int channel = cameraChannelIndex(mapping->cameraChannel);
            if (channel >= 0 && channel != m_selectedChannel) selectChannel(channel);
        }
        const int areaIndex = mappedParkingAreaIndex();
        m_piMappingLabel->setText(
            areaIndex > 0
                ? QStringLiteral("%1 / Area %2")
                      .arg(m_selectedParkingChannel)
                      .arg(areaIndex)
                : QStringLiteral("%1 / no IVA Area")
                      .arg(m_selectedParkingChannel));
        m_piMappingLabel->setToolTip(
            areaIndex > 0
                ? QStringLiteral("%1 ↔ %2 / Area %3")
                      .arg(m_piSlotCombo->currentText())
                      .arg(m_selectedParkingChannel)
                      .arg(areaIndex)
                : QStringLiteral("%1 has no Parking Map IVA Area mapping")
                      .arg(m_piSlotCombo->currentText()));
        if (!m_piRoiRequestInFlight) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "%1: Pi ROI automatic").arg(m_piSlotCombo->currentText()));
            m_piRoiStatusLabel->setToolTip(QStringLiteral(
                "%1 mapping selected. The normalized Pi Crop ROI is always included when this mapping has a valid frame.")
                                                .arg(m_piSlotCombo->currentText()));
            m_piRoiStatusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
        }
        if (m_draftChannel < 0) {
            selectMappedParkingArea();
        }
        if (m_areaMappingLabel) {
            m_areaMappingLabel->setText(
                editorMatchesParkingArea()
                    ? QStringLiteral("Mapped to %1").arg(m_piSlotCombo->currentText())
                    : QStringLiteral("No parking slot mapping"));
        }
        updateButtons();
    });
    connect(m_applyButton, &QPushButton::clicked,
            this, &IvaSettingsPage::startSaveChanges);
    connect(m_deleteAreaButton, &QPushButton::clicked, this, [this]() {
        if (m_requestInFlight || m_draftChannel >= 0
            || m_selectedArea < 0
            || m_selectedArea >= m_configuration.areas.size()) {
            return;
        }
        const IvaAreaDefinition selected = m_configuration.areas.at(m_selectedArea);
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this, QStringLiteral("Delete IVA Area"),
            QStringLiteral("Delete CH%1 IVA Area %2 (%3) from the camera?\n\n"
                           "The client will re-read the camera and roll back if verification differs.")
                .arg(selected.channel + 1)
                .arg(selected.areaIndex)
                .arg(selected.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) return;

        m_pendingDeletedAreaIndex = selected.areaIndex;
        m_pendingDeletedAreaName = selected.name;
        emit deleteAreaRequested(selected.channel, selected.areaIndex);
    });
    connect(helpButton, &QPushButton::clicked,
            this, &IvaSettingsPage::showHelpDialog);

    setCameraIp(cameraIp);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(100);
    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) emit previewFrameRequested(m_selectedChannel);
    });
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setObjectName(QStringLiteral("ivaConfigurationRefreshTimer"));
    m_refreshTimer->setInterval(10000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || m_requestInFlight || m_saveStage != SaveStage::Idle
            || m_piRoiRequestInFlight || m_cameraDirty || m_piDirty
            || m_draftChannel >= 0) {
            return;
        }
        setRequestStarted();
        emit refreshRequested();
    });
    selectChannel(0);
    clearEditor();
    updateButtons();
}

void IvaSettingsPage::showHelpDialog()
{
    if (QDialog *existing = findChild<QDialog *>(QStringLiteral("ivaHelpDialog"))) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("ivaHelpDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("IVA Setup 카메라·Pi 설정 가이드"));
    dialog->setModal(true);
    dialog->setMinimumSize(800, 600);
    dialog->resize(960, 780);

    auto *dialogLayout = new QVBoxLayout(dialog);
    dialogLayout->setContentsMargins(14, 14, 14, 14);
    dialogLayout->setSpacing(10);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setObjectName(QStringLiteral("ivaHelpScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("IVA Setup 카메라·Pi 설정 가이드"), content);
    title->setObjectName(QStringLiteral("ivaHelpTitle"));
    title->setStyleSheet(QStringLiteral(
        "font-size:22px;font-weight:900;color:#1f2d35;"));
    layout->addWidget(title);

    auto *intro = new QLabel(
        QStringLiteral("이 화면은 하나의 영상 영역을 편집하지만 저장 대상은 두 곳입니다. "
                       "WiseAI Area는 카메라에, Crop ROI는 Raspberry Pi에 각각 따로 저장됩니다."),
        content);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral(
        "background:#e3f2fd;color:#0d47a1;border:1px solid #90caf9;"
        "border-radius:7px;padding:10px;font-weight:800;"));
    layout->addWidget(intro);

    auto *destinationGroup = new QGroupBox(
        QStringLiteral("1. 가장 중요한 구분 · 하나의 영역, 두 저장 대상"), content);
    auto *destinationLayout = new QVBoxLayout(destinationGroup);
    auto *destinationFlow = new QWidget(destinationGroup);
    destinationFlow->setObjectName(QStringLiteral("ivaHelpDestinationFlow"));
    auto *flowLayout = new QHBoxLayout(destinationFlow);
    flowLayout->setContentsMargins(0, 2, 0, 0);
    flowLayout->setSpacing(10);
    auto makeDestinationCard = [destinationFlow](const QString &heading,
                                                 const QString &button,
                                                 const QString &payload,
                                                 const QString &result,
                                                 const QString &background,
                                                 const QString &border) {
        auto *card = new QFrame(destinationFlow);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:%1;border:2px solid %2;border-radius:8px; }")
                                .arg(background, border));
        auto *cardLayout = new QVBoxLayout(card);
        auto *cardTitle = new QLabel(heading, card);
        cardTitle->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-size:15px;font-weight:900;"));
        auto *buttonLabel = new QLabel(button, card);
        buttonLabel->setStyleSheet(QStringLiteral(
            "border:none;background:#263238;color:white;border-radius:4px;"
            "padding:6px;font-weight:800;"));
        auto *payloadLabel = new QLabel(payload, card);
        payloadLabel->setWordWrap(true);
        payloadLabel->setStyleSheet(QStringLiteral(
            "border:none;color:#455a64;font-size:11px;"));
        auto *resultLabel = new QLabel(result, card);
        resultLabel->setWordWrap(true);
        resultLabel->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:700;"));
        cardLayout->addWidget(cardTitle);
        cardLayout->addWidget(buttonLabel);
        cardLayout->addWidget(payloadLabel);
        cardLayout->addWidget(resultLabel);
        return card;
    };
    flowLayout->addWidget(makeDestinationCard(
        QStringLiteral("Hanwha Camera · WiseAI Area"),
        QStringLiteral("Save Area to Camera"),
        QStringLiteral("전체 polygon, rule name/index, 감지 모드, 객체 필터, 지속시간, 채널 활성 상태"),
        QStringLiteral("HTTPS Digest로 카메라 설정을 변경하며 Web Viewer에도 반영됩니다."),
        QStringLiteral("#fff8e1"), QStringLiteral("#ffb300")), 1);
    auto *separation = new QLabel(QStringLiteral("≠"), destinationFlow);
    separation->setAlignment(Qt::AlignCenter);
    separation->setStyleSheet(QStringLiteral(
        "color:#d84315;font-size:28px;font-weight:900;"));
    flowLayout->addWidget(separation);
    flowLayout->addWidget(makeDestinationCard(
        QStringLiteral("Raspberry Pi · Parking Crop ROI"),
        QStringLiteral("Save Crop ROI to Pi"),
        QStringLiteral("선택 polygon의 bounding rectangle을 0~1 정규화 좌표로 변환한 ROI만 전송"),
        QStringLiteral("Pi의 선택 EV 슬롯 ROI를 저장합니다. 이미지 파일이나 카메라 규칙은 전송하지 않습니다."),
        QStringLiteral("#e8f5e9"), QStringLiteral("#43a047")), 1);
    destinationLayout->addWidget(destinationFlow);
    auto *destinationWarning = new QLabel(
        QStringLiteral("카메라 저장 성공만으로 Pi ROI가 바뀌지 않으며, Pi 저장 성공만으로 카메라 Area가 바뀌지 않습니다."),
        destinationGroup);
    destinationWarning->setWordWrap(true);
    destinationWarning->setStyleSheet(QStringLiteral(
        "color:#b71c1c;font-weight:800;padding:5px;"));
    destinationLayout->addWidget(destinationWarning);
    layout->addWidget(destinationGroup);

    auto *startGroup = new QGroupBox(
        QStringLiteral("2. 영역을 만들기 전 · 준비 순서"), content);
    auto *startLayout = new QVBoxLayout(startGroup);
    auto *setupFlow = new QWidget(startGroup);
    setupFlow->setObjectName(QStringLiteral("ivaHelpSetupFlow"));
    auto *setupFlowLayout = new QHBoxLayout(setupFlow);
    setupFlowLayout->setContentsMargins(0, 0, 0, 0);
    setupFlowLayout->setSpacing(7);
    const QList<QPair<QString, QString>> setupSteps{
        {QStringLiteral("① Refresh"), QStringLiteral("Options · Capability · 현재 Configuration 조회")},
        {QStringLiteral("② CH 선택"), QStringLiteral("CH1~CH4 중 편집할 카메라 채널 선택")},
        {QStringLiteral("③ Parking Area"), QStringLiteral("Parking Map에서 매핑된 슬롯 선택")},
        {QStringLiteral("④ 영상 드래그"), QStringLiteral("호환되는 공유 RTSP 프레임에서 사각형 작성")}
    };
    for (int index = 0; index < setupSteps.size(); ++index) {
        auto *card = new QFrame(setupFlow);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:#f5f7f9;border:1px solid #cfd8dc;border-radius:6px; }"));
        auto *cardLayout = new QVBoxLayout(card);
        auto *stepTitle = new QLabel(setupSteps.at(index).first, card);
        stepTitle->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:900;"));
        auto *stepBody = new QLabel(setupSteps.at(index).second, card);
        stepBody->setWordWrap(true);
        stepBody->setStyleSheet(QStringLiteral(
            "border:none;color:#546e7a;font-size:10px;"));
        cardLayout->addWidget(stepTitle);
        cardLayout->addWidget(stepBody);
        setupFlowLayout->addWidget(card, 1);
        if (index < setupSteps.size() - 1) {
            setupFlowLayout->addWidget(new QLabel(QStringLiteral("→"), setupFlow));
        }
    }
    startLayout->addWidget(setupFlow);

    auto *compatibility = new QLabel(
        QStringLiteral("드래그 조건: 카메라가 IVA 좌표 해상도를 제공하고, Dashboard에서 공유받은 RTSP 프레임의 화면비가 "
                       "IVA 좌표계와 일치해야 합니다. Frame Status가 빨간색이면 먼저 해당 원인을 해결합니다."),
        startGroup);
    compatibility->setObjectName(QStringLiteral("ivaHelpFrameCompatibility"));
    compatibility->setWordWrap(true);
    compatibility->setStyleSheet(QStringLiteral(
        "background:#fff3e0;color:#5d4037;border:1px solid #ffcc80;"
        "border-radius:6px;padding:8px;"));
    startLayout->addWidget(compatibility);
    layout->addWidget(startGroup);

    auto *mappingGroup = new QGroupBox(
        QStringLiteral("3. Parking Area 매핑 · 현재 선택한 CH 안에서 적용"), content);
    auto *mappingLayout = new QVBoxLayout(mappingGroup);
    auto *mappingHint = new QLabel(
        QStringLiteral("Parking Map의 채널·IVA Area 매핑이 유효하면 저장 시 Pi Crop ROI가 자동으로 포함됩니다."),
        mappingGroup);
    mappingHint->setWordWrap(true);
    mappingLayout->addWidget(mappingHint);
    auto *mappingTable = new QTableWidget(4, 3, mappingGroup);
    mappingTable->setObjectName(QStringLiteral("ivaHelpMappingTable"));
    mappingTable->setHorizontalHeaderLabels({
        QStringLiteral("Parking Area"), QStringLiteral("Camera rule name"),
        QStringLiteral("Camera Area index")});
    mappingTable->verticalHeader()->setVisible(false);
    mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mappingTable->setSelectionMode(QAbstractItemView::NoSelection);
    mappingTable->setFocusPolicy(Qt::NoFocus);
    mappingTable->setFixedHeight(154);
    for (int row = 0; row < 4; ++row) {
        mappingTable->setItem(row, 0, new QTableWidgetItem(
            QStringLiteral("EV-0%1").arg(row + 1)));
        mappingTable->setItem(row, 1, new QTableWidgetItem(
            QStringLiteral("name%1").arg(row + 1)));
        mappingTable->setItem(row, 2, new QTableWidgetItem(
            QString::number(row + 1)));
    }
    mappingLayout->addWidget(mappingTable);
    layout->addWidget(mappingGroup);

    auto *canvasGroup = new QGroupBox(
        QStringLiteral("4. 영상 캔버스와 편집기 · 표시를 읽는 방법"), content);
    auto *canvasLayout = new QVBoxLayout(canvasGroup);
    auto *canvasLegend = new QWidget(canvasGroup);
    canvasLegend->setObjectName(QStringLiteral("ivaHelpCanvasLegend"));
    auto *legendLayout = new QGridLayout(canvasLegend);
    legendLayout->setContentsMargins(0, 0, 0, 0);
    legendLayout->setSpacing(7);
    struct CanvasMark {
        QString name;
        QString meaning;
        QString color;
        QString borderStyle;
    };
    const QList<CanvasMark> marks{
        {QStringLiteral("초록 polygon"), QStringLiteral("현재 채널의 기존 Camera Area"),
         QStringLiteral("#76ff03"), QStringLiteral("solid")},
        {QStringLiteral("청록 polygon"), QStringLiteral("현재 선택한 Camera Area"),
         QStringLiteral("#00e5ff"), QStringLiteral("solid")},
        {QStringLiteral("노란 점선"), QStringLiteral("새로 그리거나 다시 그린 draft / 선택 ROI"),
         QStringLiteral("#ffca28"), QStringLiteral("dashed")},
        {QStringLiteral("초록 실선 ROI"), QStringLiteral("Pi에서 조회된 저장 ROI"),
         QStringLiteral("#76ff03"), QStringLiteral("solid")}
    };
    for (int index = 0; index < marks.size(); ++index) {
        const CanvasMark &mark = marks.at(index);
        auto *card = new QFrame(canvasLegend);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:white;border:1px solid #cfd8dc;border-radius:6px; }"));
        auto *cardLayout = new QHBoxLayout(card);
        auto *swatch = new QLabel(card);
        swatch->setFixedSize(38, 24);
        swatch->setStyleSheet(QStringLiteral(
            "background:#263238;border:3px %1 %2;border-radius:3px;")
                                  .arg(mark.borderStyle, mark.color));
        auto *textLayout = new QVBoxLayout;
        textLayout->setSpacing(0);
        auto *markName = new QLabel(mark.name, card);
        markName->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:900;font-size:11px;"));
        auto *markMeaning = new QLabel(mark.meaning, card);
        markMeaning->setWordWrap(true);
        markMeaning->setStyleSheet(QStringLiteral(
            "border:none;color:#607d8b;font-size:10px;"));
        textLayout->addWidget(markName);
        textLayout->addWidget(markMeaning);
        cardLayout->addWidget(swatch);
        cardLayout->addLayout(textLayout, 1);
        legendLayout->addWidget(card, index / 2, index % 2);
    }
    canvasLayout->addWidget(canvasLegend);
    auto *editorHint = new QLabel(
        QStringLiteral("Area를 선택하면 오른쪽 Selected Area Details에서 Channel enabled, index/name, Detection modes, "
                       "Object filters, Appearance/Intrusion/Loitering 시간과 camera pixel 좌표를 편집합니다. "
                       "새 사각형은 4개 좌표점으로 만들어지며 저장 전에는 draft입니다."),
        canvasGroup);
    editorHint->setObjectName(QStringLiteral("ivaHelpEditorFields"));
    editorHint->setWordWrap(true);
    canvasLayout->addWidget(editorHint);
    layout->addWidget(canvasGroup);

    auto *actionGroup = new QGroupBox(
        QStringLiteral("5. 버튼별 결과와 검증 흐름"), content);
    auto *actionLayout = new QVBoxLayout(actionGroup);
    auto *actionMatrix = new QWidget(actionGroup);
    actionMatrix->setObjectName(QStringLiteral("ivaHelpActionMatrix"));
    auto *actionGrid = new QGridLayout(actionMatrix);
    actionGrid->setContentsMargins(0, 0, 0, 0);
    actionGrid->setHorizontalSpacing(8);
    actionGrid->setVerticalSpacing(6);
    const QList<QPair<QString, QString>> actions{
        {QStringLiteral("Discard draft"), QStringLiteral("저장 전 새 영역 또는 다시 그린 영역을 버리고 이전 Camera Area로 복원")},
        {QStringLiteral("Delete selected Area"), QStringLiteral("확인 후 선택 Area를 카메라 전용 삭제 API로 제거하고 재조회 검증")},
        {QStringLiteral("Save changes"), QStringLiteral("변경된 Camera WiseAI와 선택한 Pi Crop ROI를 한 번에 확인하고 순차 저장")},
        {QStringLiteral("Automatic refresh"), QStringLiteral("페이지가 표시된 동안 10초마다 카메라의 최신 Options, Capability, Configuration을 읽음")}
    };
    for (int row = 0; row < actions.size(); ++row) {
        auto *actionName = new QLabel(actions.at(row).first, actionMatrix);
        actionName->setStyleSheet(QStringLiteral(
            "background:#eceff1;color:#263238;border-radius:4px;padding:7px;font-weight:900;"));
        auto *actionMeaning = new QLabel(actions.at(row).second, actionMatrix);
        actionMeaning->setWordWrap(true);
        actionMeaning->setStyleSheet(QStringLiteral("color:#455a64;padding:4px;"));
        actionGrid->addWidget(actionName, row, 0);
        actionGrid->addWidget(actionMeaning, row, 1);
    }
    actionGrid->setColumnStretch(1, 1);
    actionLayout->addWidget(actionMatrix);

    auto *verificationFlow = new QWidget(actionGroup);
    verificationFlow->setObjectName(QStringLiteral("ivaHelpVerificationFlow"));
    auto *verificationLayout = new QVBoxLayout(verificationFlow);
    verificationLayout->setContentsMargins(0, 4, 0, 0);
    auto *cameraFlow = new QLabel(
        QStringLiteral("통합 저장  확인 → Camera 충돌 검사·PUT·재조회 검증 → 성공한 경우에만 Pi ROI 저장 시작"),
        verificationFlow);
    cameraFlow->setWordWrap(true);
    cameraFlow->setStyleSheet(QStringLiteral(
        "background:#fff8e1;color:#5d4037;border:1px solid #ffe082;border-radius:6px;padding:9px;font-weight:700;"));
    auto *piFlow = new QLabel(
        QStringLiteral("Pi 저장  nameN/index N 매핑 → polygon bounding rectangle → 0~1 정규화·최소 크기 검사 → 저장 요청 → 응답 generation 검증"),
        verificationFlow);
    piFlow->setWordWrap(true);
    piFlow->setStyleSheet(QStringLiteral(
        "background:#e8f5e9;color:#1b5e20;border:1px solid #a5d6a7;border-radius:6px;padding:9px;font-weight:700;"));
    verificationLayout->addWidget(cameraFlow);
    verificationLayout->addWidget(piFlow);
    actionLayout->addWidget(verificationFlow);
    layout->addWidget(actionGroup);

    auto *safetyNotes = new QLabel(
        QStringLiteral(
            "문제가 생겼을 때\n"
            "• Save changes 결과를 검증할 수 없다는 메시지가 나오면 추가 편집 전에 자동 조회가 끝난 뒤 상태를 확인합니다.\n"
            "• 카메라가 다른 곳에서 변경되어 preflight 충돌이 나면 새로 읽힌 값을 검토한 뒤 다시 적용합니다.\n"
            "• TLS 인증서는 최초 연결 시 SHA-256 지문으로 고정되며 이후 지문이 다르면 연결을 중단합니다. 평문으로 자동 전환하지 않습니다.\n"
            "• 카메라 설정 삭제는 Camera Web Viewer에도 반영되므로 채널과 Area index/name을 확인한 후 승인합니다.\n"
            "• 슬롯 선택은 Parking Map의 채널/IVA Area 매핑을 따르며 Pi ROI 저장은 이 화면 내부 옵션입니다."),
        content);
    safetyNotes->setObjectName(QStringLiteral("ivaHelpSafetyNotes"));
    safetyNotes->setWordWrap(true);
    safetyNotes->setStyleSheet(QStringLiteral(
        "background:#ffebee;color:#7f1d1d;border:1px solid #ef9a9a;"
        "border-radius:7px;padding:11px;"));
    layout->addWidget(safetyNotes);
    layout->addStretch();

    scrollArea->setWidget(content);
    dialogLayout->addWidget(scrollArea, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(QStringLiteral("ivaHelpButtons"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialogLayout->addWidget(buttons);
    dialog->open();
}

bool IvaSettingsPage::hasPendingChanges() const
{
    return m_cameraDirty || m_piDirty;
}

IvaSettingsPage::PendingChangesDecision IvaSettingsPage::confirmPendingChanges()
{
    if (!hasPendingChanges()) return PendingChangesDecision::Proceed;
    if (m_requestInFlight || m_piRoiRequestInFlight
        || m_saveStage != SaveStage::Idle) {
        QMessageBox::information(
            this, QStringLiteral("IVA save in progress"),
            QStringLiteral("IVA settings are still being saved. Wait for the result before leaving this page."));
        return PendingChangesDecision::Cancel;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, QStringLiteral("Unsaved IVA changes"),
        QStringLiteral("IVA settings were changed but not saved.\n\nSave them before leaving this page?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Discard) {
        discardPendingChanges();
        return PendingChangesDecision::Proceed;
    }
    if (choice != QMessageBox::Save) return PendingChangesDecision::Cancel;

    m_pendingLeaveAfterSave = true;
    if (startSaveChangesInternal(false)) {
        return PendingChangesDecision::Waiting;
    }
    m_pendingLeaveAfterSave = false;
    return PendingChangesDecision::Cancel;
}

void IvaSettingsPage::discardPendingChanges()
{
    if (!m_hasSavedConfiguration || m_requestInFlight
        || m_piRoiRequestInFlight || m_saveStage != SaveStage::Idle) {
        return;
    }
    m_configuration = m_savedConfiguration;
    m_draftChannel = -1;
    m_draftAreaIndex = -1;
    m_draftReplacesExisting = false;
    m_draftOriginalArea = {};
    m_cameraDirty = false;
    m_piDirty = false;
    m_pendingLeaveAfterSave = false;
    populateAreaTable();
    selectMappedParkingArea();
    updateVideoOverlays();
    setCameraSaveFeedback(
        QStringLiteral("Unsaved IVA changes were discarded."),
        QStringLiteral("color:#455a64;font-weight:700;"));
    updateButtons();
}

void IvaSettingsPage::setCameraIp(const QString &cameraIp)
{
    const QString normalized = cameraIp.trimmed();
    if (!m_cameraIp.isEmpty() && normalized != m_cameraIp) {
        m_loadedOnce = false;
        m_hasOptions = false;
        m_hasCapabilities = false;
        m_configuration = {};
        m_savedConfiguration = {};
        m_hasSavedConfiguration = false;
        m_options = {};
        m_capabilities = {};
        m_draftChannel = -1;
        m_draftAreaIndex = -1;
        m_cameraDirty = false;
        m_piDirty = false;
        m_saveStage = SaveStage::Idle;
        populateAreaTable();
        m_channelSummaryLabel->setText(QStringLiteral("Not loaded"));
        clearEditor();
        setCameraSaveFeedback(
            QStringLiteral("Camera changed. Refresh before editing WiseAI Areas."),
            QStringLiteral("color:#455a64;"));
    }
    m_cameraIp = normalized;
    m_cameraLabel->setText(
        normalized.isEmpty()
            ? QStringLiteral("Camera: not configured")
            : QStringLiteral("Camera: https://%1").arg(normalized));
    m_refreshButton->setToolTip(
        normalized.isEmpty()
            ? QStringLiteral("Reload WiseAI configuration")
            : QStringLiteral("Reload WiseAI configuration from https://%1")
                  .arg(normalized));
}

void IvaSettingsPage::startSaveChanges()
{
    startSaveChangesInternal(true);
}

void IvaSettingsPage::setParkingZoneMappings(const QList<ParkingZoneLayout> &mappings)
{
    const QString previousZoneId = !m_selectedParkingZoneId.isEmpty()
        ? m_selectedParkingZoneId
        : (m_piSlotCombo ? m_piSlotCombo->currentText() : QString());
    m_parkingZoneMappings = mappings;

    if (!m_piSlotCombo) return;
    {
        const QSignalBlocker blocker(m_piSlotCombo);
        m_piSlotCombo->clear();
        for (const ParkingZoneLayout &mapping : m_parkingZoneMappings) {
            if (!mapping.zoneId.trimmed().isEmpty()) {
                m_piSlotCombo->addItem(mapping.zoneId.trimmed().toUpper(),
                                       mapping.zoneId.trimmed().toUpper());
            }
        }
        int selectedIndex = m_piSlotCombo->findText(previousZoneId,
                                                    Qt::MatchFixedString);
        if (selectedIndex < 0 && m_piSlotCombo->count() > 0) selectedIndex = 0;
        if (selectedIndex >= 0) m_piSlotCombo->setCurrentIndex(selectedIndex);
    }

    const ParkingZoneLayout *mapping = parkingZoneMapping(m_piSlotCombo->currentText());
    if (mapping) {
        m_selectedParkingZoneId = mapping->zoneId;
        m_selectedParkingChannel = mapping->cameraChannel;
        m_selectedParkingIvaAreaId = mapping->ivaAreaId;
        const int channel = cameraChannelIndex(mapping->cameraChannel);
        if (channel >= 0 && channel != m_selectedChannel) selectChannel(channel);
    } else {
        m_selectedParkingZoneId.clear();
        m_selectedParkingChannel.clear();
        m_selectedParkingIvaAreaId.clear();
    }
    selectMappedParkingArea();
    updateButtons();
}

void IvaSettingsPage::setParkingSelection(const QString &zoneId,
                                          const QString &cameraChannel,
                                          const QString &ivaAreaId)
{
    const QString normalizedZoneId = zoneId.trimmed().toUpper();
    const ParkingZoneLayout *mapping = parkingZoneMapping(normalizedZoneId);
    m_selectedParkingZoneId = normalizedZoneId;
    m_selectedParkingChannel = cameraChannel.trimmed().toUpper();
    m_selectedParkingIvaAreaId = ivaAreaId.trimmed().toUpper();
    if (mapping) {
        m_selectedParkingChannel = mapping->cameraChannel;
        m_selectedParkingIvaAreaId = mapping->ivaAreaId;
    }

    if (m_piSlotCombo) {
        const QSignalBlocker blocker(m_piSlotCombo);
        const int index = m_piSlotCombo->findText(normalizedZoneId,
                                                  Qt::MatchFixedString);
        if (index >= 0) {
            m_piSlotCombo->setCurrentIndex(index);
        } else if (!normalizedZoneId.isEmpty()) {
            m_piSlotCombo->addItem(normalizedZoneId, normalizedZoneId);
            m_piSlotCombo->setCurrentText(normalizedZoneId);
        }
    }

    const int channel = cameraChannelIndex(m_selectedParkingChannel);
    if (channel >= 0 && channel != m_selectedChannel) selectChannel(channel);
    selectMappedParkingArea();
    updateButtons();
}

bool IvaSettingsPage::startSaveChangesInternal(bool askConfirmation)
{
    if (m_requestInFlight || m_piRoiRequestInFlight
        || m_saveStage != SaveStage::Idle) return false;

    const bool saveCamera = m_cameraDirty;
    const bool savePi = (m_cameraDirty || m_piDirty)
        && editorMatchesParkingArea() && m_currentPreviewFrameSize.isValid();
    if (!saveCamera && !savePi) return false;

    IvaAreaDefinition edited;
    QString errorMessage;
    if (!collectEditedArea(edited, errorMessage)) {
        if (askConfirmation) {
            QMessageBox::warning(this, QStringLiteral("IVA validation"), errorMessage);
        }
        return false;
    }
    QStringList destinations;
    if (saveCamera) destinations.append(QStringLiteral("Camera WiseAI"));
    if (savePi) destinations.append(QStringLiteral("Pi Crop ROI (%1)")
                                        .arg(m_piSlotCombo->currentText()));
    if (askConfirmation) {
        const QMessageBox::StandardButton result = QMessageBox::question(
            this, QStringLiteral("Save IVA changes"),
            QStringLiteral("Save CH%1 Area %2 (%3) to:\n\n%4\n\n"
                           "Camera verification completes before Pi ROI saving starts.")
                .arg(edited.channel + 1).arg(edited.areaIndex).arg(edited.name)
                .arg(destinations.join(QStringLiteral("\n"))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) return false;
    }

    m_continueWithPiAfterCamera = saveCamera && savePi;
    m_cameraSavedBeforePi = false;
    if (!saveCamera) {
        return requestPiRoiSave();
    }

    QList<IvaAreaDefinition> channelAreas;
    for (int index = 0; index < m_configuration.areas.size(); ++index) {
        const IvaAreaDefinition &area = m_configuration.areas.at(index);
        if (area.channel != edited.channel) continue;
        channelAreas.append(index == m_selectedArea ? edited : area);
    }
    m_saveStage = SaveStage::Camera;
    updateButtons();
    emit applyRequested(edited.channel, m_channelEnabledCheck->isChecked(),
                        channelAreas);
    return true;
}

void IvaSettingsPage::finishPendingSave(bool success)
{
    if (!m_pendingLeaveAfterSave) return;
    m_pendingLeaveAfterSave = false;
    if (success) emit pendingChangesSaved();
}

bool IvaSettingsPage::requestPiRoiSave()
{
    if (m_piRoiRequestInFlight) return false;
    IvaAreaDefinition edited;
    QString errorMessage;
    auto fail = [this](const QString &message) {
        m_saveStage = SaveStage::Idle;
        m_piRoiStatusLabel->setText(message);
        m_piRoiStatusLabel->setStyleSheet(
            QStringLiteral("color:#b71c1c;font-weight:700;"));
        updateButtons();
        return false;
    };
    if (!collectEditedArea(edited, errorMessage)) return fail(errorMessage);
    if (!editorMatchesParkingArea()) {
        return fail(QStringLiteral("The selected IVA rule does not match %1 (%2 / Area index %3).")
                        .arg(m_piSlotCombo->currentText(), mappedParkingAreaName())
                        .arg(mappedParkingAreaIndex()));
    }
    const IvaChannelCapability *capability = m_capabilities.forChannel(edited.channel);
    if (!capability || !capability->maxResolution.isValid()
        || edited.areaCoordinates.size() < 3) {
        return fail(QStringLiteral("The selected IVA rule does not provide a valid polygon or coordinate resolution."));
    }
    QPolygonF polygon;
    for (const QPointF &point : edited.areaCoordinates) polygon.append(point);
    const ParkingRoi roi = ParkingRoi::fromRectangle(
        IvaVideoCanvas::normalizedFromSource(polygon.boundingRect(),
                                             capability->maxResolution));
    if (!roi.isValid(&errorMessage)) return fail(errorMessage);
    if (!m_currentPreviewFrameSize.isValid()) {
        return fail(QStringLiteral("Unable to validate the ROI because the source frame size is unavailable."));
    }
    if (!roi.isLargeEnough(m_currentPreviewFrameSize)) {
        return fail(QStringLiteral("The selected area is too small. Select an area of at least 8 x 8 pixels."));
    }
    ++m_piRoiGeneration;
    m_pendingPiSlotId = m_piSlotCombo->currentText();
    m_piRoiRequestInFlight = true;
    m_saveStage = SaveStage::Pi;
    m_piRoiStatusLabel->setText(
        QStringLiteral("Saving %1 normalized crop ROI to the Pi server...")
            .arg(m_pendingPiSlotId));
    m_piRoiStatusLabel->setStyleSheet(QStringLiteral("color:#0d47a1;font-weight:700;"));
    updateButtons();
    emit piRoiSaveRequested(m_pendingPiSlotId, roi, m_piRoiGeneration);
    return true;
}

void IvaSettingsPage::setRequestStarted()
{
    m_requestInFlight = true;
    m_statusLabel->setText(QStringLiteral(
        "Loading WiseAI IVA options, capabilities, and configuration..."));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
    updateButtons();
}

void IvaSettingsPage::setOptions(const IvaAreaOptions &options)
{
    m_options = options;
    m_hasOptions = true;
    if (m_selectedArea >= 0) {
        populateEditor(m_areaTable->currentRow());
    }
    updateButtons();
}

void IvaSettingsPage::setCapabilities(
    const WiseAiCapabilities &capabilities)
{
    m_capabilities = capabilities;
    m_hasCapabilities = true;
    selectChannel(m_selectedChannel);
    updateButtons();
}

void IvaSettingsPage::setConfiguration(
    const IvaAreaConfiguration &configuration)
{
    const bool continuingPiSave = m_saveStage == SaveStage::Camera
        && m_continueWithPiAfterCamera;
    m_configuration = configuration;
    m_savedConfiguration = configuration;
    m_hasSavedConfiguration = true;
    m_draftChannel = -1;
    m_draftAreaIndex = -1;
    m_draftReplacesExisting = false;
    m_draftOriginalArea = {};
    populateAreaTable();
    selectMappedParkingArea();
    updateVideoOverlays();

    QStringList channels;
    for (const IvaChannelDefinition &channel : configuration.channels) {
        channels.append(QStringLiteral("CH%1 %2 (%3 areas)")
                            .arg(channel.channel + 1)
                            .arg(channel.enabled ? QStringLiteral("ON")
                                                 : QStringLiteral("OFF"))
                            .arg(channel.rawContainer
                                     .value(QStringLiteral("definedArea"))
                                     .toArray().size()));
    }
    m_channelSummaryLabel->setText(channels.join(QStringLiteral("  |  ")));
    m_requestInFlight = false;
    m_loadedOnce = true;
    m_cameraDirty = false;
    if (!continuingPiSave) {
        m_piDirty = false;
    }
    m_statusLabel->setText(
        configuration.warnings.isEmpty()
            ? QStringLiteral("Camera configuration loaded and validated.")
            : QStringLiteral("Loaded with warnings: %1")
                  .arg(configuration.warnings.join(QStringLiteral(" | "))));
    m_statusLabel->setStyleSheet(
        configuration.warnings.isEmpty()
            ? QStringLiteral("color:#1b5e20;font-weight:700;")
            : QStringLiteral("color:#e65100;font-weight:700;"));
    updateButtons();
}

void IvaSettingsPage::setPreviewFrame(int channel, const QImage &frame)
{
    if (channel != m_selectedChannel || !m_videoCanvas) return;
    if (!frame.isNull()) m_currentPreviewFrameSize = frame.size();
    m_videoCanvas->setFrame(frame);
}

void IvaSettingsPage::setRequestError(const QString &message)
{
    m_requestInFlight = false;
    m_statusLabel->setText(
        QStringLiteral("Failed to load camera configuration. Last-good view retained.\n%1")
            .arg(message));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
    updateButtons();
}

void IvaSettingsPage::setApplyStarted(int channel)
{
    m_requestInFlight = true;
    m_saveStage = SaveStage::Camera;
    m_statusLabel->setText(
        QStringLiteral("Applying CH%1: conflict check -> PUT -> camera verification...")
            .arg(channel + 1));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#0d47a1;font-weight:700;"));
    setCameraSaveFeedback(
        QStringLiteral("Saving CH%1 WiseAI Area to the camera...").arg(channel + 1),
        QStringLiteral("color:#0d47a1;font-weight:700;"));
    setEditorState(QStringLiteral("SAVING"), QStringLiteral("#e3f2fd"),
                   QStringLiteral("#0d47a1"));
    updateButtons();
}

void IvaSettingsPage::setApplySuccess(
    int channel,
    const IvaAreaConfiguration &verifiedConfiguration)
{
    m_requestInFlight = false;
    const bool deletedArea = m_pendingDeletedAreaIndex >= 0;
    m_cameraDirty = false;
    m_configuration = verifiedConfiguration;
    m_savedConfiguration = verifiedConfiguration;
    m_hasSavedConfiguration = true;
    m_draftChannel = -1;
    m_draftAreaIndex = -1;
    m_draftReplacesExisting = false;
    m_draftOriginalArea = {};
    populateAreaTable();
    selectMappedParkingArea();
    updateVideoOverlays();
    if (deletedArea) {
        m_statusLabel->setText(
            QStringLiteral("CH%1 IVA Area %2 (%3) was deleted and verified from the camera. "
                           "The Area is now removed from the camera Web Viewer.")
                .arg(channel + 1)
                .arg(m_pendingDeletedAreaIndex)
                .arg(m_pendingDeletedAreaName));
        m_pendingDeletedAreaIndex = -1;
        m_pendingDeletedAreaName.clear();
    } else {
        m_statusLabel->setText(
            QStringLiteral("CH%1 IVA settings applied and verified from the camera. "
                           "Raspberry Pi configuration refresh is still pending.")
                .arg(channel + 1));
    }
    m_statusLabel->setStyleSheet(QStringLiteral("color:#1b5e20;font-weight:700;"));
    setCameraSaveFeedback(
        deletedArea
            ? QStringLiteral("Camera WiseAI Area deletion verified for CH%1. Pi Crop ROI was not changed.")
                  .arg(channel + 1)
            : (m_continueWithPiAfterCamera
                   ? QStringLiteral("Camera WiseAI save verified for CH%1. Continuing with Pi Crop ROI...")
                         .arg(channel + 1)
                   : QStringLiteral("Camera WiseAI save verified for CH%1. Pi Crop ROI was not changed.")
                         .arg(channel + 1)),
        QStringLiteral("color:#1b5e20;font-weight:700;"));
    if (!deletedArea && m_continueWithPiAfterCamera) {
        m_continueWithPiAfterCamera = false;
        m_cameraSavedBeforePi = true;
        if (requestPiRoiSave()) {
            setEditorState(QStringLiteral("SAVING"), QStringLiteral("#e3f2fd"),
                           QStringLiteral("#0d47a1"));
            return;
        }
    }
    m_saveStage = SaveStage::Idle;
    if (deletedArea || m_selectedArea < 0) {
        setEditorState(QStringLiteral("NO SELECTION"), QStringLiteral("#eceff1"),
                       QStringLiteral("#607d8b"));
    } else {
        setEditorState(QStringLiteral("SAVED"), QStringLiteral("#e8f5e9"),
                       QStringLiteral("#1b5e20"));
    }
    finishPendingSave(true);
    updateButtons();
}

void IvaSettingsPage::setApplyError(int channel,
                                    const QString &message,
                                    bool rollbackSucceeded)
{
    m_requestInFlight = false;
    m_saveStage = SaveStage::Idle;
    m_continueWithPiAfterCamera = false;
    m_cameraSavedBeforePi = false;
    const bool deletingArea = m_pendingDeletedAreaIndex >= 0;
    const QString prefix = channel < 0
        ? (deletingArea ? QStringLiteral("IVA Area deletion failed")
                        : QStringLiteral("IVA apply failed"))
        : (deletingArea
               ? QStringLiteral("CH%1 IVA Area deletion failed").arg(channel + 1)
               : QStringLiteral("CH%1 IVA apply failed").arg(channel + 1));
    m_pendingDeletedAreaIndex = -1;
    m_pendingDeletedAreaName.clear();
    m_statusLabel->setText(
        rollbackSucceeded
            ? QStringLiteral("%1; last-good camera state was restored.\n%2")
                  .arg(prefix, message)
            : QStringLiteral("%1. Refresh before another edit.\n%2")
                  .arg(prefix, message));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
    setCameraSaveFeedback(
        rollbackSucceeded
            ? QStringLiteral("%1. Last-good camera state was restored.").arg(prefix)
            : QStringLiteral("%1. Refresh camera state before editing again.").arg(prefix),
        QStringLiteral("color:#b71c1c;font-weight:700;"));
    setEditorState(QStringLiteral("FAILED"), QStringLiteral("#ffebee"),
                   QStringLiteral("#b71c1c"));
    finishPendingSave(false);
    updateButtons();
}

void IvaSettingsPage::setPiRoiResult(const QString &slotId,
                                     const ParkingRoi &roi,
                                     quint64 generation,
                                     bool afterSave,
                                     bool appliedImmediately)
{
    Q_UNUSED(roi)
    if (!m_piRoiRequestInFlight || !afterSave
        || generation != m_piRoiGeneration || slotId != m_pendingPiSlotId) {
        return;
    }
    m_piRoiRequestInFlight = false;
    m_piDirty = false;
    m_saveStage = SaveStage::Idle;
    m_cameraSavedBeforePi = false;
    m_piRoiStatusLabel->setText(
        appliedImmediately
            ? QStringLiteral("%1 crop ROI was saved, verified, and applied immediately on the Pi server.").arg(slotId)
            : QStringLiteral("%1 crop ROI was saved and verified on the Pi server.").arg(slotId));
    m_piRoiStatusLabel->setStyleSheet(
        QStringLiteral("color:#1b5e20;font-weight:700;"));
    setEditorState(QStringLiteral("SAVED"), QStringLiteral("#e8f5e9"),
                   QStringLiteral("#1b5e20"));
    finishPendingSave(true);
    updateButtons();
}

void IvaSettingsPage::setPiRoiError(const QString &slotId,
                                    const QString &message,
                                    quint64 generation,
                                    bool saveRequest)
{
    if (!m_piRoiRequestInFlight || !saveRequest
        || generation != m_piRoiGeneration || slotId != m_pendingPiSlotId) {
        return;
    }
    m_piRoiRequestInFlight = false;
    m_saveStage = SaveStage::Idle;
    const bool partialSave = m_cameraSavedBeforePi;
    m_cameraSavedBeforePi = false;
    m_piRoiStatusLabel->setText(
        partialSave
            ? QStringLiteral("Camera WiseAI was saved, but %1 crop ROI failed: %2")
                  .arg(slotId, message)
            : QStringLiteral("Failed to save %1 crop ROI on the Pi server: %2")
                  .arg(slotId, message));
    m_piRoiStatusLabel->setStyleSheet(
        QStringLiteral("color:#b71c1c;font-weight:700;"));
    setEditorState(partialSave ? QStringLiteral("PARTIAL")
                               : QStringLiteral("FAILED"),
                   QStringLiteral("#ffebee"), QStringLiteral("#b71c1c"));
    finishPendingSave(false);
    updateButtons();
}

void IvaSettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_previewTimer) {
        m_previewTimer->start();
        emit previewFrameRequested(m_selectedChannel);
    }
    if (m_refreshTimer) m_refreshTimer->start();
    if (m_loadedOnce || m_requestInFlight) return;
    setRequestStarted();
    emit refreshRequested();
}

void IvaSettingsPage::hideEvent(QHideEvent *event)
{
    if (m_previewTimer) m_previewTimer->stop();
    if (m_refreshTimer) m_refreshTimer->stop();
    QWidget::hideEvent(event);
}

QString IvaSettingsPage::durationText(const IvaAreaDefinition &area)
{
    auto value = [](int seconds) {
        return seconds < 0 ? QStringLiteral("-") : QString::number(seconds);
    };
    return QStringLiteral("A:%1 / I:%2 / L:%3")
        .arg(value(area.appearanceDuration), value(area.intrusionDuration),
             value(area.loiteringDuration));
}

QString IvaSettingsPage::coordinateText(double value)
{
    return std::floor(value) == value
        ? QString::number(static_cast<qint64>(value))
        : QString::number(value, 'f', 3);
}

void IvaSettingsPage::populateAreaTable()
{
    m_areaTable->setRowCount(m_configuration.areas.size());
    if (m_areaCountLabel) {
        m_areaCountLabel->setText(
            QStringLiteral("%1 area%2")
                .arg(m_configuration.areas.size())
                .arg(m_configuration.areas.size() == 1
                         ? QString() : QStringLiteral("s")));
    }
    for (int row = 0; row < m_configuration.areas.size(); ++row) {
        const IvaAreaDefinition &area = m_configuration.areas.at(row);
        const QString modes = area.detectionModes.isEmpty()
            ? QStringLiteral("-")
            : area.detectionModes.join(QStringLiteral(", "));
        QStringList objectIcons = area.objectTypeFilter;
        if (objectIcons.isEmpty()) {
            if (const IvaChannelOptions *options = m_options.forChannel(area.channel)) {
                objectIcons = options->objectTypeFilters;
            }
        }
        const QStringList objectNames = [&objectIcons]() {
            QStringList names;
            for (const QString &object : objectIcons) {
                names.append(objectDisplayName(object));
            }
            return names;
        }();
        const QString objectDescription = area.objectTypeFilter.isEmpty()
            ? QStringLiteral("All objects: %1").arg(
                  objectNames.isEmpty() ? QStringLiteral("camera defaults")
                                        : objectNames.join(QStringLiteral(", ")))
            : objectNames.join(QStringLiteral(", "));
        const QStringList values{
            QStringLiteral("CH%1").arg(area.channel + 1),
            area.areaIndex < 0 ? QStringLiteral("-") : QString::number(area.areaIndex),
            area.name,
            modes,
            QString(),
            area.channelEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            if (column == 4) {
                item->setIcon(objectIconStrip(objectIcons));
                item->setData(Qt::AccessibleTextRole, objectDescription);
                item->setTextAlignment(Qt::AlignCenter);
            }
            item->setToolTip(
                column == 2
                    ? QStringLiteral("%1 / %2 points / %3")
                          .arg(area.name)
                          .arg(area.areaCoordinates.size())
                          .arg(durationText(area))
                    : (column == 4 ? objectDescription : values.at(column)));
            if (column == 5) {
                item->setTextAlignment(Qt::AlignCenter);
                item->setForeground(area.channelEnabled
                                        ? QColor(QStringLiteral("#1b5e20"))
                                        : QColor(QStringLiteral("#b71c1c")));
            } else if (column == 0 || column == 1) {
                item->setTextAlignment(Qt::AlignCenter);
            }
            m_areaTable->setItem(row, column, item);
        }
    }
    updateChannelButtons();
}

void IvaSettingsPage::populateEditor(int row)
{
    if (row < 0 || row >= m_configuration.areas.size()) {
        clearEditor();
        return;
    }
    m_updatingEditor = true;
    m_selectedArea = row;
    const IvaAreaDefinition &area = m_configuration.areas.at(row);
    if (area.channel != m_selectedChannel) {
        selectChannel(area.channel);
    }
    m_videoCanvas->setSelectedAreaIndex(area.areaIndex);
    selectParkingZoneForArea(area.channel, area.areaIndex);
    const IvaChannelOptions *options = m_options.forChannel(area.channel);
    m_channelEnabledCheck->setChecked(area.channelEnabled);
    if (options) {
        m_indexSpin->setRange(options->areaIndex.minimum, options->areaIndex.maximum);
        m_appearanceDurationSpin->setRange(options->appearanceDuration.minimum,
                                           options->appearanceDuration.maximum);
        m_intrusionDurationSpin->setRange(options->intrusionDuration.minimum,
                                          options->intrusionDuration.maximum);
        m_loiteringDurationSpin->setRange(options->loiteringDuration.minimum,
                                          options->loiteringDuration.maximum);
        populateChecklist(m_detectionModesList, options->detectionModes,
                          area.detectionModes);
        populateChecklist(m_objectFiltersList, options->objectTypeFilters,
                          area.objectTypeFilter, true);
    } else {
        m_indexSpin->setRange(0, 999);
        populateChecklist(m_detectionModesList, area.detectionModes,
                          area.detectionModes);
        populateChecklist(m_objectFiltersList, area.objectTypeFilter,
                          area.objectTypeFilter);
    }
    m_indexSpin->setValue(area.areaIndex);
    m_nameEdit->setText(area.name);
    m_appearanceDurationSpin->setValue(qMax(0, area.appearanceDuration));
    m_intrusionDurationSpin->setValue(qMax(0, area.intrusionDuration));
    m_loiteringDurationSpin->setValue(qMax(0, area.loiteringDuration));
    m_coordinateTable->setRowCount(0);
    for (const QPointF &point : area.areaCoordinates) {
        addCoordinateRow(point.x(), point.y());
    }
    m_updatingEditor = false;
    updateDurationAvailability();
    if (m_areaMappingLabel) {
        m_areaMappingLabel->setText(
            editorMatchesParkingArea()
                ? QStringLiteral("Mapped to %1").arg(m_piSlotCombo->currentText())
                : QStringLiteral("No parking slot mapping"));
    }
    updateEditorSummary();
    const bool unsavedDraft = m_draftChannel == area.channel
        && m_draftAreaIndex == area.areaIndex;
    if (unsavedDraft) {
        setEditorState(QStringLiteral("UNSAVED"), QStringLiteral("#fff3e0"),
                       QStringLiteral("#e65100"));
    } else {
        setEditorState(QStringLiteral("SAVED"), QStringLiteral("#e8f5e9"),
                       QStringLiteral("#1b5e20"));
    }
    updateButtons();
    setCameraSaveFeedback(
        QStringLiteral("Loaded CH%1 Area %2 (%3). Edit fields or drag on the video, then save to the camera.")
            .arg(area.channel + 1)
            .arg(area.areaIndex)
            .arg(area.name),
        QStringLiteral("color:#455a64;"));
}

void IvaSettingsPage::clearEditor()
{
    m_selectedArea = -1;
    m_channelEnabledCheck->setChecked(false);
    m_indexSpin->setValue(0);
    m_nameEdit->clear();
    m_detectionModesList->clear();
    m_objectFiltersList->clear();
    static_cast<UniformChecklistWidget *>(m_detectionModesList)->synchronizeGrid();
    static_cast<UniformChecklistWidget *>(m_objectFiltersList)->synchronizeGrid();
    m_coordinateTable->setRowCount(0);
    updateDurationAvailability();
    if (m_areaMappingLabel) {
        m_areaMappingLabel->setText(QStringLiteral("No parking slot mapping"));
    }
    updateEditorSummary();
    setEditorState(QStringLiteral("NO SELECTION"), QStringLiteral("#eceff1"),
                   QStringLiteral("#607d8b"));
    updateButtons();
    setCameraSaveFeedback(
        QStringLiteral("No WiseAI Area selected for camera save."),
        QStringLiteral("color:#455a64;"));
}

void IvaSettingsPage::populateChecklist(QListWidget *list,
                                        const QStringList &available,
                                        const QStringList &selected,
                                        bool emptyMeansAll)
{
    list->clear();
    const bool selectAll = emptyMeansAll && selected.isEmpty();
    for (const QString &value : available) {
        QString displayValue = value;
        if (displayValue.startsWith(QStringLiteral("Vehicle."))) {
            displayValue = displayValue.section(QLatin1Char('.'), -1);
        }
        auto *item = new QListWidgetItem(displayValue, list);
        item->setData(Qt::UserRole, value);
        item->setIcon(checklistIcon(value, list == m_detectionModesList));
        item->setToolTip(value);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectAll || selected.contains(value)
                                ? Qt::Checked : Qt::Unchecked);
    }
    static_cast<UniformChecklistWidget *>(list)->synchronizeGrid();
}

QStringList IvaSettingsPage::checkedValues(const QListWidget *list) const
{
    QStringList values;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem *item = list->item(row);
        if (item->checkState() == Qt::Checked) {
            const QString rawValue = item->data(Qt::UserRole).toString();
            values.append(rawValue.isEmpty() ? item->text() : rawValue);
        }
    }
    return values;
}

bool IvaSettingsPage::collectEditedArea(IvaAreaDefinition &editedArea,
                                        QString &errorMessage) const
{
    if (m_selectedArea < 0 || m_selectedArea >= m_configuration.areas.size()) {
        errorMessage = QStringLiteral("Select an IVA rule first");
        return false;
    }
    editedArea = m_configuration.areas.at(m_selectedArea);
    editedArea.areaIndex = m_indexSpin->value();
    editedArea.name = m_nameEdit->text().trimmed();
    editedArea.detectionModes = checkedValues(m_detectionModesList);
    editedArea.objectTypeFilter = checkedValues(m_objectFiltersList);
    editedArea.appearanceDuration = m_appearanceDurationSpin->value();
    editedArea.intrusionDuration = m_intrusionDurationSpin->value();
    editedArea.loiteringDuration = m_loiteringDurationSpin->value();
    editedArea.areaCoordinates.clear();
    for (int row = 0; row < m_coordinateTable->rowCount(); ++row) {
        bool xOk = false;
        bool yOk = false;
        const double x = m_coordinateTable->item(row, 0)->text().toDouble(&xOk);
        const double y = m_coordinateTable->item(row, 1)->text().toDouble(&yOk);
        if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y)
            || x < 0.0 || y < 0.0 || std::round(x) != x
            || std::round(y) != y) {
            errorMessage = QStringLiteral(
                "Polygon point %1 requires finite non-negative integer-pixel X and Y")
                               .arg(row + 1);
            return false;
        }
        editedArea.areaCoordinates.append(QPointF(x, y));
    }
    if (editedArea.name.isEmpty()) {
        errorMessage = QStringLiteral("Rule name cannot be empty");
        return false;
    }
    errorMessage.clear();
    return true;
}

void IvaSettingsPage::addCoordinateRow(double x, double y)
{
    const int row = m_coordinateTable->rowCount();
    m_coordinateTable->insertRow(row);
    m_coordinateTable->setItem(row, 0, new QTableWidgetItem(coordinateText(x)));
    m_coordinateTable->setItem(row, 1, new QTableWidgetItem(coordinateText(y)));
    if (!m_updatingEditor) updateEditorSummary();
}

void IvaSettingsPage::selectChannel(int channel)
{
    if (channel < 0) return;
    m_selectedChannel = channel;
    m_selectedParkingChannel = QStringLiteral("CH%1").arg(channel + 1);
    m_currentPreviewFrameSize = {};
    for (int index = 0; index < m_channelButtons.size(); ++index) {
        m_channelButtons.at(index)->setChecked(index == channel);
    }
    const IvaChannelCapability *capability = m_capabilities.forChannel(channel);
    const QSize coordinateResolution = capability && capability->ivaAreaSupported
        ? capability->maxResolution : QSize();
    m_videoCanvas->setChannel(channel, coordinateResolution);
    selectParkingZoneForArea(channel, mappedParkingAreaIndex());
    updateVideoOverlays();
    if (m_previewTimer && m_previewTimer->isActive()) {
        emit previewFrameRequested(channel);
    }
    updateButtons();
}

const ParkingZoneLayout *IvaSettingsPage::parkingZoneMapping(const QString &zoneId) const
{
    const QString normalized = zoneId.trimmed().toUpper();
    for (const ParkingZoneLayout &mapping : m_parkingZoneMappings) {
        if (mapping.zoneId.trimmed().toUpper() == normalized) return &mapping;
    }
    return nullptr;
}

int IvaSettingsPage::cameraChannelIndex(const QString &cameraChannel) const
{
    const QString normalized = cameraChannel.trimmed().toUpper();
    if (!normalized.startsWith(QStringLiteral("CH"))) return -1;
    bool ok = false;
    const int channel = normalized.mid(2).toInt(&ok);
    return ok && channel >= 1 && channel <= 4 ? channel - 1 : -1;
}

void IvaSettingsPage::selectParkingZoneForArea(int channel, int areaIndex)
{
    if (!m_piSlotCombo || channel < 0) return;
    const QString expectedChannel = QStringLiteral("CH%1").arg(channel + 1);
    const QString expectedArea = QStringLiteral("IVA%1").arg(areaIndex);
    for (const ParkingZoneLayout &mapping : m_parkingZoneMappings) {
        if (mapping.cameraChannel.compare(expectedChannel, Qt::CaseInsensitive) != 0
            || areaIndex < 1
            || mapping.ivaAreaId.compare(expectedArea, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QSignalBlocker blocker(m_piSlotCombo);
        m_piSlotCombo->setCurrentText(mapping.zoneId);
        m_selectedParkingZoneId = mapping.zoneId;
        m_selectedParkingChannel = mapping.cameraChannel;
        m_selectedParkingIvaAreaId = mapping.ivaAreaId;
        m_piMappingLabel->setText(
            QStringLiteral("%1 / Area %2")
                .arg(mapping.cameraChannel)
                .arg(areaIndex));
        m_piMappingLabel->setToolTip(
            QStringLiteral("%1 ↔ %2 / Area %3")
                .arg(mapping.zoneId, mapping.cameraChannel)
                .arg(areaIndex));
        return;
    }
    for (const ParkingZoneLayout &mapping : m_parkingZoneMappings) {
        if (areaIndex >= 1
            || mapping.cameraChannel.compare(expectedChannel, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QSignalBlocker blocker(m_piSlotCombo);
        m_piSlotCombo->setCurrentText(mapping.zoneId);
        m_selectedParkingZoneId = mapping.zoneId;
        m_selectedParkingChannel = mapping.cameraChannel;
        m_selectedParkingIvaAreaId = mapping.ivaAreaId;
        m_piMappingLabel->setText(
            QStringLiteral("%1 / Area %2")
                .arg(mapping.cameraChannel)
                .arg(mapping.ivaAreaId.mid(3)));
        m_piMappingLabel->setToolTip(
            QStringLiteral("%1 ↔ %2 / Area %3")
                .arg(mapping.zoneId, mapping.cameraChannel)
                .arg(mapping.ivaAreaId.mid(3)));
        return;
    }
    const QSignalBlocker blocker(m_piSlotCombo);
    m_piSlotCombo->setCurrentIndex(-1);
    m_selectedParkingZoneId.clear();
    m_selectedParkingChannel = expectedChannel;
    m_selectedParkingIvaAreaId.clear();
    m_piMappingLabel->setText(
        QStringLiteral("%1 / no IVA Area").arg(expectedChannel));
    m_piMappingLabel->setToolTip(
        QStringLiteral("No Parking Map IVA Area is mapped to %1").arg(expectedChannel));
}

int IvaSettingsPage::mappedParkingAreaIndex() const
{
    const QString areaId = m_selectedParkingIvaAreaId.trimmed().toUpper();
    if (!areaId.startsWith(QStringLiteral("IVA"))) return -1;
    bool ok = false;
    const int index = areaId.mid(3).toInt(&ok);
    return ok && index >= 1 && index <= 4 ? index : -1;
}

QString IvaSettingsPage::mappedParkingAreaName() const
{
    const int index = mappedParkingAreaIndex();
    return index > 0 ? QStringLiteral("name%1").arg(index) : QString();
}

void IvaSettingsPage::selectMappedParkingArea()
{
    if (!m_areaTable || m_draftChannel >= 0) return;
    const int targetIndex = mappedParkingAreaIndex();
    const QString targetName = mappedParkingAreaName();
    int indexFallbackRow = -1;
    for (int row = 0; row < m_configuration.areas.size(); ++row) {
        const IvaAreaDefinition &area = m_configuration.areas.at(row);
        if (area.channel != m_selectedChannel) continue;
        if (area.name.compare(targetName, Qt::CaseInsensitive) == 0) {
            m_areaTable->selectRow(row);
            populateEditor(row);
            m_statusLabel->setText(QStringLiteral(
                "%1 is mapped to %2. Drag the selected box to move it or a corner to resize it.")
                                       .arg(m_piSlotCombo->currentText(), targetName));
            m_statusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
            return;
        }
        if (area.areaIndex == targetIndex) indexFallbackRow = row;
    }
    if (indexFallbackRow >= 0) {
        m_areaTable->selectRow(indexFallbackRow);
        populateEditor(indexFallbackRow);
        m_statusLabel->setText(QStringLiteral(
            "%1 uses camera Area index %2. Drag directly on the video to map it to %3.")
                                   .arg(m_piSlotCombo->currentText())
                                   .arg(targetIndex)
                                   .arg(targetName));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
        return;
    }

    m_areaTable->clearSelection();
    m_areaTable->setCurrentCell(-1, -1);
    clearEditor();
    m_videoCanvas->setSelectedAreaIndex(-1);
    m_statusLabel->setText(QStringLiteral(
        "%1 has no camera Area yet. Drag directly on the video to create %2.")
                               .arg(m_piSlotCombo->currentText(), targetName));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
    updateButtons();
}

void IvaSettingsPage::updateVideoOverlays()
{
    if (!m_videoCanvas) return;
    m_videoCanvas->setAreas(m_configuration.areas);
    if (m_selectedArea >= 0 && m_selectedArea < m_configuration.areas.size()) {
        const IvaAreaDefinition &area = m_configuration.areas.at(m_selectedArea);
        if (area.channel == m_selectedChannel) {
            m_videoCanvas->setSelectedAreaIndex(area.areaIndex);
        }
    }
}

void IvaSettingsPage::createRectangleDraft(const QRectF &sourceRectangle)
{
    if (m_draftChannel >= 0) {
        m_statusLabel->setText(QStringLiteral(
            "Discard or apply the existing rectangle draft before creating another."));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
        return;
    }
    const IvaChannelOptions *options = m_options.forChannel(m_selectedChannel);
    const IvaChannelCapability *capability = m_capabilities.forChannel(
        m_selectedChannel);
    if (!options || !capability || !capability->ivaAreaSupported
        || !capability->maxResolution.isValid()) {
        m_statusLabel->setText(QStringLiteral(
            "The selected channel does not provide usable IVA options/capability."));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
        return;
    }
    if (!options->areaCoordinateCount.contains(4)) {
        m_statusLabel->setText(QStringLiteral(
            "The selected camera channel does not accept four-point rectangles."));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
        return;
    }

    const QList<QPointF> rectangleCoordinates =
        IvaVideoCanvas::rectangleCoordinates(sourceRectangle);
    const int targetIndex = mappedParkingAreaIndex();
    const QString targetName = mappedParkingAreaName();
    int nameRow = -1;
    int indexRow = -1;
    for (int row = 0; row < m_configuration.areas.size(); ++row) {
        const IvaAreaDefinition &area = m_configuration.areas.at(row);
        if (area.channel != m_selectedChannel) continue;
        if (area.name.compare(targetName, Qt::CaseInsensitive) == 0) nameRow = row;
        if (area.areaIndex == targetIndex) indexRow = row;
    }
    if (nameRow >= 0 && indexRow >= 0 && nameRow != indexRow) {
        m_statusLabel->setText(QStringLiteral(
            "%1 cannot be mapped because %2 and camera Area index %3 belong to different rules.")
                                   .arg(m_piSlotCombo->currentText(), targetName)
                                   .arg(targetIndex));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
        return;
    }
    const int targetRow = nameRow >= 0 ? nameRow : indexRow;
    if (targetRow >= 0) {
        m_statusLabel->setText(QStringLiteral(
            "%1 (%2) already exists. Select another Parking Map slot / IVA Area "
            "to draw a new area, or drag the existing box to edit it.")
                               .arg(m_piSlotCombo->currentText(), targetName));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
        updateButtons();
        return;
    }

    if (!options->areaIndex.contains(targetIndex)) {
        m_statusLabel->setText(QStringLiteral(
            "%1 requires camera Area index %2, but this channel supports %3-%4.")
                                   .arg(m_piSlotCombo->currentText())
                                   .arg(targetIndex)
                                   .arg(options->areaIndex.minimum)
                                   .arg(options->areaIndex.maximum));
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
        return;
    }

    bool channelEnabled = false;
    for (const IvaChannelDefinition &channel : m_configuration.channels) {
        if (channel.channel == m_selectedChannel) {
            channelEnabled = channel.enabled;
            break;
        }
    }
    IvaAreaDefinition draft;
    draft.channel = m_selectedChannel;
    draft.channelEnabled = channelEnabled;
    draft.areaIndex = targetIndex;
    draft.name = targetName;
    draft.areaCoordinates = rectangleCoordinates;
    draft.appearanceDuration = options->appearanceDuration.minimum;
    draft.intrusionDuration = options->intrusionDuration.minimum;
    draft.loiteringDuration = options->loiteringDuration.minimum;
    m_configuration.areas.append(draft);
    m_draftChannel = m_selectedChannel;
    m_draftAreaIndex = targetIndex;
    m_draftReplacesExisting = false;
    m_draftOriginalArea = {};
    populateAreaTable();
    const int row = m_configuration.areas.size() - 1;
    m_areaTable->selectRow(row);
    populateEditor(row);
    updateVideoOverlays();
    m_statusLabel->setText(QStringLiteral(
        "%1 (%2) draft was created. Choose detection modes and object filters, then Save changes.")
                               .arg(m_piSlotCombo->currentText(), targetName));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
    setCameraSaveFeedback(
        QStringLiteral("%1 (%2) is an unsaved Camera WiseAI draft. Use Save changes to write it.")
            .arg(m_piSlotCombo->currentText(), targetName),
        QStringLiteral("color:#e65100;font-weight:700;"));
    setEditorDirtyFeedback(true);
    updateButtons();
}

void IvaSettingsPage::updateRectangleDraft(const QRectF &sourceRectangle)
{
    if (m_selectedArea < 0 || m_selectedArea >= m_configuration.areas.size()) {
        createRectangleDraft(sourceRectangle);
        return;
    }

    IvaAreaDefinition edited;
    QString errorMessage;
    if (!collectEditedArea(edited, errorMessage)) {
        m_videoCanvas->setAreas(m_configuration.areas);
        updateVideoOverlays();
        m_statusLabel->setText(errorMessage);
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b71c1c;font-weight:700;"));
        return;
    }
    if (m_draftChannel < 0) {
        m_draftChannel = edited.channel;
        m_draftAreaIndex = edited.areaIndex;
        m_draftReplacesExisting = true;
        m_draftOriginalArea = m_configuration.areas.at(m_selectedArea);
    }
    edited.areaCoordinates = IvaVideoCanvas::rectangleCoordinates(sourceRectangle);
    m_configuration.areas[m_selectedArea] = edited;
    populateAreaTable();
    m_areaTable->selectRow(m_selectedArea);
    populateEditor(m_selectedArea);
    updateVideoOverlays();
    m_statusLabel->setText(QStringLiteral(
        "%1 (%2) was adjusted. Review it, then Save changes or Discard draft.")
                               .arg(m_piSlotCombo->currentText(), edited.name));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
    setCameraSaveFeedback(
        QStringLiteral("%1 (%2) is an unsaved Camera WiseAI draft. Use Save changes to write it.")
            .arg(m_piSlotCombo->currentText(), edited.name),
        QStringLiteral("color:#e65100;font-weight:700;"));
    setEditorDirtyFeedback(true);
    updateButtons();
}

void IvaSettingsPage::discardRectangleDraft()
{
    if (m_draftChannel < 0) return;
    const int channel = m_draftChannel;
    for (int index = m_configuration.areas.size() - 1; index >= 0; --index) {
        const IvaAreaDefinition &area = m_configuration.areas.at(index);
        if (area.channel == m_draftChannel && area.areaIndex == m_draftAreaIndex) {
            if (m_draftReplacesExisting) {
                m_configuration.areas[index] = m_draftOriginalArea;
            } else {
                m_configuration.areas.removeAt(index);
            }
            break;
        }
    }
    m_draftChannel = -1;
    m_draftAreaIndex = -1;
    m_draftReplacesExisting = false;
    m_draftOriginalArea = {};
    m_cameraDirty = false;
    m_piDirty = false;
    populateAreaTable();
    clearEditor();
    selectChannel(channel);
    selectMappedParkingArea();
    m_statusLabel->setText(QStringLiteral("Rectangle draft discarded."));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
    updateButtons();
}

void IvaSettingsPage::setCameraSaveFeedback(const QString &message,
                                            const QString &styleSheet)
{
    if (!m_cameraSaveStatusLabel) return;
    m_cameraSaveStatusLabel->setToolTip(message);
    m_cameraSaveStatusLabel->setText(
        m_cameraSaveStatusLabel->fontMetrics().elidedText(
            message, Qt::ElideRight, m_cameraSaveStatusLabel->maximumWidth()));
    m_cameraSaveStatusLabel->setStyleSheet(styleSheet);
}

void IvaSettingsPage::setEditorState(const QString &text,
                                     const QString &background,
                                     const QString &foreground)
{
    if (!m_selectedAreaStateLabel) return;
    m_selectedAreaStateLabel->setText(text);
    m_selectedAreaStateLabel->setStyleSheet(QStringLiteral(
        "QLabel { background:%1;color:%2;border:1px solid %2;border-radius:4px;"
        "padding:3px 7px;font-size:10px;font-weight:900; }")
                                                .arg(background, foreground));
}

void IvaSettingsPage::updateEditorSummary()
{
    const bool hasSelection = m_selectedArea >= 0
        && m_selectedArea < m_configuration.areas.size();
    if (!hasSelection) {
        if (m_selectedAreaTitleLabel) {
            m_selectedAreaTitleLabel->setText(QStringLiteral("No Area selected"));
        }
        if (m_selectedAreaMetaLabel) {
            m_selectedAreaMetaLabel->setText(
                QStringLiteral("Select a camera Area from the table or video."));
        }
        if (m_detectionSelectionLabel) {
            m_detectionSelectionLabel->setText(QStringLiteral("0 selected"));
        }
        if (m_objectSelectionLabel) {
            m_objectSelectionLabel->setText(QStringLiteral("0 selected"));
        }
        if (m_allObjectFiltersCheck) {
            const QSignalBlocker blocker(m_allObjectFiltersCheck);
            m_allObjectFiltersCheck->setChecked(false);
        }
        return;
    }

    const IvaAreaDefinition &area = m_configuration.areas.at(m_selectedArea);
    const QString name = m_nameEdit->text().trimmed().isEmpty()
        ? area.name : m_nameEdit->text().trimmed();
    const int areaIndex = m_indexSpin->value();
    const int modeCount = checkedValues(m_detectionModesList).size();
    const int objectCount = checkedValues(m_objectFiltersList).size();
    const bool allObjects = m_objectFiltersList->count() > 0
        && objectCount == m_objectFiltersList->count();
    m_selectedAreaTitleLabel->setText(
        QStringLiteral("CH%1 / Area %2 / %3")
            .arg(area.channel + 1)
            .arg(areaIndex)
            .arg(name));
    m_selectedAreaMetaLabel->setText(
        QStringLiteral("%1 mode%2 / %3 / %4 points")
            .arg(modeCount)
            .arg(modeCount == 1 ? QString() : QStringLiteral("s"))
            .arg(allObjects
                     ? QStringLiteral("All objects")
                     : QStringLiteral("%1 object filter%2")
                           .arg(objectCount)
                           .arg(objectCount == 1 ? QString()
                                                 : QStringLiteral("s")))
            .arg(m_coordinateTable->rowCount()));
    m_detectionSelectionLabel->setText(
        QStringLiteral("%1 selected").arg(modeCount));
    m_objectSelectionLabel->setText(
        allObjects ? QStringLiteral("All")
                   : QStringLiteral("%1 selected").arg(objectCount));
    const QSignalBlocker blocker(m_allObjectFiltersCheck);
    m_allObjectFiltersCheck->setChecked(allObjects);
}

void IvaSettingsPage::updateChannelButtons()
{
    for (int channelIndex = 0; channelIndex < m_channelButtons.size(); ++channelIndex) {
        bool channelFound = false;
        bool enabled = false;
        for (const IvaChannelDefinition &channel : m_configuration.channels) {
            if (channel.channel == channelIndex) {
                channelFound = true;
                enabled = channel.enabled;
                break;
            }
        }
        int areaCount = 0;
        for (const IvaAreaDefinition &area : m_configuration.areas) {
            if (area.channel == channelIndex) ++areaCount;
        }
        QPushButton *button = m_channelButtons.at(channelIndex);
        const QString state = !channelFound
            ? QStringLiteral("--")
            : (enabled ? QStringLiteral("ON") : QStringLiteral("OFF"));
        button->setText(QStringLiteral("CH%1\n%2 / %3")
                            .arg(channelIndex + 1)
                            .arg(state)
                            .arg(areaCount));
        button->setToolTip(
            channelFound
                ? QStringLiteral("CH%1 is %2 with %3 configured Area%4")
                      .arg(channelIndex + 1)
                      .arg(state)
                      .arg(areaCount)
                      .arg(areaCount == 1 ? QString() : QStringLiteral("s"))
                : QStringLiteral("CH%1 has not been loaded from the camera")
                      .arg(channelIndex + 1));
        button->setProperty("channelState",
                            channelFound
                                ? (enabled ? QStringLiteral("on")
                                           : QStringLiteral("off"))
                                : QStringLiteral("unknown"));
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void IvaSettingsPage::updateDurationAvailability()
{
    if (!m_appearanceDurationSpin || !m_intrusionDurationSpin
        || !m_loiteringDurationSpin) return;

    const QStringList modes = checkedValues(m_detectionModesList);
    const auto containsMode = [&modes](const QString &expected) {
        for (const QString &mode : modes) {
            if (mode.compare(expected, Qt::CaseInsensitive) == 0) return true;
        }
        return false;
    };
    const bool hasSelection = m_selectedArea >= 0
        && m_selectedArea < m_configuration.areas.size();
    const bool editable = hasSelection && !m_requestInFlight
        && !m_piRoiRequestInFlight && m_saveStage == SaveStage::Idle;
    const bool appearanceEnabled = editable && !modes.isEmpty();
    const bool intrusionEnabled = editable && containsMode(QStringLiteral("Intrusion"));
    const bool loiteringEnabled = editable && containsMode(QStringLiteral("Loitering"));

    m_appearanceDurationSpin->setEnabled(appearanceEnabled);
    m_intrusionDurationSpin->setEnabled(intrusionEnabled);
    m_loiteringDurationSpin->setEnabled(loiteringEnabled);
    m_appearanceDurationLabel->setEnabled(appearanceEnabled);
    m_intrusionDurationLabel->setEnabled(intrusionEnabled);
    m_loiteringDurationLabel->setEnabled(loiteringEnabled);
    m_appearanceDurationSpin->setToolTip(
        appearanceEnabled ? QStringLiteral("Minimum appearance time before detection")
                          : QStringLiteral("Select a detection mode to edit this value"));
    m_intrusionDurationSpin->setToolTip(
        intrusionEnabled ? QStringLiteral("Time required to confirm intrusion")
                         : QStringLiteral("Select Intrusion mode to edit this value"));
    m_loiteringDurationSpin->setToolTip(
        loiteringEnabled ? QStringLiteral("Time required to confirm loitering")
                         : QStringLiteral("Select Loitering mode to edit this value"));
}

bool IvaSettingsPage::editorMatchesParkingArea() const
{
    return m_selectedArea >= 0
        && m_selectedArea < m_configuration.areas.size()
        && m_configuration.areas.at(m_selectedArea).channel == m_selectedChannel
        && m_indexSpin->value() == mappedParkingAreaIndex()
        && m_nameEdit->text().trimmed().compare(
               mappedParkingAreaName(), Qt::CaseInsensitive) == 0;
}

void IvaSettingsPage::setEditorDirtyFeedback(bool geometryChanged)
{
    if (m_updatingEditor
        || m_requestInFlight
        || m_selectedArea < 0
        || m_selectedArea >= m_configuration.areas.size()) {
        return;
    }
    const IvaAreaDefinition &area = m_configuration.areas.at(m_selectedArea);
    m_cameraDirty = true;
    if (geometryChanged && editorMatchesParkingArea()) {
        m_piDirty = true;
        m_piRoiStatusLabel->setText(
            QStringLiteral("ROI geometry changed. Pi Crop ROI will be saved automatically."));
        m_piRoiStatusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
    }
    setCameraSaveFeedback(
        QStringLiteral("Unsaved Camera WiseAI draft for CH%1 Area %2 (%3).")
            .arg(area.channel + 1)
            .arg(m_indexSpin ? m_indexSpin->value() : area.areaIndex)
            .arg(m_nameEdit ? m_nameEdit->text().trimmed() : area.name),
        QStringLiteral("color:#e65100;font-weight:700;"));
    setEditorState(QStringLiteral("UNSAVED"), QStringLiteral("#fff3e0"),
                   QStringLiteral("#e65100"));
    updateEditorSummary();
}

void IvaSettingsPage::updateButtons()
{
    if (!m_refreshButton || !m_applyButton) {
        return;
    }
    const bool hasSelection = m_selectedArea >= 0
        && m_selectedArea < m_configuration.areas.size();
    const bool selectedAreaIsOnChannel = hasSelection
        && m_configuration.areas.at(m_selectedArea).channel == m_selectedChannel;
    const bool matchesParkingArea = editorMatchesParkingArea();
    const bool anyRequestInFlight = m_requestInFlight || m_piRoiRequestInFlight
        || m_saveStage != SaveStage::Idle;
    m_refreshButton->setEnabled(!anyRequestInFlight);
    const bool canSavePi = matchesParkingArea
        && m_currentPreviewFrameSize.isValid();
    const bool savePi = (m_cameraDirty || m_piDirty) && canSavePi;
    m_applyButton->setEnabled(!anyRequestInFlight && m_hasOptions && hasSelection
                              && (m_cameraDirty || savePi));
    m_applyButton->setText(
        m_cameraDirty && savePi ? QStringLiteral("Save Camera + Pi")
        : m_cameraDirty ? QStringLiteral("Save Camera")
        : savePi ? QStringLiteral("Save Pi ROI")
                 : QStringLiteral("No changes"));
    m_channelEnabledCheck->setEnabled(!anyRequestInFlight && hasSelection);
    m_geometryToggleButton->setEnabled(hasSelection);
    m_allObjectFiltersCheck->setEnabled(!anyRequestInFlight && hasSelection
                                        && m_objectFiltersList->count() > 0);
    m_deleteAreaButton->setEnabled(!anyRequestInFlight && hasSelection
                                   && m_draftChannel < 0);
    m_addPointButton->setEnabled(!anyRequestInFlight && hasSelection);
    m_removePointButton->setEnabled(!anyRequestInFlight && hasSelection
                                    && m_coordinateTable->currentRow() >= 0);
    const IvaChannelCapability *capability = m_capabilities.forChannel(
        m_selectedChannel);
    const bool canDraw = !anyRequestInFlight && m_hasOptions && m_hasCapabilities
        && capability && capability->ivaAreaSupported
        && capability->maxResolution.isValid()
        && m_videoCanvas->frameCompatible() && m_draftChannel < 0;
    m_videoCanvas->setDrawMode(canDraw);
    const bool canEdit = !anyRequestInFlight && m_hasOptions && m_hasCapabilities
        && capability && capability->ivaAreaSupported
        && capability->maxResolution.isValid()
        && m_videoCanvas->frameCompatible() && selectedAreaIsOnChannel;
    m_videoCanvas->setEditMode(canEdit);
    m_discardDraftButton->setEnabled(!anyRequestInFlight && m_draftChannel >= 0);
    m_piSlotCombo->setEnabled(!m_piRoiRequestInFlight
                              && !m_requestInFlight
                              && m_saveStage == SaveStage::Idle
                              && m_draftChannel < 0);
    updateDurationAvailability();
}
