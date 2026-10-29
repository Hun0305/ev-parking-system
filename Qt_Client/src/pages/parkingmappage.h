#ifndef PARKINGMAPPAGE_H
#define PARKINGMAPPAGE_H

#include "models/parkingstate.h"
#include "models/monitoringevent.h"
#include "models/parkingzonelayout.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPoint;
class QPointF;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class ImageLoader;

class ParkingMapPage : public QWidget
{
    Q_OBJECT

public:
    explicit ParkingMapPage(const QString &layoutPath, QWidget *parent = nullptr);
    ~ParkingMapPage() override;
    void render(const ParkingViewState &state);
    void appendEvent(const MonitoringEvent &event);
    void setImageLoader(ImageLoader *imageLoader);
    bool hasUnsavedLayoutChanges() const { return m_layoutDirty; }
    bool saveLayoutNow(QString *errorMessage = nullptr);
    QString channelDisplayName(const QString &channel) const;
    bool setChannelDisplayName(const QString &channel, const QString &displayName);
    QList<ParkingZoneLayout> parkingZoneMappings() const { return m_zones; }

signals:
    void layoutSaveResult(bool success, const QString &message);
    void layoutDirtyChanged(bool dirty);
    void slotDetailRequested(const QString &zoneId);
    void eventsRequested(const QString &zoneId, const QString &eventId);
    void evidenceRequested(const QString &zoneId, const QString &eventId);
    void cameraRequested(const QString &channel);
    void ivaSettingsRequested(const QString &zoneId,
                              const QString &cameraChannel,
                              const QString &ivaAreaId);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void undoLastLayoutChange();
    void addGeneralZone();
    void addEvZone();
    void deleteSelectedZone();
    void saveLayout();
    void reloadLayout();
    void resetDefaultLayout();
    void resetSelectedZoneShape();
    void handleSceneSelectionChanged();
    void handleZoneTableClicked(int row, int column);
    void handleZoneItemMoved(const QString &zoneId);
    void applyEditorFields();
    void setEditMode(bool enabled);
    void editChannelDisplayNames();
    void applyZoneFilters();
    void updateOverviewSearchResults();
    void openOverviewSearchResult(QListWidgetItem *item);
    void showOverview();
    void showCurrentZoneDetail();
    void editOverviewLayout();

private:
    struct LayoutSnapshot {
        QList<ParkingZoneLayout> zones;
        ParkingChannelDisplayNames channelDisplayNames;
        ParkingOverviewLayouts overviewLayouts;
        QString selectedZoneId;
    };

    void loadLayout();
    void showHelpDialog();
    LayoutSnapshot captureLayoutSnapshot() const;
    void pushUndoSnapshot(const LayoutSnapshot &snapshot);
    void pushCurrentLayoutToUndoHistory();
    void clearUndoHistory();
    void restoreLayoutSnapshot(const LayoutSnapshot &snapshot);
    bool layoutMatchesCleanSnapshot() const;
    void updateUndoButtonState();
    void beginEditorSliderGesture();
    void endEditorSliderGesture();
    QString exampleLayoutPath() const;
    QString nextZoneId(const QString &prefix) const;
    QRectF nextZoneRectForChannel(const QString &channel) const;
    int zoneIndexById(const QString &zoneId) const;
    QString selectedZoneId() const;
    ParkingZoneLayout *selectedZone();
    const ParkingZoneLayout *selectedZone() const;
    bool stateForZone(const QString &zoneId, SlotState *state) const;
    SlotVisualState visualStateForZone(const QString &zoneId, bool *known = nullptr) const;
    bool isParkingControlChannel(const QString &channel) const;
    QString zoneSearchText(const ParkingZoneLayout &zone) const;
    bool zoneMatchesFilters(const ParkingZoneLayout &zone) const;
    bool validateLayout(QString *errorMessage) const;
    void handleZoneItemDragStarted(const QString &zoneId);
    void showZoneContextMenu(const QString &zoneId, const QPoint &screenPos);
    void selectZoneById(const QString &zoneId);
    QString channelForScenePoint(const QPointF &point) const;
    QString nextIvaAreaForChannel(const QString &channel, const QString &excludeZoneId = QString()) const;
    bool ivaUsedInChannel(const QString &channel, const QString &ivaAreaId, const QString &excludeZoneId) const;
    void applyZoneTypeRules(ParkingZoneLayout *zone, const QString &preferredIva = QString()) const;
    void rebuildScene();
    void updateZoneVisual(const QString &zoneId);
    void updateAllZoneVisuals();
    void updateAlarmAnimationState();
    void updateAlarmPulse();
    void updateRuntimeStatusFromSelection();
    void updateOperationalSummary();
    void updateOverviewSummary();
    void positionOverviewSearchPopup();
    void rebuildOverviewScene();
    void updateOverviewScene();
    void updateRecentEvents();
    void updateVehicleImage();
    void updateEditorFromSelection();
    void updateZoneTable();
    void scrollMapToOrigin();
    void updateGeometrySliderLabels();
    void syncZonesFromItems();
    void syncItemsEditable();
    void markLayoutDirty(const QString &detail = QString());
    void setLayoutDirty(bool dirty, const QString &status = QString());
    QString channelPanelTitle(const QString &channel) const;

