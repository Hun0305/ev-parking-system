#include "ivasettingspage.h"

#include "iva/ivavideocanvas.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPolygonF>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

IvaSettingsPage::IvaSettingsPage(const QString &cameraIp, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Hanwha WiseAI IVA Configuration"), this);
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:800;color:#202124;"));
    titleRow->addWidget(title, 1);
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
    titleRow->addWidget(helpButton);
    layout->addLayout(titleRow);

    auto *description = new QLabel(
        QStringLiteral("Rules are read and written directly over HTTPS Digest. "
                       "Apply performs a fresh conflict check, PUT, camera re-read, "
                       "and last-good rollback when verification differs."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_cameraLabel = new QLabel(this);
    m_cameraLabel->setObjectName(QStringLiteral("ivaCameraAddressLabel"));
    m_channelSummaryLabel = new QLabel(QStringLiteral("Not loaded"), this);
    m_channelSummaryLabel->setObjectName(QStringLiteral("ivaChannelSummaryLabel"));
    layout->addWidget(m_cameraLabel);
    layout->addWidget(m_channelSummaryLabel);

    auto *workspaceSplitter = new QSplitter(Qt::Horizontal, this);
    auto *videoPanel = new QWidget(workspaceSplitter);
    auto *videoLayout = new QVBoxLayout(videoPanel);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    auto *channelRow = new QHBoxLayout;
    auto *channelGroup = new QButtonGroup(this);
    channelGroup->setExclusive(true);
    for (int channel = 0; channel < 4; ++channel) {
        auto *button = new QPushButton(QStringLiteral("CH%1").arg(channel + 1),
                                       videoPanel);
        button->setObjectName(QStringLiteral("ivaPreviewChannel%1Button").arg(channel + 1));
        button->setCheckable(true);
        button->setProperty("channel", channel);
        channelGroup->addButton(button, channel);
        channelRow->addWidget(button);
        m_channelButtons.append(button);
        connect(button, &QPushButton::clicked, this, [this, channel]() {
            selectChannel(channel);
            selectMappedParkingArea();
        });
    }
    m_channelButtons.constFirst()->setChecked(true);
    channelRow->addStretch(1);
    auto *parkingAreaLabel = new QLabel(QStringLiteral("Parking Area"), videoPanel);
    parkingAreaLabel->setStyleSheet(QStringLiteral("font-weight:700;"));
    m_piSlotCombo = new QComboBox(videoPanel);
    m_piSlotCombo->setObjectName(QStringLiteral("ivaPiParkingSlotCombo"));
    m_piSlotCombo->addItems({QStringLiteral("EV-01"), QStringLiteral("EV-02"),
                             QStringLiteral("EV-03"), QStringLiteral("EV-04")});
    m_piSlotCombo->setMinimumWidth(90);
    m_piSlotCombo->setToolTip(QStringLiteral(
        "EV-01-EV-04 map to camera IVA rules name1-name4 and indexes 1-4."));
    channelRow->addWidget(parkingAreaLabel);
    channelRow->addWidget(m_piSlotCombo);
    m_discardDraftButton = new QPushButton(QStringLiteral("Discard draft"), videoPanel);
    m_discardDraftButton->setObjectName(QStringLiteral("ivaDiscardDraftButton"));
    channelRow->addWidget(m_discardDraftButton);
    videoLayout->addLayout(channelRow);
    m_videoCanvas = new IvaVideoCanvas(videoPanel);
    videoLayout->addWidget(m_videoCanvas, 1);
    m_frameStatusLabel = new QLabel(
        QStringLiteral("Refresh the camera, then select a channel."), videoPanel);
    m_frameStatusLabel->setObjectName(QStringLiteral("ivaFrameStatusLabel"));
    m_frameStatusLabel->setWordWrap(true);
    videoLayout->addWidget(m_frameStatusLabel);
    auto *dragInstruction = new QLabel(
        QStringLiteral("1. Select a channel and parking area  2. Drag on the video  "
                       "3. Save to the camera and/or Pi server"),
        videoPanel);
    dragInstruction->setObjectName(QStringLiteral("ivaDragInstructionLabel"));
    dragInstruction->setWordWrap(true);
    dragInstruction->setStyleSheet(QStringLiteral(
        "padding:6px;background:#e3f2fd;color:#0d47a1;font-weight:700;"));
    videoLayout->addWidget(dragInstruction);
    workspaceSplitter->addWidget(videoPanel);

    auto *splitter = new QSplitter(Qt::Vertical, workspaceSplitter);
    m_areaTable = new QTableWidget(0, 8, splitter);
    m_areaTable->setObjectName(QStringLiteral("ivaAreaTable"));
    m_areaTable->setHorizontalHeaderLabels(
        {QStringLiteral("Channel"), QStringLiteral("Enabled"),
         QStringLiteral("Index"), QStringLiteral("Rule name"),
         QStringLiteral("Detection modes"), QStringLiteral("Object filters"),
         QStringLiteral("Points"), QStringLiteral("Durations (s)")});
    m_areaTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_areaTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_areaTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_areaTable->setAlternatingRowColors(true);
    m_areaTable->verticalHeader()->setVisible(false);
    m_areaTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_areaTable->horizontalHeader()->setStretchLastSection(true);
    for (int column : {1, 4, 5, 6, 7}) {
        m_areaTable->setColumnHidden(column, true);
    }

    auto *editorGroup = new QGroupBox(QStringLiteral("Selected Area Details"), splitter);
    editorGroup->setObjectName(QStringLiteral("ivaRuleEditor"));
    auto *editorLayout = new QGridLayout(editorGroup);

    m_channelEnabledCheck = new QCheckBox(QStringLiteral("Channel enabled"), editorGroup);
    m_channelEnabledCheck->setObjectName(QStringLiteral("ivaChannelEnabledCheck"));
    m_indexSpin = new QSpinBox(editorGroup);
    m_indexSpin->setObjectName(QStringLiteral("ivaRuleIndexSpin"));
    m_indexSpin->setRange(0, 999);
    m_nameEdit = new QLineEdit(editorGroup);
    m_nameEdit->setObjectName(QStringLiteral("ivaRuleNameEdit"));
    editorLayout->addWidget(m_channelEnabledCheck, 0, 0);
    editorLayout->addWidget(new QLabel(QStringLiteral("Rule index"), editorGroup), 0, 1);
    editorLayout->addWidget(m_indexSpin, 0, 2);
    editorLayout->addWidget(new QLabel(QStringLiteral("Rule name"), editorGroup), 0, 3);
    editorLayout->addWidget(m_nameEdit, 0, 4);

    m_detectionModesList = new QListWidget(editorGroup);
    m_detectionModesList->setObjectName(QStringLiteral("ivaDetectionModesList"));
    m_objectFiltersList = new QListWidget(editorGroup);
    m_objectFiltersList->setObjectName(QStringLiteral("ivaObjectFiltersList"));
    editorLayout->addWidget(new QLabel(QStringLiteral("Detection modes"), editorGroup),
                            1, 0, 1, 2);
    editorLayout->addWidget(new QLabel(QStringLiteral("Object filters"), editorGroup),
                            1, 2, 1, 2);
    editorLayout->addWidget(m_detectionModesList, 2, 0, 1, 2);
    editorLayout->addWidget(m_objectFiltersList, 2, 2, 1, 2);
    m_detectionModesList->setMinimumHeight(90);
    m_objectFiltersList->setMinimumHeight(90);

    auto *durationWidget = new QWidget(editorGroup);
    auto *durationLayout = new QFormLayout(durationWidget);
    durationLayout->setContentsMargins(0, 0, 0, 0);
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
    durationLayout->addRow(QStringLiteral("Appearance"), m_appearanceDurationSpin);
    durationLayout->addRow(QStringLiteral("Intrusion"), m_intrusionDurationSpin);
    durationLayout->addRow(QStringLiteral("Loitering"), m_loiteringDurationSpin);
    editorLayout->addWidget(durationWidget, 2, 4);

    m_coordinateTable = new QTableWidget(0, 2, editorGroup);
    m_coordinateTable->setObjectName(QStringLiteral("ivaCoordinateTable"));
    m_coordinateTable->setHorizontalHeaderLabels(
        {QStringLiteral("X"), QStringLiteral("Y")});
    m_coordinateTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_coordinateTable->verticalHeader()->setDefaultSectionSize(24);
    m_coordinateTable->setMinimumHeight(145);
    auto *coordinateButtons = new QHBoxLayout;
    m_addPointButton = new QPushButton(QStringLiteral("Add point"), editorGroup);
    m_addPointButton->setObjectName(QStringLiteral("ivaAddPointButton"));
    m_removePointButton = new QPushButton(QStringLiteral("Remove point"), editorGroup);
    m_removePointButton->setObjectName(QStringLiteral("ivaRemovePointButton"));
    coordinateButtons->addWidget(m_addPointButton);
    coordinateButtons->addWidget(m_removePointButton);
    coordinateButtons->addStretch(1);
    auto *coordinateWidget = new QWidget(editorGroup);
    auto *coordinateLayout = new QVBoxLayout(coordinateWidget);
    coordinateLayout->setContentsMargins(0, 0, 0, 0);
    coordinateLayout->addWidget(new QLabel(
        QStringLiteral("Polygon points (camera pixel coordinates)"), coordinateWidget));
    coordinateLayout->addWidget(m_coordinateTable);
    coordinateLayout->addLayout(coordinateButtons);
    editorLayout->addWidget(coordinateWidget, 3, 0, 1, 5);

    m_applyButton = new QPushButton(QStringLiteral("Save Area to Camera"),
                                    editorGroup);
    m_applyButton->setObjectName(QStringLiteral("applyIvaConfigurationButton"));
    m_applyButton->setToolTip(QStringLiteral(
        "Save the selected Area to the Hanwha camera and verify it by reading it back."));
    m_deleteAreaButton = new QPushButton(QStringLiteral("Delete selected Area"),
                                         editorGroup);
    m_deleteAreaButton->setObjectName(QStringLiteral("deleteSelectedIvaAreaButton"));
    auto *applyButtons = new QHBoxLayout;
    applyButtons->addStretch(1);
    applyButtons->addWidget(m_deleteAreaButton);
    applyButtons->addWidget(m_applyButton);
    editorLayout->addLayout(applyButtons, 4, 0, 1, 5);
    splitter->addWidget(m_areaTable);
    splitter->addWidget(editorGroup);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({170, 500});
    workspaceSplitter->addWidget(splitter);
    workspaceSplitter->setStretchFactor(0, 3);
    workspaceSplitter->setStretchFactor(1, 2);
    layout->addWidget(workspaceSplitter, 1);

    auto *piRoiGroup = new QGroupBox(QStringLiteral("Parking Area Actions"), this);
    auto *piRoiLayout = new QHBoxLayout(piRoiGroup);
    m_sendPiRoiButton = new QPushButton(QStringLiteral("Save Crop ROI to Pi"), piRoiGroup);
    m_sendPiRoiButton->setObjectName(QStringLiteral("sendIvaRoiToPiButton"));
    m_sendPiRoiButton->setToolTip(QStringLiteral(
        "Send the selected IVA polygon's normalized bounding rectangle to the Pi server. No image is uploaded."));
    m_piRoiStatusLabel = new QLabel(
        QStringLiteral("Select EV-01-EV-04, then drag directly on the video. "
                       "EV-01-EV-04 map to name1-name4."),
        piRoiGroup);
    m_piRoiStatusLabel->setObjectName(QStringLiteral("ivaPiRoiStatusLabel"));
    m_piRoiStatusLabel->setWordWrap(true);
    piRoiLayout->addWidget(m_sendPiRoiButton);
    piRoiLayout->addWidget(m_piRoiStatusLabel, 1);
    layout->addWidget(piRoiGroup);

    m_statusLabel = new QLabel(
        QStringLiteral("Open this page or press Refresh to read the camera."), this);
    m_statusLabel->setObjectName(QStringLiteral("ivaStatusLabel"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_refreshButton = new QPushButton(QStringLiteral("Refresh from camera"), this);
    m_refreshButton->setObjectName(QStringLiteral("refreshIvaConfigurationButton"));
    layout->addWidget(m_refreshButton, 0, Qt::AlignRight);

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
        }
        updateButtons();
    });
    connect(m_coordinateTable, &QTableWidget::currentCellChanged,
            this, [this]() { updateButtons(); });
    connect(m_indexSpin, &QSpinBox::valueChanged, this, [this]() {
        if (!m_updatingEditor) updateButtons();
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_updatingEditor) updateButtons();
    });
    connect(m_discardDraftButton, &QPushButton::clicked,
            this, &IvaSettingsPage::discardRectangleDraft);
    connect(m_videoCanvas, &IvaVideoCanvas::rectangleDrafted,
            this, [this](const QRectF &rectangle) {
        createRectangleDraft(rectangle);
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
        if (!m_piRoiRequestInFlight) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "Drag directly on the video for %1 (%2), then save it to the camera or Pi server.")
                                            .arg(m_piSlotCombo->currentText(),
                                                 mappedParkingAreaName()));
            m_piRoiStatusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
        }
        if (m_draftChannel < 0) {
            selectMappedParkingArea();
        }
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        IvaAreaDefinition edited;
        QString errorMessage;
        if (!collectEditedArea(edited, errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("IVA validation"), errorMessage);
            return;
        }
        const QMessageBox::StandardButton result = QMessageBox::question(
            this, QStringLiteral("Apply IVA configuration"),
            QStringLiteral("Write CH%1 rule %2 (%3) to camera %4?\n\n"
                           "The client will re-read the camera and roll back if verification differs.")
                .arg(edited.channel + 1)
                .arg(edited.areaIndex)
                .arg(edited.name)
                .arg(m_cameraIp),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
        QList<IvaAreaDefinition> channelAreas;
        for (int index = 0; index < m_configuration.areas.size(); ++index) {
            const IvaAreaDefinition &area = m_configuration.areas.at(index);
            if (area.channel != edited.channel) {
                continue;
            }
            channelAreas.append(index == m_selectedArea ? edited : area);
        }
        emit applyRequested(edited.channel, m_channelEnabledCheck->isChecked(),
                            channelAreas);
    });
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
    connect(m_sendPiRoiButton, &QPushButton::clicked, this, [this]() {
        if (m_piRoiRequestInFlight) return;
        IvaAreaDefinition edited;
        QString errorMessage;
        if (!collectEditedArea(edited, errorMessage)) {
            m_piRoiStatusLabel->setText(errorMessage);
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        if (edited.channel != m_selectedChannel
            || edited.areaIndex != mappedParkingAreaIndex()
            || edited.name.compare(mappedParkingAreaName(),
                                   Qt::CaseInsensitive) != 0) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "The selected IVA rule does not match %1 (%2 / Area index %3).")
                                            .arg(m_piSlotCombo->currentText(),
                                                 mappedParkingAreaName())
                                            .arg(mappedParkingAreaIndex()));
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        const IvaChannelCapability *capability = m_capabilities.forChannel(
            edited.channel);
        if (!capability || !capability->maxResolution.isValid()
            || edited.areaCoordinates.size() < 3) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "The selected IVA rule does not provide a valid polygon or coordinate resolution."));
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        QPolygonF polygon;
        for (const QPointF &point : edited.areaCoordinates) polygon.append(point);
        const QRectF normalized = IvaVideoCanvas::normalizedFromSource(
            polygon.boundingRect(), capability->maxResolution);
        const ParkingRoi roi = ParkingRoi::fromRectangle(normalized);
        if (!roi.isValid(&errorMessage)) {
            m_piRoiStatusLabel->setText(errorMessage);
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        if (!m_currentPreviewFrameSize.isValid()) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "Unable to validate the ROI because the source frame size is unavailable."));
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        if (!roi.isLargeEnough(m_currentPreviewFrameSize)) {
            m_piRoiStatusLabel->setText(QStringLiteral(
                "ROI is too small. Please select the area again.\n"
                "The selected area is too small. Please select an area of at least 8 × 8 pixels."));
            m_piRoiStatusLabel->setStyleSheet(
                QStringLiteral("color:#b71c1c;font-weight:700;"));
            return;
        }
        ++m_piRoiGeneration;
        m_pendingPiSlotId = m_piSlotCombo->currentText();
        m_piRoiRequestInFlight = true;
        m_piRoiStatusLabel->setText(
            QStringLiteral("Sending %1 normalized ROI to the Pi server...")
                .arg(m_pendingPiSlotId));
        m_piRoiStatusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
        updateButtons();
        emit piRoiSaveRequested(m_pendingPiSlotId, roi, m_piRoiGeneration);
    });
    connect(helpButton, &QPushButton::clicked,
            this, &IvaSettingsPage::showHelpDialog);

    setCameraIp(cameraIp);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(100);
    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) emit previewFrameRequested(m_selectedChannel);
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
        {QStringLiteral("③ Parking Area"), QStringLiteral("EV-01~EV-04 중 매핑 대상 선택")},
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
        QStringLiteral("Pi ROI 저장 버튼은 아래 세 값이 정확히 일치하는 Area를 선택했을 때만 활성화됩니다."),
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
        {QStringLiteral("Save Area to Camera"), QStringLiteral("선택 CH의 WiseAI 설정을 변경하며 Pi ROI에는 영향 없음")},
        {QStringLiteral("Save Crop ROI to Pi"), QStringLiteral("정규화 bounding rectangle을 선택 EV 슬롯에 저장하며 Camera Area에는 영향 없음")},
        {QStringLiteral("Refresh from camera"), QStringLiteral("카메라의 최신 Options, Capability, Configuration을 다시 읽음")}
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
        QStringLiteral("Camera 저장  확인 → 최신 설정 충돌 검사 → PUT/DELETE → 카메라 재조회 검증 → 불일치 시 last-good rollback"),
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
            "• Apply 결과를 검증할 수 없다는 메시지가 나오면 추가 편집 전에 Refresh from camera를 실행합니다.\n"
            "• 카메라가 다른 곳에서 변경되어 preflight 충돌이 나면 새로 읽힌 값을 검토한 뒤 다시 적용합니다.\n"
            "• TLS 인증서는 최초 연결 시 SHA-256 지문으로 고정되며 이후 지문이 다르면 연결을 중단합니다. 평문으로 자동 전환하지 않습니다.\n"
            "• 카메라 설정 삭제는 Camera Web Viewer에도 반영되므로 채널과 Area index/name을 확인한 후 승인합니다.\n"
            "• 이 화면의 EV-01~04는 Pi ROI 선택 이름이며 실제 서버 slot_id 매핑을 자동으로 의미하지 않습니다."),
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

