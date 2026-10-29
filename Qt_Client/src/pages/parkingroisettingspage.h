#pragma once

#include "api/parkingroi.h"

#include <QImage>
#include <QWidget>

class IvaVideoCanvas;
class QComboBox;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;

class ParkingRoiSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ParkingRoiSettingsPage(QWidget *parent = nullptr);

    void setRoiList(const ParkingRoiMap &rois, quint64 generation);
    void setRoi(const QString &slotId, const ParkingRoi &roi,
                quint64 generation, bool afterSave,
                bool appliedImmediately);
    void setRequestError(const QString &slotId, const QString &message,
                         quint64 generation, bool saveRequest);
    void setPreviewFrame(const QImage &frame);

signals:
    void roiListRequested(quint64 generation);
    void roiRequested(const QString &slotId, quint64 generation);
    void roiSaveRequested(const QString &slotId, const ParkingRoi &roi,
                          quint64 generation);
    void previewFrameRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    quint64 nextGeneration();
    QString selectedSlotId() const;
    void requestAllRois();
    void requestSelectedRoi();
    void handleSlotChanged();
    void freezeCurrentFrame();
    void refreshFrame();
    void resetSelection(const QString &statusMessage);
    void updateCanvasOverlays();
    void updateCoordinateLabels();
    void updateButtons();
    void setStatus(const QString &message, bool error = false,
                   bool success = false);
    static QString normalizedText(const ParkingRoi &roi);
    static QString pixelText(const ParkingRoi &roi, const QSize &frameSize);
    static QString friendlyError(const QString &message);

    QComboBox *m_slotCombo = nullptr;
    QLabel *m_currentRoiLabel = nullptr;
    QLabel *m_selectedRoiLabel = nullptr;
    QLabel *m_pixelCoordinatesLabel = nullptr;
    QLabel *m_normalizedCoordinatesLabel = nullptr;
    QLabel *m_frameStatusLabel = nullptr;
    QLabel *m_serverStatusLabel = nullptr;
    IvaVideoCanvas *m_videoCanvas = nullptr;
    QPushButton *m_freezeButton = nullptr;
    QPushButton *m_refreshFrameButton = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_previewTimer = nullptr;
    ParkingRoiMap m_rois;
    ParkingRoi m_selectedRoi;
    QImage m_currentFrame;
    quint64 m_generation = 0;
    bool m_hasSelection = false;
    bool m_frozen = false;
    bool m_refreshPending = false;
    bool m_loadInFlight = false;
    bool m_saveInFlight = false;
};