    QString m_layoutPath;
    QList<ParkingZoneLayout> m_zones;
    ParkingChannelDisplayNames m_channelDisplayNames;
    ParkingOverviewLayouts m_overviewLayouts;
    ParkingViewState m_lastState;
    QList<MonitoringEvent> m_recentEvents;
    bool m_editMode = false;
    bool m_updatingEditor = false;
    bool m_rebuildingScene = false;
    bool m_layoutDirty = false;
    QList<LayoutSnapshot> m_undoHistory;
    LayoutSnapshot m_pendingDragSnapshot;
    LayoutSnapshot m_editorSliderSnapshot;
    LayoutSnapshot m_cleanLayoutSnapshot;
    bool m_hasPendingDragSnapshot = false;
    bool m_editorSliderGestureActive = false;
    bool m_editorSliderSnapshotRecorded = false;

    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_mapView = nullptr;
    QGraphicsScene *m_overviewScene = nullptr;
    QGraphicsView *m_overviewMapView = nullptr;
    QStackedWidget *m_operationViewStack = nullptr;
    QHash<QString, QGraphicsRectItem *> m_zoneItems;
    QHash<QString, QGraphicsRectItem *> m_zoneAccentBars;
    QHash<QString, QGraphicsPathItem *> m_zoneTypeIcons;
    QHash<QString, QGraphicsSimpleTextItem *> m_zoneLabels;
    QHash<QString, QGraphicsSimpleTextItem *> m_zonePlateLabels;
    QHash<QString, QGraphicsSimpleTextItem *> m_zoneStateLabels;
    QHash<QString, QGraphicsSimpleTextItem *> m_zoneMetaLabels;
    QHash<QString, QGraphicsSimpleTextItem *> m_channelSummaryLabels;
    QHash<QString, QGraphicsSimpleTextItem *> m_corridorEventLabels;
    QHash<QString, QGraphicsEllipseItem *> m_zoneAlarmHalos;
    QHash<QString, QGraphicsPathItem *> m_zoneAlarmBeacons;
    QHash<QString, QGraphicsSimpleTextItem *> m_zoneAlarmLabels;
    QHash<QString, QGraphicsRectItem *> m_overviewSlotItems;
    QTableWidget *m_zoneTable = nullptr;
    QTimer *m_alarmPulseTimer = nullptr;
    QTimer *m_runtimeClockTimer = nullptr;
    double m_alarmPulsePhase = 0.0;

    QLabel *m_selectedTitleLabel = nullptr;
    QLabel *m_selectedMetaLabel = nullptr;
    QLabel *m_selectedStateLabel = nullptr;
    QLabel *m_runtimeDataStatusLabel = nullptr;
    QLabel *m_runtimeVehicleLabel = nullptr;
    QLabel *m_runtimeVehicleImageLabel = nullptr;
    QLabel *m_runtimeOccupiedSinceLabel = nullptr;
    QLabel *m_runtimeOccupiedTimeLabel = nullptr;
    QLabel *m_runtimeAlarmLabel = nullptr;
    QLabel *m_runtimeAlarmStateLabel = nullptr;
    QPushButton *m_editToggleButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_editChannelNamesButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QLineEdit *m_zoneIdEdit = nullptr;
    QLineEdit *m_displayNameEdit = nullptr;
    QComboBox *m_zoneTypeCombo = nullptr;
    QComboBox *m_cameraChannelCombo = nullptr;
    QComboBox *m_ivaAreaCombo = nullptr;
    QLineEdit *m_hallSensorEdit = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    QDoubleSpinBox *m_xSpin = nullptr;
    QDoubleSpinBox *m_ySpin = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QSlider *m_widthSlider = nullptr;
    QSlider *m_heightSlider = nullptr;
    QSlider *m_rotationSlider = nullptr;
    QLabel *m_widthValueLabel = nullptr;
    QLabel *m_heightValueLabel = nullptr;
    QLabel *m_rotationValueLabel = nullptr;
    QLabel *m_layoutStatusLabel = nullptr;
    QLabel *m_totalSummaryLabel = nullptr;
    QLabel *m_vacantSummaryLabel = nullptr;
    QLabel *m_occupiedSummaryLabel = nullptr;
    QLabel *m_waitingSummaryLabel = nullptr;
    QLabel *m_alertSummaryLabel = nullptr;
    QLabel *m_overviewTotalSummaryLabel = nullptr;
    QLabel *m_overviewVacantSummaryLabel = nullptr;
    QLabel *m_overviewOccupiedSummaryLabel = nullptr;
    QLabel *m_overviewAlertSummaryLabel = nullptr;
    QLabel *m_filterResultLabel = nullptr;
    QLineEdit *m_zoneSearchEdit = nullptr;
    QLineEdit *m_overviewSearchEdit = nullptr;
    QFrame *m_overviewSearchPopup = nullptr;
    QListWidget *m_overviewSearchResults = nullptr;
    QLabel *m_overviewSearchStatusLabel = nullptr;
    QComboBox *m_stateFilterCombo = nullptr;
    QTableWidget *m_recentEventsTable = nullptr;
    QPushButton *m_eventsButton = nullptr;
    QPushButton *m_evidenceButton = nullptr;
    QPushButton *m_cameraButton = nullptr;
    QPointer<ImageLoader> m_imageLoader;
    QString m_vehicleImageRequestId;
};

#endif
