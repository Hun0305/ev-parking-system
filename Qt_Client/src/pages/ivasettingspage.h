#pragma once

#include "api/parkingroi.h"
#include "iva/ivaareamodels.h"

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
class IvaVideoCanvas;

class IvaSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit IvaSettingsPage(const QString &cameraIp, QWidget *parent = nullptr);

    void setCameraIp(const QString &cameraIp);
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

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
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
    int mappedParkingAreaIndex() const;
    QString mappedParkingAreaName() const;
    void updateVideoOverlays();
    void createRectangleDraft(const QRectF &sourceRectangle);
    void discardRectangleDraft();
    void updateButtons();

    QString m_cameraIp;
    IvaAreaConfiguration m_configuration;
    IvaAreaOptions m_options;
    WiseAiCapabilities m_capabilities;
    QLabel *m_cameraLabel = nullptr;
    QLabel *m_channelSummaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTableWidget *m_areaTable = nullptr;
    IvaVideoCanvas *m_videoCanvas = nullptr;
    QLabel *m_frameStatusLabel = nullptr;
    QList<QPushButton *> m_channelButtons;
    QCheckBox *m_channelEnabledCheck = nullptr;
    QSpinBox *m_indexSpin = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QListWidget *m_detectionModesList = nullptr;
    QListWidget *m_objectFiltersList = nullptr;
    QSpinBox *m_appearanceDurationSpin = nullptr;
    QSpinBox *m_intrusionDurationSpin = nullptr;
    QSpinBox *m_loiteringDurationSpin = nullptr;
    QTableWidget *m_coordinateTable = nullptr;
    QPushButton *m_addPointButton = nullptr;
    QPushButton *m_removePointButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_deleteAreaButton = nullptr;
    QPushButton *m_discardDraftButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QComboBox *m_piSlotCombo = nullptr;
    QPushButton *m_sendPiRoiButton = nullptr;
    QLabel *m_piRoiStatusLabel = nullptr;
    QTimer *m_previewTimer = nullptr;
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
    bool m_loadedOnce = false;
    bool m_hasOptions = false;
    bool m_hasCapabilities = false;
    bool m_updatingEditor = false;
};