void IvaSettingsPage::setCameraIp(const QString &cameraIp)
{
    const QString normalized = cameraIp.trimmed();
    if (!m_cameraIp.isEmpty() && normalized != m_cameraIp) {
        m_loadedOnce = false;
        m_hasOptions = false;
        m_hasCapabilities = false;
        m_configuration = {};
        m_options = {};
        m_capabilities = {};
        m_draftChannel = -1;
        m_draftAreaIndex = -1;
        m_areaTable->setRowCount(0);
        m_channelSummaryLabel->setText(QStringLiteral("Not loaded"));
        clearEditor();
    }
    m_cameraIp = normalized;
    m_cameraLabel->setText(
        normalized.isEmpty()
            ? QStringLiteral("Camera: not configured")
            : QStringLiteral("Camera: https://%1").arg(normalized));
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
    m_configuration = configuration;
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
    m_statusLabel->setText(
        QStringLiteral("Applying CH%1: conflict check -> PUT -> camera verification...")
            .arg(channel + 1));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#0d47a1;font-weight:700;"));
    updateButtons();
}

void IvaSettingsPage::setApplySuccess(
    int channel,
    const IvaAreaConfiguration &verifiedConfiguration)
{
    Q_UNUSED(verifiedConfiguration)
    m_requestInFlight = false;
    if (m_pendingDeletedAreaIndex >= 0) {
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
    updateButtons();
}

void IvaSettingsPage::setApplyError(int channel,
                                    const QString &message,
                                    bool rollbackSucceeded)
{
    m_requestInFlight = false;
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
    m_piRoiStatusLabel->setText(
        appliedImmediately
            ? QStringLiteral("%1 ROI was saved, verified, and applied immediately on the Pi server.").arg(slotId)
            : QStringLiteral("%1 ROI was saved and verified on the Pi server.").arg(slotId));
    m_piRoiStatusLabel->setStyleSheet(
        QStringLiteral("color:#1b5e20;font-weight:700;"));
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
    m_piRoiStatusLabel->setText(
        QStringLiteral("Failed to save %1 ROI on the Pi server: %2")
            .arg(slotId, message));
    m_piRoiStatusLabel->setStyleSheet(
        QStringLiteral("color:#b71c1c;font-weight:700;"));
    updateButtons();
}

void IvaSettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_previewTimer) {
        m_previewTimer->start();
        emit previewFrameRequested(m_selectedChannel);
    }
    if (m_loadedOnce || m_requestInFlight) return;
    setRequestStarted();
    emit refreshRequested();
}

