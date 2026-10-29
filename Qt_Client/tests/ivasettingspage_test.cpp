#include "pages/ivasettingspage.h"
#include "iva/ivavideocanvas.h"

#include <QApplication>
#include <QAbstractButton>
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
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

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
    channelOptions.detectionModes = {QStringLiteral("Intrusion"),
                                     QStringLiteral("Loitering")};
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
    if (!require(table->item(0, 0)->text() == QStringLiteral("CH1")
                     && table->item(0, 3)->text() == QStringLiteral("parking-a"),
                 "table must show channel and rule identity")) return 1;
    auto *summary = page.findChild<QLabel *>(QStringLiteral("ivaChannelSummaryLabel"));
    if (!require(summary && summary->text().contains(QStringLiteral("CH2 OFF (0 areas)")),
                 "disabled empty camera channels must remain visible")) return 1;
    auto *coordinateTable = page.findChild<QTableWidget *>(
        QStringLiteral("ivaCoordinateTable"));
    auto *indexSpin = page.findChild<QSpinBox *>(QStringLiteral("ivaRuleIndexSpin"));
    auto *apply = page.findChild<QPushButton *>(
        QStringLiteral("applyIvaConfigurationButton"));
    auto *objectFilters = page.findChild<QListWidget *>(
        QStringLiteral("ivaObjectFiltersList"));
    bool allObjectFiltersChecked = objectFilters && objectFilters->count() == 6;
    if (objectFilters) {
        for (int row = 0; row < objectFilters->count(); ++row) {
            allObjectFiltersChecked = allObjectFiltersChecked
                && objectFilters->item(row)->checkState() == Qt::Checked;
        }
    }
    if (!require(coordinateTable && coordinateTable->rowCount() == 3
                     && indexSpin && indexSpin->minimum() == 1
                     && indexSpin->maximum() == 4 && apply && apply->isEnabled()
                     && allObjectFiltersChecked,
                 "selected rules must expose editable coordinates and camera option ranges")) return 1;

    auto *canvas = page.findChild<IvaVideoCanvas *>(QStringLiteral("ivaVideoCanvas"));
    auto *discard = page.findChild<QPushButton *>(QStringLiteral("ivaDiscardDraftButton"));
    auto *piSlot = page.findChild<QComboBox *>(QStringLiteral("ivaPiParkingSlotCombo"));
    auto *sendToPi = page.findChild<QPushButton *>(QStringLiteral("sendIvaRoiToPiButton"));
    auto *piStatus = page.findChild<QLabel *>(QStringLiteral("ivaPiRoiStatusLabel"));
    QString piRequestedSlot;
    ParkingRoi piRequestedRoi;
    quint64 piGeneration = 0;
    QObject::connect(&page, &IvaSettingsPage::piRoiSaveRequested,
                     [&](const QString &slotId, const ParkingRoi &roi,
                         quint64 generation) {
        piRequestedSlot = slotId;
        piRequestedRoi = roi;
        piGeneration = generation;
    });
    page.resize(1400, 900);
    page.show();
    app.processEvents();
    if (!require(canvas && canvas->frameCompatible() && canvas->drawMode(),
                 "shared matching RTSP frames must enable direct video dragging")) return 1;
    if (!require(piSlot && sendToPi && !sendToPi->isEnabled() && piStatus,
                 "unmapped IVA rules must not enable Pi ROI saving")) return 1;
    piSlot->setCurrentText(QStringLiteral("EV-03"));
    if (!require(table->currentRow() < 0 && canvas->drawMode(),
                 "selecting an unmapped EV area must prepare direct creation")) return 1;
    const QPoint start = canvas->mapFromScene(QPointF(1200, 500));
    const QPoint end = canvas->mapFromScene(QPointF(1600, 900));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(end),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &release);
    app.processEvents();
    if (!require(table->rowCount() == 2
                     && table->item(1, 2)->text() == QStringLiteral("3")
                     && table->item(1, 3)->text() == QStringLiteral("name3")
                     && table->item(1, 6)->text() == QStringLiteral("4")
                     && coordinateTable->rowCount() == 4
                     && discard && discard->isEnabled(),
                 "EV03 direct dragging must create camera Area index 3 named name3")) return 1;
    sendToPi->click();
    if (!require(piRequestedSlot == QStringLiteral("EV-03")
                     && piGeneration > (quint64(1) << 63),
                 "Pi ROI send must use the EV area selected for direct dragging")) return 1;
    page.setPiRoiResult(QStringLiteral("EV-03"), piRequestedRoi,
                        piGeneration - 1, true, true);
    if (!require(!sendToPi->isEnabled(),
                 "stale Pi ROI responses must not finish the active request")) return 1;
    page.setPiRoiResult(QStringLiteral("EV-03"), piRequestedRoi,
                        piGeneration, true, true);
    if (!require(sendToPi->isEnabled()
                     && piStatus->text().contains(QStringLiteral("applied immediately")),
                 "verified Pi ROI responses must complete the request")) return 1;
    discard->click();
    if (!require(table->rowCount() == 1 && table->currentRow() < 0
                     && coordinateTable->rowCount() == 0 && !apply->isEnabled(),
                 "discard draft must remove the unsaved EV03 camera Area")) return 1;

    page.setRequestError(QStringLiteral("temporary failure"));
    if (!require(table->rowCount() == 1,
                 "request errors must retain the last-good table")) return 1;

    int refreshRequests = 0;
    QObject::connect(&page, &IvaSettingsPage::refreshRequested,
                     [&]() { ++refreshRequests; });
    auto *refresh = page.findChild<QPushButton *>(
        QStringLiteral("refreshIvaConfigurationButton"));
    if (!require(refresh && refresh->isEnabled(),
                 "refresh control must be available after an error")) return 1;
    refresh->click();
    if (!require(refreshRequests == 1 && !refresh->isEnabled(),
                 "refresh must emit once and guard duplicate requests")) return 1;

    page.setRequestError(QStringLiteral("refresh stopped for test"));
    piSlot->setCurrentText(QStringLiteral("EV-01"));

    auto *deleteArea = page.findChild<QPushButton *>(
        QStringLiteral("deleteSelectedIvaAreaButton"));
    auto *ivaStatus = page.findChild<QLabel *>(QStringLiteral("ivaStatusLabel"));
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
    piSlot->setCurrentText(QStringLiteral("EV-02"));
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

    std::cout << "PASS: IVA settings page edits option-bounded rules and isolates camera state\n";
    return 0;
}
