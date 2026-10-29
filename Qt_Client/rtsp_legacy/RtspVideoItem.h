#pragma once

#include <QImage>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickPaintedItem>
#include <QSize>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

class QTimer;

class RtspVideoItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(int startupDelayMs READ startupDelayMs NOTIFY startupDelayMsChanged)
    Q_PROPERTY(qint64 frameTimestampMs READ frameTimestampMs NOTIFY frameTimestampMsChanged)
    Q_PROPERTY(qint64 frameWallClockMs READ frameWallClockMs NOTIFY frameWallClockMsChanged)
    Q_PROPERTY(QString frameClockText READ frameClockText NOTIFY frameClockTextChanged)

public:
    explicit RtspVideoItem(QQuickItem *parent = nullptr);
    ~RtspVideoItem() override;

    QString source() const;
    void setSource(const QString &source);

    QString status() const;
    QString errorString() const;
    QSize videoSize() const;
    int startupDelayMs() const;
    qint64 frameTimestampMs() const;
    qint64 frameWallClockMs() const;
    QString frameClockText() const;
    QImage currentFrame() const;

    void paint(QPainter *painter) override;

public slots:
    void start();
    void stop();

signals:
    void sourceChanged();
    void statusChanged();
    void errorStringChanged();
    void videoSizeChanged();
    void startupDelayMsChanged();
    void frameTimestampMsChanged();
    void frameWallClockMsChanged();
    void frameClockTextChanged();

private slots:
    void handleDecodedFrame(const QImage &image, int startupDelayMs, qint64 frameTimestampMs,
                            qint64 frameWallClockMs);
    void deliverPendingFrame();
    void handleStatusChanged(const QString &status);
    void handleErrorChanged(const QString &message);
    void handleStreamFailure(const QString &source, const QString &message);

private:
    struct WorkerState;
    friend int interruptCallback(void *opaque);

    void startWorker();
    void stopWorker();
    void setStatus(const QString &status);
    void setErrorString(const QString &message);
    void setVideoSize(const QSize &size);
    void setStartupDelayMs(int delayMs);
    void queueDecodedFrame(QImage image, int startupDelayMs, qint64 frameTimestampMs,
                           qint64 frameWallClockMs);
    void decodeLoop(QString source, std::shared_ptr<WorkerState> state);

    mutable QMutex m_mutex;
    QString m_source;
    QString m_status = QStringLiteral("대기");
    QString m_errorString;
    QSize m_videoSize;
    int m_startupDelayMs = -1;
    qint64 m_frameTimestampMs = -1;
    qint64 m_frameWallClockMs = -1;
    QImage m_frame;
    QImage m_pendingFrame;
    int m_pendingStartupDelayMs = -1;
    qint64 m_pendingFrameTimestampMs = -1;
    qint64 m_pendingFrameWallClockMs = -1;
    std::atomic_bool m_frameDeliveryQueued { false };

    std::thread m_worker;
    std::shared_ptr<WorkerState> m_state;
    QTimer *m_reconnectTimer = nullptr;
    QString m_reconnectSource;
    int m_reconnectAttempt = 0;
};