void IvaSettingsPage::hideEvent(QHideEvent *event)
{
    if (m_previewTimer) m_previewTimer->stop();
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
    for (int row = 0; row < m_configuration.areas.size(); ++row) {
        const IvaAreaDefinition &area = m_configuration.areas.at(row);
        const QStringList values{
            QStringLiteral("CH%1").arg(area.channel + 1),
            area.channelEnabled ? QStringLiteral("ON") : QStringLiteral("OFF"),
            area.areaIndex < 0 ? QStringLiteral("-") : QString::number(area.areaIndex),
            area.name,
            area.detectionModes.join(QStringLiteral(", ")),
            area.objectTypeFilter.join(QStringLiteral(", ")),
            QString::number(area.areaCoordinates.size()),
            durationText(area)};
        for (int column = 0; column < values.size(); ++column) {
            m_areaTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
    }
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
    if (area.areaIndex >= 1 && area.areaIndex <= m_piSlotCombo->count()) {
        const QString mappedName = QStringLiteral("name%1").arg(area.areaIndex);
        if (area.name.compare(mappedName, Qt::CaseInsensitive) == 0) {
            const QSignalBlocker blocker(m_piSlotCombo);
            m_piSlotCombo->setCurrentIndex(area.areaIndex - 1);
        }
    }
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
    updateButtons();
}

void IvaSettingsPage::clearEditor()
{
    m_selectedArea = -1;
    m_channelEnabledCheck->setChecked(false);
    m_indexSpin->setValue(0);
    m_nameEdit->clear();
    m_detectionModesList->clear();
    m_objectFiltersList->clear();
    m_coordinateTable->setRowCount(0);
    updateButtons();
}

void IvaSettingsPage::populateChecklist(QListWidget *list,
                                        const QStringList &available,
                                        const QStringList &selected,
                                        bool emptyMeansAll)
{
    list->clear();
    const bool selectAll = emptyMeansAll && selected.isEmpty();
    for (const QString &value : available) {
        auto *item = new QListWidgetItem(value, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectAll || selected.contains(value)
                                ? Qt::Checked : Qt::Unchecked);
    }
}

QStringList IvaSettingsPage::checkedValues(const QListWidget *list) const
{
    QStringList values;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem *item = list->item(row);
        if (item->checkState() == Qt::Checked) {
            values.append(item->text());
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
}

void IvaSettingsPage::selectChannel(int channel)
{
    if (channel < 0) return;
    m_selectedChannel = channel;
    m_currentPreviewFrameSize = {};
    for (int index = 0; index < m_channelButtons.size(); ++index) {
        m_channelButtons.at(index)->setChecked(index == channel);
    }
    const IvaChannelCapability *capability = m_capabilities.forChannel(channel);
    const QSize coordinateResolution = capability && capability->ivaAreaSupported
        ? capability->maxResolution : QSize();
    m_videoCanvas->setChannel(channel, coordinateResolution);
    updateVideoOverlays();
    if (m_previewTimer && m_previewTimer->isActive()) {
        emit previewFrameRequested(channel);
    }
    updateButtons();
}

int IvaSettingsPage::mappedParkingAreaIndex() const
{
    return m_piSlotCombo ? m_piSlotCombo->currentIndex() + 1 : -1;
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
                "%1 is mapped to %2. Drag directly on the video to redraw it.")
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
        IvaAreaDefinition &selectedArea = m_configuration.areas[targetRow];
        m_draftChannel = selectedArea.channel;
        m_draftReplacesExisting = true;
        m_draftOriginalArea = selectedArea;
        selectedArea.areaIndex = targetIndex;
        selectedArea.name = targetName;
        selectedArea.areaCoordinates = rectangleCoordinates;
        m_draftAreaIndex = targetIndex;

        populateAreaTable();
        m_areaTable->selectRow(targetRow);
        populateEditor(targetRow);
        updateVideoOverlays();
        m_statusLabel->setText(QStringLiteral(
            "%1 (%2) was redrawn. Review it, then Apply or Discard draft.")
                                   .arg(m_piSlotCombo->currentText(), targetName));
        m_statusLabel->setStyleSheet(
            QStringLiteral("color:#e65100;font-weight:700;"));
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
        "%1 (%2) draft was created. Choose detection modes and object filters, then Apply.")
                               .arg(m_piSlotCombo->currentText(), targetName));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#e65100;font-weight:700;"));
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
    populateAreaTable();
    clearEditor();
    selectChannel(channel);
    selectMappedParkingArea();
    m_statusLabel->setText(QStringLiteral("Rectangle draft discarded."));
    m_statusLabel->setStyleSheet(QStringLiteral("color:#455a64;"));
    updateButtons();
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
    const bool editorMatchesParkingArea = selectedAreaIsOnChannel
        && m_indexSpin->value() == mappedParkingAreaIndex()
        && m_nameEdit->text().trimmed().compare(
               mappedParkingAreaName(), Qt::CaseInsensitive) == 0;
    const bool anyRequestInFlight = m_requestInFlight || m_piRoiRequestInFlight;
    m_refreshButton->setEnabled(!anyRequestInFlight);
    m_applyButton->setEnabled(!anyRequestInFlight && m_hasOptions && hasSelection);
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
    m_discardDraftButton->setEnabled(!m_requestInFlight && m_draftChannel >= 0);
    m_sendPiRoiButton->setEnabled(!m_requestInFlight
                                  && !m_piRoiRequestInFlight
                                  && editorMatchesParkingArea
                                  && m_currentPreviewFrameSize.isValid());
    m_piSlotCombo->setEnabled(!m_piRoiRequestInFlight
                              && !m_requestInFlight
                              && m_draftChannel < 0);
}
