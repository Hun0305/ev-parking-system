#ifndef EVIDENCEPAGE_H
#define EVIDENCEPAGE_H

#include "api/parkingmodels.h"
#include "models/parkingcapturegroup.h"
#include "models/parkingstate.h"

#include <QByteArray>
#include <QList>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class EvidenceImageLabel;
class ImageLoader;
class QComboBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTimer;

class EvidencePage : public QWidget
{
    Q_OBJECT

public:
    explicit EvidencePage(QWidget *parent = nullptr);

    void setImageLoader(ImageLoader *imageLoader);
    void render(const ParkingViewState &state);
    // v0.1 client-side fallback: combine only evidence already delivered by
    // the existing slot/session APIs; no new history endpoint is assumed.
    void showLocalEvidenceSnapshot(const ParkingViewState &state);
    void showEvidence(const QString &slotId, SlotState state,
                      const QString &plateNumber,
                      const QList<ParkingImageResource> &images);
    void showLoading(const QString &slotId);
    void showError(const QString &slotId, const QString &message);
    void openEvent(const QString &eventId, const QString &slotId);
    void showEventEvidence(const QString &eventId, const QString &slotId,
                           qint64 sessionId, SlotState state,
                           const QString &plateNumber,
                           const QList<ParkingImageResource> &images);
    void showEventError(const QString &eventId, const QString &slotId,
                        const QString &message);
    bool selectSlot(const QString &slotId);
    QString currentSlotId() const;
    QString currentEventId() const;
    int captureCount() const;
    bool downloadSelectedPairsTo(const QString &directoryPath);

public slots:
    void requestCurrentEvidence();

signals:
    void slotEvidenceRequested(const QString &slotId);
    void eventEvidenceRequested(const QString &eventId);
    void evidenceDownloadFinished(bool success,
                                  const QString &message,
                                  const QStringList &files);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void requestEvidenceRefresh();
    bool mergeEvidenceCache(const ParkingViewState &state);
    void rebuildTimelineFilters(const ParkingViewState &state);
    void renderCaptureTable();
    void renderSelectedCapture(int pairIndex);
    void updateSlotCaptureCount(const QString &slotId, int captureCount);
    void updateSummaryMetrics(const QString &slotId,
                              SlotState state,
                              const QString &plateNumber,
                              qint64 sessionId,
                              int captureCount,
                              const QString &eventId);
    void renderCaptureCard(const ParkingCaptureGroup *capture,
                           const QString &heading,
                           EvidenceImageLabel *imageLabel,
                           QLabel *titleLabel,
                           QLabel *metadataLabel,
                           QPushButton *openButton,
                           const QString &requestRole);
    void clearCaptureCard(EvidenceImageLabel *imageLabel,
                          QLabel *titleLabel,
                          QLabel *metadataLabel,
                          QPushButton *openButton,
                          const QString &title,
                          const QString &message);
    void showFullImage(EvidenceImageLabel *source, const QString &title);
    void chooseEvidenceDownloadDirectory();
    void updateDownloadButtonState();
    void handleDownloadedImage(const QString &requestId,
                               const QByteArray &data,
                               const QString &contentType);
    void handleDownloadFailure(const QString &requestId,
                               const QString &message);
    void finishEvidenceDownload();

    struct LocalTimelineEntry {
        QString slotId;
        QString plateNumber;
        SlotState state = SlotState::Vacant;
        ParkingCaptureGroup capture;
    };

    struct LocalTimelinePair {
        QString key;
        QString slotId;
        QString plateNumber;
        SlotState state = SlotState::Vacant;
        qint64 sessionId = -1;
        int firstEntryIndex = -1;
        int latestEntryIndex = -1;
        int captureCount = 0;
    };

    struct EvidenceDownloadItem {
        QString requestId;
        QString slotId;
        qint64 sessionId = -1;
        QString position;
        qint64 captureId = -1;
        QDateTime capturedAt;
        QString ocr;
        QString plateNumber;
        QString reason;
        QString fileStem;
        QUrl sourceUrl;
    };

    SlotState stateForSlot(const ParkingViewState &state,
                           const QString &slotId) const;
    QString plateForSlot(const ParkingViewState &state,
                         const QString &slotId) const;
    void resetLocalTimeline();

    QPointer<ImageLoader> m_imageLoader;
    QComboBox *m_slotFilter = nullptr;
    QLineEdit *m_plateFilter = nullptr;
    QLineEdit *m_reasonFilter = nullptr;
    QLabel *m_slotMetricLabel = nullptr;
    QLabel *m_plateMetricLabel = nullptr;
    QLabel *m_sessionMetricLabel = nullptr;
    QLabel *m_captureMetricLabel = nullptr;
    EvidenceImageLabel *m_firstImageLabel = nullptr;
    EvidenceImageLabel *m_selectedImageLabel = nullptr;
    QLabel *m_firstTitleLabel = nullptr;
    QLabel *m_selectedTitleLabel = nullptr;
    QLabel *m_firstMetadataLabel = nullptr;
    QLabel *m_selectedMetadataLabel = nullptr;
    QPushButton *m_firstOpenButton = nullptr;
    QPushButton *m_selectedOpenButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QLabel *m_downloadSelectionLabel = nullptr;
    QTableWidget *m_captureTable = nullptr;
    QString m_currentSlotId;
    QString m_currentEventId;
    qint64 m_currentSessionId = -1;
    QString m_plateNumber;
    QVector<ParkingCaptureGroup> m_captures;
    QVector<LocalTimelineEntry> m_localTimelineEntries;
    QVector<LocalTimelineEntry> m_allLocalTimelineEntries;
    QVector<LocalTimelinePair> m_localTimelinePairs;
    ParkingViewState m_latestState;
    // Retains non-empty image lists observed during this application run. The
    // parking status snapshot is allowed to omit images without erasing the
    // locally available Evidence timeline.
    ParkingViewState m_evidenceCacheState;
    QSet<QString> m_currentSnapshotSlotIds;
    bool m_localTimelineMode = true;
    bool m_localTimelineInitialized = false;
    QHash<QString, int> m_slotCaptureCounts;
    QHash<QString, EvidenceImageLabel *> m_requestTargets;
    QHash<QString, EvidenceDownloadItem> m_pendingDownloads;
    QString m_downloadDirectory;
    QString m_downloadExportId;
    QStringList m_downloadManifestRows;
    QStringList m_downloadedFiles;
    int m_downloadFailureCount = 0;
    bool m_downloadInProgress = false;
    QTimer *m_autoRefreshTimer = nullptr;
    quint64 m_requestGeneration = 0;
    quint64 m_requestSequence = 0;
};

#endif
