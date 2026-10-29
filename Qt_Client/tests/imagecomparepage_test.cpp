#include "api/imageloader.h"
#include "pages/imagecomparepage.h"

#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
QByteArray pngBytes(const QColor &color)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ImageComparePage page;

    ParkingViewState state;
    EvSlotInfo evSlot;
    evSlot.slotId = QStringLiteral("EV-02");
    evSlot.state = SlotState::OvertimeAlert;
    state.evSlots.insert(evSlot.slotId, evSlot);
    ParkingSlotInfo generalSlot;
    generalSlot.slotId = QStringLiteral("P-01");
    generalSlot.state = SlotState::Vacant;
    state.parkingSlots.insert(generalSlot.slotId, generalSlot);
    page.render(state);

    QListWidget *slotList = page.findChild<QListWidget *>(
        QStringLiteral("imageCompareSlotList"));
    if (!slotList || slotList->count() != 2) return 1;
    if (page.currentSlotId() != QStringLiteral("EV-02")) return 2;

    int requestCount = 0;
    QString requestedSlot;
    QObject::connect(&page, &ImageComparePage::comparisonRequested,
                     [&](const QString &slotId) {
        ++requestCount;
        requestedSlot = slotId;
    });
    page.requestCurrentComparison();
    if (requestCount != 1 || requestedSlot != QStringLiteral("EV-02")) return 3;
    if (!page.selectSlot(QStringLiteral("P01"))
        || page.currentSlotId() != QStringLiteral("P-01")
        || requestCount != 2) return 4;
    if (!page.selectSlot(QStringLiteral("EV02"))
        || page.currentSlotId() != QStringLiteral("EV-02")
        || requestCount != 3) return 5;

    const QByteArray originalPng = pngBytes(QColor(QStringLiteral("#1976d2")));
    const QByteArray enhancedPng = pngBytes(QColor(QStringLiteral("#ff8f00")));
    const QByteArray laterCapturePng = pngBytes(QColor(QStringLiteral("#2e7d32")));
    if (originalPng.isEmpty() || enhancedPng.isEmpty()
        || laterCapturePng.isEmpty()) return 6;

    QTcpServer imageServer;
    if (!imageServer.listen(QHostAddress::LocalHost, 0)) return 7;
    QObject::connect(&imageServer, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket *socket = imageServer.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, originalPng, enhancedPng,
                              laterCapturePng]() {
                const QByteArray request = socket->readAll();
                const QByteArray body = request.contains("legacy/after")
                    ? laterCapturePng
                    : (request.contains("enhanced")
                           ? enhancedPng : originalPng);
                const QByteArray header =
                    "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "
                    + QByteArray::number(body.size())
                    + "\r\nConnection: close\r\n\r\n";
                socket->write(header);
                socket->write(body);
                socket->disconnectFromHost();
            });
        }
    });

    ImageLoader imageLoader(1500, true);
    page.setImageLoader(&imageLoader);
    const QString imageBaseUrl = QStringLiteral("http://127.0.0.1:%1/images/")
        .arg(imageServer.serverPort());

    ParkingImageResource firstOriginal;
    firstOriginal.imageId = 10;
    firstOriginal.processing = QStringLiteral("ORIGINAL");
    firstOriginal.url = QUrl(imageBaseUrl + QStringLiteral("10/original"));
    firstOriginal.timestamp = QDateTime::fromString(
        QStringLiteral("2026-07-29T09:00:00+09:00"), Qt::ISODate);

    ParkingImageResource latestOriginal;
    latestOriginal.imageId = 20;
    latestOriginal.processing = QStringLiteral("ORIGINAL");
    latestOriginal.url = QUrl(imageBaseUrl + QStringLiteral("20/original"));
    latestOriginal.timestamp = QDateTime::fromString(
        QStringLiteral("2026-07-29T10:00:00+09:00"), Qt::ISODate);
    latestOriginal.evidenceReason = QStringLiteral("OVERSTAY_EVIDENCE");
    latestOriginal.ocrResult = QStringLiteral("34B7788");

    ParkingImageResource latestEnhanced = latestOriginal;
    latestEnhanced.processing = QStringLiteral("ENHANCED");
    latestEnhanced.enhancementType = QStringLiteral("CLAHE");
    latestEnhanced.url = QUrl(
        imageBaseUrl + QStringLiteral("20/enhanced"));

    int loadedImageCount = 0;
    QEventLoop imageLoadLoop;
    QObject::connect(&imageLoader, &ImageLoader::imageLoaded,
                     [&](const QString &, const QPixmap &) {
        if (++loadedImageCount >= 2) imageLoadLoop.quit();
    });

    page.showComparison(QStringLiteral("EV-02"), SlotState::OvertimeAlert,
                        QStringLiteral("34B7788"),
                        {firstOriginal, latestOriginal, latestEnhanced});
    if (page.captureCount() != 2 || page.selectedImageId() != 20) return 8;
    QTableWidget *table = page.findChild<QTableWidget *>(
        QStringLiteral("imageCompareCaptureTable"));
    if (!table || table->rowCount() != 2 || table->currentRow() != 1) return 9;
    if (!table->item(1, 3) || table->item(1, 3)->text() != QStringLiteral("READY")
        || !table->item(1, 4)
        || table->item(1, 4)->text() != QStringLiteral("READY")) return 10;

    QLabel *summary = page.findChild<QLabel *>(
        QStringLiteral("imageCompareSummaryLabel"));
    if (!summary || !summary->text().contains(QStringLiteral("34B7788"))) return 11;
    QLabel *originalTitle = page.findChild<QLabel *>(
        QStringLiteral("imageCompareOriginalTitle"));
    QLabel *enhancedTitle = page.findChild<QLabel *>(
        QStringLiteral("imageCompareEnhancedTitle"));
    QLabel *originalImage = page.findChild<QLabel *>(
        QStringLiteral("imageCompareOriginalImage"));
    QLabel *enhancedImage = page.findChild<QLabel *>(
        QStringLiteral("imageCompareEnhancedImage"));
    if (!originalTitle || !originalTitle->text().startsWith(
            QStringLiteral("Original"))) return 12;
    if (!enhancedTitle || !enhancedTitle->text().startsWith(
            QStringLiteral("Enhanced"))) return 13;
    if (!originalImage || !enhancedImage) return 18;

    QTimer::singleShot(3000, &imageLoadLoop, &QEventLoop::quit);
    imageLoadLoop.exec();
    if (loadedImageCount != 2) return 14;
    const QPixmap displayedOriginal = originalImage->pixmap(Qt::ReturnByValue);
    const QPixmap displayedEnhanced = enhancedImage->pixmap(Qt::ReturnByValue);
    if (displayedOriginal.isNull() || displayedEnhanced.isNull()
        || displayedOriginal.cacheKey() == displayedEnhanced.cacheKey()) {
        return 19;
    }
    QPushButton *originalOpen = page.findChild<QPushButton *>(
        QStringLiteral("imageCompareOriginalOpenButton"));
    QPushButton *enhancedOpen = page.findChild<QPushButton *>(
        QStringLiteral("imageCompareEnhancedOpenButton"));
    if (!originalOpen || !originalOpen->isEnabled()
        || !enhancedOpen || !enhancedOpen->isEnabled()) return 15;

    ParkingImageResource originalOnly = latestOriginal;
    originalOnly.imageId = 30;
    originalOnly.url = QUrl(imageBaseUrl + QStringLiteral("30/original"));
    int originalOnlyLoadedImageCount = 0;
    QEventLoop originalOnlyLoadLoop;
    const QMetaObject::Connection originalOnlyConnection = QObject::connect(
        &imageLoader, &ImageLoader::imageLoaded,
        [&](const QString &, const QPixmap &) {
            if (++originalOnlyLoadedImageCount >= 1) {
                originalOnlyLoadLoop.quit();
            }
        });
    page.showComparison(QStringLiteral("EV-02"), SlotState::Occupied,
                        QStringLiteral("34B7788"), {originalOnly});
    if (page.captureCount() != 1 || page.selectedImageId() != 30) return 16;
    QLabel *status = page.findChild<QLabel *>(
        QStringLiteral("imageCompareStatusLabel"));
    if (!enhancedImage
        || !enhancedImage->text().contains(QStringLiteral("not available"),
                                           Qt::CaseInsensitive)
        || !status
        || !status->text().contains(QStringLiteral("no enhanced image"),
                                    Qt::CaseInsensitive)
        || enhancedOpen->isEnabled()) return 17;
    QTimer::singleShot(3000, &originalOnlyLoadLoop, &QEventLoop::quit);
    originalOnlyLoadLoop.exec();
    QObject::disconnect(originalOnlyConnection);
    if (originalOnlyLoadedImageCount != 1) return 25;

    ParkingImageResource legacyBefore;
    legacyBefore.role = QStringLiteral("VEHICLE");
    legacyBefore.processing = QStringLiteral("ORIGINAL");
    legacyBefore.url = QUrl(imageBaseUrl + QStringLiteral("legacy/before"));
    legacyBefore.timestamp = QDateTime::fromString(
        QStringLiteral("2026-07-29T11:00:00+09:00"), Qt::ISODate);
    ParkingImageResource legacyAfter = legacyBefore;
    legacyAfter.url = QUrl(imageBaseUrl + QStringLiteral("legacy/after"));
    // Matching role/timestamp still does not establish capture identity.
    legacyAfter.timestamp = legacyBefore.timestamp;

    int legacyLoadedImageCount = 0;
    QEventLoop legacyLoadLoop;
    QObject::connect(&imageLoader, &ImageLoader::imageLoaded,
                     [&](const QString &, const QPixmap &) {
        if (++legacyLoadedImageCount >= 1) legacyLoadLoop.quit();
    });
    page.showComparison(QStringLiteral("EV-02"), SlotState::Occupied,
                        QStringLiteral("34B7788"),
                        {legacyBefore, legacyAfter});
    if (page.captureCount() != 2 || table->rowCount() != 2
        || table->currentRow() != 1) return 20;
    if (!table->item(1, 3)
        || table->item(1, 3)->text() != QStringLiteral("READY")
        || !table->item(1, 4)
        || table->item(1, 4)->text() != QStringLiteral("N/A")) return 21;
    if (!enhancedImage->text().contains(QStringLiteral("not available"),
                                        Qt::CaseInsensitive)
        || enhancedOpen->isEnabled()) return 22;

    QTimer::singleShot(3000, &legacyLoadLoop, &QEventLoop::quit);
    legacyLoadLoop.exec();
    if (legacyLoadedImageCount != 1) return 23;
    const QPixmap displayedLaterCapture =
        originalImage->pixmap(Qt::ReturnByValue);
    if (displayedLaterCapture.isNull()
        || displayedLaterCapture.cacheKey() == displayedOriginal.cacheKey()) {
        return 24;
    }

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("imageCompareHelpButton"));
    if (!helpButton) return 26;
    helpButton->click();
    QApplication::processEvents();
    QDialog *helpDialog = page.findChild<QDialog *>(
        QStringLiteral("imageCompareHelpDialog"));
    QLabel *helpSteps = helpDialog
        ? helpDialog->findChild<QLabel *>(QStringLiteral("imageCompareHelpSteps"))
        : nullptr;
    if (!helpDialog || !helpSteps
        || !helpSteps->text().contains(QStringLiteral("ORIGINAL"))) return 27;
    helpDialog->close();

    return 0;
}
