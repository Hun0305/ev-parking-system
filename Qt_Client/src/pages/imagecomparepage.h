#ifndef IMAGECOMPAREPAGE_H
#define IMAGECOMPAREPAGE_H

#include "models/parkingcapturegroup.h"
#include "models/parkingstate.h"

#include <QHash>
#include <QPointer>
#include <QVector>
#include <QWidget>

class ImageCompareImageLabel;
class ImageLoader;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTableWidget;

class ImageComparePage : public QWidget
{
    Q_OBJECT

public:
    explicit ImageComparePage(QWidget *parent = nullptr);

    void setImageLoader(ImageLoader *imageLoader);
    void render(const ParkingViewState &state);
    void showComparison(const QString &slotId, SlotState state,
                        const QString &plateNumber,
                        const QList<ParkingImageResource> &images);
    void showLoading(const QString &slotId);
    void showError(const QString &slotId, const QString &message);
    bool selectSlot(const QString &slotId);
    QString currentSlotId() const;
    int captureCount() const;
    qint64 selectedImageId() const;

public slots:
    void requestCurrentComparison();

signals:
    void comparisonRequested(const QString &slotId);

private:
    void handleSlotChanged(QListWidgetItem *current);
    void filterSlots(const QString &text);
    void renderCaptureTable();
    void renderSelectedCapture(int row);
    void renderVariantCard(const ParkingCaptureGroup *capture,
                           const QString &processing,
                           ImageCompareImageLabel *imageLabel,
                           QLabel *titleLabel,
                           QLabel *metadataLabel,
                           QPushButton *openButton,
                           const QString &requestRole);
    void clearVariantCard(ImageCompareImageLabel *imageLabel,
                          QLabel *titleLabel,
                          QLabel *metadataLabel,
                          QPushButton *openButton,
                          const QString &title,
                          const QString &message);
    void showFullImage(ImageCompareImageLabel *source, const QString &title);

    QPointer<ImageLoader> m_imageLoader;
    QListWidget *m_slotList = nullptr;
    QLineEdit *m_slotSearch = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    ImageCompareImageLabel *m_originalImageLabel = nullptr;
    ImageCompareImageLabel *m_enhancedImageLabel = nullptr;
    QLabel *m_originalTitleLabel = nullptr;
    QLabel *m_enhancedTitleLabel = nullptr;
    QLabel *m_originalMetadataLabel = nullptr;
    QLabel *m_enhancedMetadataLabel = nullptr;
    QPushButton *m_originalOpenButton = nullptr;
    QPushButton *m_enhancedOpenButton = nullptr;
    QTableWidget *m_captureTable = nullptr;
    QString m_currentSlotId;
    QString m_plateNumber;
    QVector<ParkingCaptureGroup> m_captures;
    int m_selectedCaptureRow = -1;
    QHash<QString, ImageCompareImageLabel *> m_requestTargets;
    quint64 m_requestGeneration = 0;
    quint64 m_requestSequence = 0;
};

#endif
