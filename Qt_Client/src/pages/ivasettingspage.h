#pragma once

#include "api/parkingroi.h"
#include "iva/ivaareamodels.h"
#include "models/parkingzonelayout.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRectF;
class QShowEvent;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class IvaVideoCanvas;

class IvaSettingsPage : public QWidget
{
    Q_OBJECT

public:
    enum class PendingChangesDecision {
        Proceed,
        Waiting,
        Cancel,
    };

    explicit IvaSettingsPage(const QString &cameraIp, QWidget *parent = nullptr);

    bool hasPendingChanges() const;
    PendingChangesDecision confirmPendingChanges();
    void discardPendingChanges();
    void setCameraIp(const QString &cameraIp);
    void setParkingZoneMappings(const QList<ParkingZoneLayout> &mappings);
    void setParkingSelection(const QString &zoneId,
                             const QString &cameraChannel,
                             const QString &ivaAreaId);
    void setRequestStarted();
    void setOptions(const IvaAreaOptions &options);
    void setCapabilities(const WiseAiCapabilities &capabilities);
    void setConfiguration(const IvaAreaConfiguration &configuration);
    void setPreviewFrame(int channel, const QImage &frame);
    void setRequestError(const QString &message);
    void setApplyStarted(int channel);
    void setApplySuccess(int channel,
                         const IvaAreaConfiguration &verifiedConfiguration);
    void setApplyError(int channel,
                       const QString &message,
                       bool rollbackSucceeded);
    void setPiRoiResult(const QString &slotId, const ParkingRoi &roi,
                        quint64 generation, bool afterSave,
                        bool appliedImmediately);
    void setPiRoiError(const QString &slotId, const QString &message,
                       quint64 generation, bool saveRequest);

signals:
    void refreshRequested();
    void applyRequested(int channel,
                        bool enabled,
                        const QList<IvaAreaDefinition> &areas);
    void deleteAreaRequested(int channel, int areaIndex);
    void previewFrameRequested(int channel);
    void piRoiSaveRequested(const QString &slotId, const ParkingRoi &roi,
                            quint64 generation);
    void pendingChangesSaved();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class SaveStage {
        Idle,
        Camera,
        Pi,
    };

    static QString durationText(const IvaAreaDefinition &area);
    static QString coordinateText(double value);
    void showHelpDialog();
    void populateAreaTable();
    void populateEditor(int row);
    void clearEditor();
    void populateChecklist(QListWidget *list,
                           const QStringList &available,
                           const QStringList &selected,
                           bool emptyMeansAll = false);
    QStringList checkedValues(const QListWidget *list) const;
    bool collectEditedArea(IvaAreaDefinition &editedArea,
                           QString &errorMessage) const;
    void addCoordinateRow(double x, double y);
    void selectChannel(int channel);
    void selectMappedParkingArea();
    void selectParkingZoneForArea(int channel, int areaIndex);
    const ParkingZoneLayout *parkingZoneMapping(const QString &zoneId) const;
    int cameraChannelIndex(const QString &cameraChannel) const;
    int mappedParkingAreaIndex() const;
    QString mappedParkingAreaName() const;
    bool editorMatchesParkingArea() const;
    void updateVideoOverlays();
    void createRectangleDraft(const QRectF &sourceRectangle);
    void updateRectangleDraft(const QRectF &sourceRectangle);
    void discardRectangleDraft();
    void setCameraSaveFeedback(const QString &message,
                               const QString &styleSheet);
    void setEditorState(const QString &text,
                        const QString &background,
                        const QString &foreground);
    void updateEditorSummary();
    void updateDurationAvailability();
    void updateChannelButtons();
    void setEditorDirtyFeedback(bool geometryChanged = false);
    void startSaveChanges();
    bool startSaveChangesInternal(bool askConfirmation);
    void finishPendingSave(bool success);
    bool requestPiRoiSave();
    void updateButtons();

    QString m_cameraIp;
    QList<ParkingZoneLayout> m_parkingZoneMappings;
    QString m_selectedParkingZoneId;
    QString m_selectedParkingChannel;
    QString m_selectedParkingIvaAreaId;
    IvaAreaConfiguration m_configuration;
    IvaAreaConfiguration m_savedConfiguration;
    IvaAreaOptions m_options;
    WiseAiCapabilities m_capabilities;
    QLabel *m_cameraLabel = nullptr;
    QLabel *m_channelSummaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_areaCountLabel = nullptr;
    QLabel *m_selectedAreaTitleLabel = nullptr;
    QLabel *m_selectedAreaMetaLabel = nullptr;
    QLabel *m_selectedAreaStateLabel = nullptr;
    QLabel *m_detectionSelectionLabel = nullptr;
    QLabel *m_objectSelectionLabel = nullptr;
    QLabel *m_areaMappingLabel = nullptr;
    QLabel *m_piMappingLabel = nullptr;
    QTableWidget *m_areaTable = nullptr;
    IvaVideoCanvas *m_videoCanvas = nullptr;
    QLabel *m_frameStatusLabel = nullptr;
    QList<QPushButton *> m_channelButtons;
    QCheckBox *m_channelEnabledCheck = nullptr;
    QSpinBox *m_indexSpin = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QListWidget *m_detectionModesList = nullptr;
    QListWidget *m_objectFiltersList = nullptr;
    QCheckBox *m_allObjectFiltersCheck = nullptr;
    QLabel *m_appearanceDurationLabel = nullptr;
    QLabel *m_intrusionDurationLabel = nullptr;
    QLabel *m_loiteringDurationLabel = nullptr;
    QSpinBox *m_appearanceDurationSpin = nullptr;
    QSpinBox *m_intrusionDurationSpin = nullptr;
    QSpinBox *m_loiteringDurationSpin = nullptr;
    QTableWidget *m_coordinateTable = nullptr;
    QPushButton *m_addPointButton = nullptr;
    QPushButton *m_removePointButton = nullptr;
    QToolButton *m_geometryToggleButton = nullptr;
    QWidget *m_geometryWidget = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_deleteAreaButton = nullptr;
    QLabel *m_cameraSaveStatusLabel = nullptr;
    QPushButton *m_discardDraftButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QComboBox *m_piSlotCombo = nullptr;
    QLabel *m_piRoiStatusLabel = nullptr;
    QTimer *m_previewTimer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    int m_selectedArea = -1;
    int m_selectedChannel = 0;
    int m_draftChannel = -1;
    int m_draftAreaIndex = -1;
    bool m_draftReplacesExisting = false;
    IvaAreaDefinition m_draftOriginalArea;
    quint64 m_piRoiGeneration = (quint64(1) << 63);
    bool m_piRoiRequestInFlight = false;
    QString m_pendingPiSlotId;
    QSize m_currentPreviewFrameSize;
    int m_pendingDeletedAreaIndex = -1;
    QString m_pendingDeletedAreaName;
    bool m_requestInFlight = false;
    bool m_cameraDirty = false;
    bool m_piDirty = false;
    bool m_continueWithPiAfterCamera = false;
    bool m_cameraSavedBeforePi = false;
    bool m_pendingLeaveAfterSave = false;
    SaveStage m_saveStage = SaveStage::Idle;
    bool m_loadedOnce = false;
    bool m_hasOptions = false;
    bool m_hasCapabilities = false;
    bool m_hasSavedConfiguration = false;
    bool m_updatingEditor = false;
};
