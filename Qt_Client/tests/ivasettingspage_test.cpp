#include "pages/ivasettingspage.h"
#include "iva/ivavideocanvas.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QImage>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include <iostream>

namespace {
bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << "FAIL: " << message << '\n';
    return false;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    IvaSettingsPage page(QStringLiteral("192.0.2.10"));
    page.setParkingZoneMappings(defaultParkingZoneLayout());

    IvaAreaConfiguration configuration;
    IvaChannelDefinition channel0;
    channel0.channel = 0;
    channel0.enabled = true;
    channel0.rawContainer = QJsonObject{
        {QStringLiteral("definedArea"), QJsonArray{QJsonObject{}}}};
    configuration.channels.append(channel0);
    IvaChannelDefinition channel1;
    channel1.channel = 1;
    channel1.enabled = false;
    channel1.rawContainer = QJsonObject{
        {QStringLiteral("definedArea"), QJsonArray{}}};
    configuration.channels.append(channel1);

    IvaAreaDefinition area;
    area.channel = 0;
    area.channelEnabled = true;
    area.areaIndex = 1;
    area.name = QStringLiteral("parking-a");
    area.detectionModes = {QStringLiteral("Intrusion"), QStringLiteral("Loitering")};
    area.objectTypeFilter.clear();
    area.areaCoordinates = {QPointF(0, 0), QPointF(80, 0), QPointF(80, 40)};
    area.appearanceDuration = 10;
    area.intrusionDuration = 5;
    area.loiteringDuration = 15;
    configuration.areas.append(area);

    IvaChannelOptions channelOptions;
    channelOptions.channel = 0;
    channelOptions.areaIndex = {1, 4};
    channelOptions.areaCoordinateCount = {4, 8};
    channelOptions.appearanceDuration = {1, 60};
    channelOptions.intrusionDuration = {0, 5};
    channelOptions.loiteringDuration = {1, 600};
    channelOptions.detectionModes = {
        QStringLiteral("Intrusion"), QStringLiteral("Loitering"),
        QStringLiteral("AppearDisappear"), QStringLiteral("Entering"),
        QStringLiteral("Exiting")};
    channelOptions.objectTypeFilters = {
        QStringLiteral("Person"), QStringLiteral("Vehicle.Bicycle"),
        QStringLiteral("Vehicle.Car"), QStringLiteral("Vehicle.Motorcycle"),
        QStringLiteral("Vehicle.Bus"), QStringLiteral("Vehicle.Truck")};
    IvaAreaOptions options;
    options.channels.append(channelOptions);
    page.setOptions(options);
    IvaChannelCapability capability;
    capability.channel = 0;
    capability.ivaAreaSupported = true;
    capability.maxResolution = QSize(2592, 1520);
    WiseAiCapabilities capabilities;
    capabilities.channels.append(capability);
    page.setCapabilities(capabilities);
    page.setConfiguration(configuration);
    page.setPreviewFrame(0, QImage(1296, 760, QImage::Format_RGB32));
    auto *table = page.findChild<QTableWidget *>(QStringLiteral("ivaAreaTable"));
    if (!require(table && table->rowCount() == 1,
                 "validated IVA areas must populate the table")) return 1;
    if (!require(table->columnCount() == 6
                     && table->item(0, 0)->text() == QStringLiteral("CH1")
                     && table->item(0, 1)->text() == QStringLiteral("1")
                     && table->item(0, 2)->text() == QStringLiteral("parking-a")
                     && table->item(0, 3)->text().contains(QStringLiteral("Intrusion"))
                     && table->item(0, 4)->text().isEmpty()
                     && !table->item(0, 4)->icon().isNull()
                     && table->item(0, 4)->toolTip().contains(QStringLiteral("All objects"))
                     && table->item(0, 4)->data(Qt::AccessibleTextRole).toString()
                            == table->item(0, 4)->toolTip()
                     && table->item(0, 5)->text() == QStringLiteral("ON")
                     && !table->isColumnHidden(3) && !table->isColumnHidden(4)
                     && !table->isColumnHidden(5),
                 "table must show readable rule data and an accessible object icon strip")) return 1;
    auto *summary = page.findChild<QLabel *>(QStringLiteral("ivaChannelSummaryLabel"));
    if (!require(summary && summary->text().contains(QStringLiteral("CH2 OFF (0 areas)"))
                     && !summary->isVisible(),
                 "channel counts must remain available diagnostically without duplicating the channel buttons")) return 1;
    auto *coordinateTable = page.findChild<QTableWidget *>(
        QStringLiteral("ivaCoordinateTable"));
    auto *indexSpin = page.findChild<QSpinBox *>(QStringLiteral("ivaRuleIndexSpin"));
    auto *apply = page.findChild<QPushButton *>(
        QStringLiteral("saveIvaChangesButton"));
    auto *saveGroup = page.findChild<QFrame *>(
        QStringLiteral("ivaSaveChangesGroup"));
    auto *cameraWiseAiAreasTitle = page.findChild<QLabel *>(
        QStringLiteral("ivaCameraWiseAiAreasTitle"));
    auto *cameraSaveStatus = page.findChild<QLabel *>(
        QStringLiteral("ivaCameraSaveStatusLabel"));
    auto *ivaStatus = page.findChild<QLabel *>(QStringLiteral("ivaStatusLabel"));
    auto *areaCount = page.findChild<QLabel *>(QStringLiteral("ivaAreaCountLabel"));
    auto *selectedTitle = page.findChild<QLabel *>(
        QStringLiteral("ivaSelectedAreaTitleLabel"));
    auto *selectedState = page.findChild<QLabel *>(
        QStringLiteral("ivaSelectedAreaStateLabel"));
    auto *geometryToggle = page.findChild<QToolButton *>(
        QStringLiteral("ivaGeometryToggleButton"));
    auto *geometry = page.findChild<QWidget *>(QStringLiteral("ivaAdvancedGeometry"));
    auto *allObjects = page.findChild<QCheckBox *>(
        QStringLiteral("ivaAllObjectFiltersCheck"));
    auto *channel1Button = page.findChild<QPushButton *>(
        QStringLiteral("ivaPreviewChannel1Button"));
    auto *channelEnabled = page.findChild<QCheckBox *>(
        QStringLiteral("ivaChannelEnabledCheck"));
    auto *objectFilters = page.findChild<QListWidget *>(
        QStringLiteral("ivaObjectFiltersList"));
    auto *detectionModes = page.findChild<QListWidget *>(
        QStringLiteral("ivaDetectionModesList"));
    auto *appearanceDuration = page.findChild<QSpinBox *>(
        QStringLiteral("ivaAppearanceDurationSpin"));
    auto *intrusionDuration = page.findChild<QSpinBox *>(
        QStringLiteral("ivaIntrusionDurationSpin"));
    auto *loiteringDuration = page.findChild<QSpinBox *>(
        QStringLiteral("ivaLoiteringDurationSpin"));
    bool allObjectFiltersChecked = objectFilters && objectFilters->count() == 6;
    if (objectFilters) {
        for (int row = 0; row < objectFilters->count(); ++row) {
            allObjectFiltersChecked = allObjectFiltersChecked
                && objectFilters->item(row)->checkState() == Qt::Checked;
        }
    }
    const auto hasUniformGrid = [](const QListWidget *list) {
        if (!list || list->count() == 0 || !list->gridSize().isValid()) return false;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->sizeHint() != list->gridSize()) return false;
        }
        return list->spacing() == 0
            && list->selectionMode() == QAbstractItemView::NoSelection;
    };
    if (!require(coordinateTable && coordinateTable->rowCount() == 3
                     && indexSpin && indexSpin->minimum() == 1
                     && indexSpin->maximum() == 4 && apply && !apply->isEnabled()
                     && allObjectFiltersChecked && allObjects && allObjects->isChecked()
                     && saveGroup && apply->text() == QStringLiteral("No changes")
                     && cameraSaveStatus
                     && cameraSaveStatus->text().contains(QStringLiteral("Loaded CH1"))
                     && areaCount && areaCount->text() == QStringLiteral("1 area")
                     && selectedTitle
                     && selectedTitle->text().contains(QStringLiteral("CH1 / Area 1 / parking-a"))
                     && selectedState && selectedState->text() == QStringLiteral("SAVED")
                     && geometryToggle && geometry && geometry->isHidden()
                     && channel1Button
                     && channel1Button->text().contains(QStringLiteral("ON / 1"))
                     && detectionModes && detectionModes->count() == 5
                     && !detectionModes->item(0)->icon().isNull()
                     && objectFilters && !objectFilters->item(0)->icon().isNull()
                     && hasUniformGrid(detectionModes)
                     && hasUniformGrid(objectFilters)
                     && objectFilters->item(2)->text() == QStringLiteral("Car")
                     && objectFilters->item(2)->data(Qt::UserRole).toString()
                            == QStringLiteral("Vehicle.Car")
                     && objectFilters->styleSheet().contains(
                            QStringLiteral("indicator:unchecked"))
                     && objectFilters->styleSheet().contains(
                            QStringLiteral("check.svg"))
                     && appearanceDuration && appearanceDuration->isEnabled()
                     && intrusionDuration && intrusionDuration->isEnabled()
                     && loiteringDuration && loiteringDuration->isEnabled(),
                 "selected rules must expose a readable editor and unified save state")) return 1;

    QListWidgetItem *carFilter = objectFilters->item(2);
    carFilter->setCheckState(Qt::Unchecked);
    if (!require(carFilter->checkState() == Qt::Unchecked
                     && !carFilter->icon().isNull(),
                 "unchecked object filters must retain their readable icon tile")) return 1;
    carFilter->setCheckState(Qt::Checked);

    const int appearanceValue = appearanceDuration->value();
    const int intrusionValue = intrusionDuration->value();
    const int loiteringValue = loiteringDuration->value();
    QListWidgetItem *intrusionMode = nullptr;
    QListWidgetItem *loiteringMode = nullptr;
    for (int row = 0; row < detectionModes->count(); ++row) {
        QListWidgetItem *item = detectionModes->item(row);
        const QString rawValue = item->data(Qt::UserRole).toString();
        if (rawValue == QStringLiteral("Intrusion")) intrusionMode = item;
        if (rawValue == QStringLiteral("Loitering")) loiteringMode = item;
    }
    if (!require(intrusionMode && loiteringMode,
                 "duration test requires Intrusion and Loitering modes")) return 1;
    intrusionMode->setCheckState(Qt::Unchecked);
    if (!require(appearanceDuration->isEnabled()
                     && !intrusionDuration->isEnabled()
                     && loiteringDuration->isEnabled(),
                 "each mode must control its related duration input")) return 1;
    loiteringMode->setCheckState(Qt::Unchecked);
    if (!require(!appearanceDuration->isEnabled()
                     && !intrusionDuration->isEnabled()
                     && !loiteringDuration->isEnabled()
                     && appearanceDuration->value() == appearanceValue
                     && intrusionDuration->value() == intrusionValue
                     && loiteringDuration->value() == loiteringValue,
                 "no selected mode must disable durations without clearing values")) return 1;
    intrusionMode->setCheckState(Qt::Checked);
    loiteringMode->setCheckState(Qt::Checked);

    auto *canvas = page.findChild<IvaVideoCanvas *>(QStringLiteral("ivaVideoCanvas"));
    auto *discard = page.findChild<QPushButton *>(QStringLiteral("ivaDiscardDraftButton"));
    auto *piSlot = page.findChild<QComboBox *>(QStringLiteral("ivaPiParkingSlotCombo"));
    auto *piStatus = page.findChild<QLabel *>(QStringLiteral("ivaPiRoiStatusLabel"));
    QString piRequestedSlot;
    ParkingRoi piRequestedRoi;
    quint64 piGeneration = 0;
    if (!require(piSlot
                     && piSlot->findText(QStringLiteral("P-01")) >= 0
                     && piSlot->findText(QStringLiteral("P-02")) >= 0
                     && piSlot->findText(QStringLiteral("P-03")) >= 0
                     && piSlot->findText(QStringLiteral("P-04")) >= 0,
                 "IVA Setup must expose P01-P04 as mapped Pi ROI targets")) return 1;
    QObject::connect(&page, &IvaSettingsPage::piRoiSaveRequested,
                     [&](const QString &slotId, const ParkingRoi &roi,
                         quint64 generation) {
        piRequestedSlot = slotId;
        piRequestedRoi = roi;
        piGeneration = generation;
    });
    page.resize(1180, 720);
    page.show();
    app.processEvents();
    auto *detailsSplitter = page.findChild<QSplitter *>(
        QStringLiteral("ivaRuleDetailsSplitter"));
    auto *workspaceSplitter = page.findChild<QSplitter *>(
        QStringLiteral("ivaWorkspaceSplitter"));
    auto *controlRow = page.findChild<QWidget *>(QStringLiteral("ivaTopControlRow"));
    auto *editorScrollArea = page.findChild<QScrollArea *>(
        QStringLiteral("ivaRuleEditorScrollArea"));
    if (!require(workspaceSplitter && workspaceSplitter->sizes().size() == 2,
                 "IVA workspace splitter must have video and rule panes")) return 1;
    if (!require(workspaceSplitter->sizes().at(1) >= 460,
                 "IVA rule pane must retain a usable width")) return 1;
    if (!require(detailsSplitter && editorScrollArea
                     && editorScrollArea->widgetResizable()
                     && !detailsSplitter->childrenCollapsible()
                     && detailsSplitter->handleWidth() == 8
                     && table->height() >= table->minimumHeight(),
                  "IVA rule table must remain readable while details use a resizable scroll viewport")) return 1;
    if (!require(controlRow && saveGroup && channel1Button && channelEnabled
                     && saveGroup->parentWidget() == controlRow
                     && saveGroup->sizeHint().width() < 800
                     && saveGroup->maximumHeight() == 42
                     && cameraWiseAiAreasTitle
                     && cameraSaveStatus->parentWidget()
                            && cameraSaveStatus->parentWidget()->objectName()
                                   == QStringLiteral("ivaSelectedAreaHeader")
                     && saveGroup->geometry().top() <= controlRow->height()
                     && saveGroup->geometry().right() >= controlRow->width() - 2
                     && saveGroup->mapToGlobal(QPoint(0, 0)).x()
                            > channelEnabled->mapToGlobal(QPoint(0, 0)).x()
                     && channelEnabled->mapToGlobal(QPoint(0, 0)).x()
                            > channel1Button->mapToGlobal(QPoint(0, 0)).x(),
                 "Channel enabled must stay beside the channel buttons while compact save information remains on the right")) return 1;
    geometryToggle->setChecked(true);
    app.processEvents();
    const int compactListHeight = detailsSplitter->sizes().at(0);
    detailsSplitter->setSizes({compactListHeight + 100,
                               qMax(1, detailsSplitter->height()
                                           - compactListHeight - 100)});
    app.processEvents();
    if (!require(detailsSplitter->sizes().at(0) >= compactListHeight + 80
                     && table->height() >= table->minimumHeight()
                     && geometry->isVisible()
                     && editorScrollArea->viewport()->height() > 0
                     && editorScrollArea->verticalScrollBar()->maximum() > 0
                     && editorScrollArea->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
                 "splitter and narrow-window scrolling must keep the IVA editor usable")) return 1;
    if (!require(canvas && canvas->frameCompatible() && canvas->drawMode(),
                 "shared matching RTSP frames must enable direct video dragging")) return 1;
    const QPoint newAreaStart = canvas->mapFromScene(QPointF(1200, 500));
    const QPoint newAreaEnd = canvas->mapFromScene(QPointF(1600, 900));
    QMouseEvent newAreaPress(QEvent::MouseButtonPress, QPointF(newAreaStart),
                             QPointF(canvas->viewport()->mapToGlobal(newAreaStart)),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &newAreaPress);
    QMouseEvent newAreaMove(QEvent::MouseMove, QPointF(newAreaEnd),
                            QPointF(canvas->viewport()->mapToGlobal(newAreaEnd)),
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &newAreaMove);
    QMouseEvent newAreaRelease(QEvent::MouseButtonRelease, QPointF(newAreaEnd),
                               QPointF(canvas->viewport()->mapToGlobal(newAreaEnd)),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &newAreaRelease);
    app.processEvents();
    if (!require(table->rowCount() == 1
                     && ivaStatus && ivaStatus->text().contains(
                            QStringLiteral("already exists"), Qt::CaseInsensitive),
                 "drawing with an occupied Parking Map/IVA mapping must not silently replace the saved area")) return 1;
    const QPoint existingMoveStart = canvas->mapFromScene(QPointF(40, 20));
    const QPoint existingMoveEnd = canvas->mapFromScene(QPointF(100, 50));
    QMouseEvent existingPress(QEvent::MouseButtonPress, QPointF(existingMoveStart),
                              QPointF(canvas->viewport()->mapToGlobal(existingMoveStart)),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &existingPress);
    QMouseEvent existingMove(QEvent::MouseMove, QPointF(existingMoveEnd),
                             QPointF(canvas->viewport()->mapToGlobal(existingMoveEnd)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &existingMove);
    QMouseEvent existingRelease(QEvent::MouseButtonRelease, QPointF(existingMoveEnd),
                                QPointF(canvas->viewport()->mapToGlobal(existingMoveEnd)),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &existingRelease);
    if (!require(table->item(0, 2)->text() == QStringLiteral("parking-a")
                     && cameraSaveStatus->text().contains(
                            QStringLiteral("unsaved Camera WiseAI draft"),
                            Qt::CaseInsensitive)
                     && selectedState->text() == QStringLiteral("UNSAVED"),
                 "editing an existing IVA box must preserve its camera rule identity")) return 1;
    discard->click();
    page.setPreviewFrame(0, QImage(1296, 760, QImage::Format_RGB32));
    app.processEvents();
    if (!require(piSlot && piStatus && !page.findChild<QCheckBox *>(
                     QStringLiteral("ivaIncludePiRoiCheck"))
                     && piStatus->text().contains(QStringLiteral("automatic"),
                                                   Qt::CaseInsensitive),
                 "Pi ROI must be automatic without an include checkbox")) return 1;
    piSlot->setCurrentText(QStringLiteral("EV-03"));
    if (!require(table->currentRow() < 0 && canvas->drawMode(),
                 "selecting an unmapped EV area must prepare direct creation")) return 1;
    const QPoint start = canvas->mapFromScene(QPointF(1200, 500));
    const QPoint end = canvas->mapFromScene(QPointF(1600, 900));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                      QPointF(canvas->viewport()->mapToGlobal(start)),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(end),
                     QPointF(canvas->viewport()->mapToGlobal(end)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end),
                        QPointF(canvas->viewport()->mapToGlobal(end)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &release);
    app.processEvents();
    if (!require(table->rowCount() == 2
                     && table->item(1, 1)->text() == QStringLiteral("3")
                     && table->item(1, 2)->text() == QStringLiteral("name3")
                     && table->item(1, 2)->toolTip().contains(QStringLiteral("4 points"))
                     && coordinateTable->rowCount() == 4
                     && discard && discard->isEnabled()
                     && apply->isEnabled()
                     && apply->text().contains(QStringLiteral("Camera + Pi")),
                 "EV03 direct dragging must create camera Area index 3 named name3")) return 1;
    if (!require(cameraSaveStatus->text().contains(
                     QStringLiteral("unsaved Camera WiseAI draft"),
                     Qt::CaseInsensitive),
                 "newly dragged polygons must be visibly marked as unsaved camera drafts")) return 1;
    int cameraApplyChannel = -1;
    QObject::connect(&page, &IvaSettingsPage::applyRequested,
                     [&](int channel, bool, const QList<IvaAreaDefinition> &) {
        cameraApplyChannel = channel;
    });
    QTimer::singleShot(50, []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (QAbstractButton *yesButton = box->button(QMessageBox::Yes)) {
                    yesButton->click();
                }
                return;
            }
        }
    });
    apply->click();
    if (!require(cameraApplyChannel == 0 && piGeneration == 0,
                 "combined save must send Camera WiseAI before Pi ROI")) return 1;
    page.setApplyStarted(0);
    IvaAreaConfiguration verifiedConfiguration = configuration;
    IvaAreaDefinition verifiedArea = area;
    verifiedArea.areaIndex = 3;
    verifiedArea.name = QStringLiteral("name3");
    verifiedArea.areaCoordinates = {QPointF(120, 50), QPointF(240, 50),
                                    QPointF(240, 150), QPointF(120, 150)};
    verifiedConfiguration.areas.append(verifiedArea);
    page.setApplySuccess(0, verifiedConfiguration);
    if (!require(piRequestedSlot == QStringLiteral("EV-03")
                     && piGeneration > (quint64(1) << 63),
                 "verified Camera save must continue with the selected Pi ROI")) return 1;
    page.setPiRoiResult(QStringLiteral("EV-03"), piRequestedRoi,
                        piGeneration - 1, true, true);
    if (!require(!apply->isEnabled(),
                 "stale Pi ROI responses must not finish the active request")) return 1;
    page.setPiRoiResult(QStringLiteral("EV-03"), piRequestedRoi,
                        piGeneration, true, true);
    if (!require(!apply->isEnabled()
                     && piStatus->text().contains(QStringLiteral("applied immediately")),
                 "verified Pi ROI responses must complete the request")) return 1;
    discard->click();
    if (!require(table->rowCount() == 2 && table->currentRow() == 1
                     && coordinateTable->rowCount() == 4 && !apply->isEnabled(),
                 "verified Camera save must refresh and retain the EV03 camera Area")) return 1;

    page.setRequestError(QStringLiteral("temporary failure"));
    if (!require(table->rowCount() == 2,
                 "request errors must retain the last-good table")) return 1;

    int refreshRequests = 0;
    QObject::connect(&page, &IvaSettingsPage::refreshRequested,
                     [&]() { ++refreshRequests; });
    auto *refresh = page.findChild<QPushButton *>(
        QStringLiteral("refreshIvaConfigurationButton"));
    auto *refreshTimer = page.findChild<QTimer *>(
        QStringLiteral("ivaConfigurationRefreshTimer"));
    if (!require(refresh && !refresh->isVisible() && refresh->isEnabled()
                     && refreshTimer && refreshTimer->interval() == 10000,
                 "manual refresh must stay hidden while the page uses a 10-second refresh timer")) return 1;
    refresh->click();
    if (!require(refreshRequests == 1 && !refresh->isEnabled(),
                 "refresh must emit once and guard duplicate requests")) return 1;

    page.setRequestError(QStringLiteral("refresh stopped for test"));
    piSlot->setCurrentText(QStringLiteral("EV-01"));

    auto *deleteArea = page.findChild<QPushButton *>(
        QStringLiteral("deleteSelectedIvaAreaButton"));
    int deletedChannel = -1;
    int deletedAreaIndex = -1;
    QObject::connect(&page, &IvaSettingsPage::deleteAreaRequested,
                     [&](int channel, int areaIndex) {
        deletedChannel = channel;
        deletedAreaIndex = areaIndex;
    });
    if (!require(deleteArea && deleteArea->isEnabled(),
                 "persisted IVA rules must expose the delete control")) return 1;
    QTimer::singleShot(50, []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (QAbstractButton *yesButton = box->button(QMessageBox::Yes)) {
                    yesButton->click();
                }
                return;
            }
        }
    });
    deleteArea->click();
    if (!require(deletedChannel == 0 && deletedAreaIndex == 1,
                 "Delete selected Area must request the camera's dedicated Area deletion API")) return 1;
    page.setApplyStarted(0);
    IvaAreaConfiguration deletedConfiguration = configuration;
    deletedConfiguration.areas.clear();
    deletedConfiguration.channels[0].rawContainer.insert(
        QStringLiteral("definedArea"), QJsonArray{});
    page.setConfiguration(deletedConfiguration);
    page.setApplySuccess(0, deletedConfiguration);
    if (!require(table->rowCount() == 0 && ivaStatus
                     && ivaStatus->text().contains(QStringLiteral("deleted and verified"))
                     && !deleteArea->isEnabled(),
                 "verified deletion must remove the Area and report camera Web Viewer synchronization")) return 1;

    IvaAreaConfiguration secondChannelConfiguration = configuration;
    secondChannelConfiguration.areas.clear();
    IvaAreaDefinition secondChannelArea = area;
    secondChannelArea.channel = 1;
    secondChannelArea.areaIndex = 2;
    secondChannelArea.name = QStringLiteral("parking-b");
    secondChannelConfiguration.areas.append(secondChannelArea);
    secondChannelConfiguration.channels[1].rawContainer.insert(
        QStringLiteral("definedArea"), QJsonArray{QJsonObject{}});
    page.setConfiguration(secondChannelConfiguration);
    auto *channel2Button = page.findChild<QPushButton *>(
        QStringLiteral("ivaPreviewChannel2Button"));
    channel2Button->click();
    table->selectRow(0);
    QTimer::singleShot(50, []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (QAbstractButton *yesButton = box->button(QMessageBox::Yes)) {
                    yesButton->click();
                }
                return;
            }
        }
    });
    deleteArea->click();
    if (!require(deletedChannel == 1 && deletedAreaIndex == 2,
                 "the same delete control must preserve CH2 and its selected Area index")) return 1;

    page.setCameraIp(QStringLiteral("192.0.2.11"));
    if (!require(table->rowCount() == 0 && !apply->isEnabled(),
                 "changing camera IP must clear values from the previous device")) return 1;

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("ivaHelpButton"));
    if (helpButton) helpButton->click();
    QApplication::processEvents();
    QDialog *helpDialog = page.findChild<QDialog *>(QStringLiteral("ivaHelpDialog"));
    QWidget *destinationFlow = helpDialog
        ? helpDialog->findChild<QWidget *>(QStringLiteral("ivaHelpDestinationFlow"))
        : nullptr;
    QWidget *setupFlow = helpDialog
        ? helpDialog->findChild<QWidget *>(QStringLiteral("ivaHelpSetupFlow"))
        : nullptr;
    QTableWidget *helpMapping = helpDialog
        ? helpDialog->findChild<QTableWidget *>(QStringLiteral("ivaHelpMappingTable"))
        : nullptr;
    QWidget *canvasLegend = helpDialog
        ? helpDialog->findChild<QWidget *>(QStringLiteral("ivaHelpCanvasLegend"))
        : nullptr;
    QWidget *actionMatrix = helpDialog
        ? helpDialog->findChild<QWidget *>(QStringLiteral("ivaHelpActionMatrix"))
        : nullptr;
    QWidget *verificationFlow = helpDialog
        ? helpDialog->findChild<QWidget *>(QStringLiteral("ivaHelpVerificationFlow"))
        : nullptr;
    QLabel *safetyNotes = helpDialog
        ? helpDialog->findChild<QLabel *>(QStringLiteral("ivaHelpSafetyNotes"))
        : nullptr;
    if (!require(helpButton && !helpButton->icon().isNull()
                     && helpButton->text() == QStringLiteral("IVA Setup 안내")
                     && helpDialog && destinationFlow && setupFlow && helpMapping
                     && canvasLegend && actionMatrix && verificationFlow && safetyNotes
                     && destinationFlow->findChildren<QFrame *>().size() >= 2
                     && setupFlow->findChildren<QFrame *>().size() >= 4
                     && helpMapping->rowCount() == 4
                     && helpMapping->item(2, 0)->text() == QStringLiteral("EV-03")
                     && helpMapping->item(2, 1)->text() == QStringLiteral("name3")
                     && canvasLegend->findChildren<QFrame *>().size() >= 4
                     && safetyNotes->text().contains(QStringLiteral("SHA-256")),
                 "IVA help must explain camera/Pi boundaries, mapping, canvas, and verification")) return 1;
    helpDialog->close();

    page.setParkingSelection(QStringLiteral("P-02"),
                             QStringLiteral("CH3"),
                             QStringLiteral("IVA2"));
    auto *parkingSlot = page.findChild<QComboBox *>(
        QStringLiteral("ivaPiParkingSlotCombo"));
    auto *ch3Button = page.findChild<QPushButton *>(
        QStringLiteral("ivaPreviewChannel3Button"));
    if (!require(parkingSlot && parkingSlot->currentText() == QStringLiteral("P-02")
                     && ch3Button && ch3Button->isChecked(),
                 "Parking Map P slots must select their channel and IVA Area in IVA Setup")) return 1;

    std::cout << "PASS: IVA settings page edits option-bounded rules and isolates camera state\n";
    return 0;
}
