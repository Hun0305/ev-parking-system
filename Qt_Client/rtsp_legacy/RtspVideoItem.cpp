#include "RtspVideoItem.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaObject>
#include <QPainter>
#include <QRectF>
#include <QTimer>
#include <QTimeZone>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

struct RtspVideoItem::WorkerState
{
    std::atomic_bool stopRequested { false };
};

namespace {
QString ffmpegErrorToString(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

void setOption(AVDictionary **options, const char *key, const char *value)
{
    av_dict_set(options, key, value, 0);
}

QString formatKstClock(qint64 epochMs)
{
    if (epochMs < 0) {
        return QStringLiteral("--:--:--.--- KST");
    }

    static const QTimeZone kstTimeZone("Asia/Seoul");
    return QDateTime::fromMSecsSinceEpoch(epochMs, kstTimeZone)
        .toString(QStringLiteral("HH:mm:ss.zzz 'KST'"));
}
}

int interruptCallback(void *opaque)
{
    auto *state = static_cast<RtspVideoItem::WorkerState *>(opaque);
    return state && state->stopRequested.load() ? 1 : 0;
}

RtspVideoItem::RtspVideoItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_reconnectTimer(new QTimer(this))
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAntialiasing(false);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (source() == m_reconnectSource) {
            startWorker();
        }
    });
}

RtspVideoItem::~RtspVideoItem()
{
    m_reconnectTimer->stop();
    stopWorker();
}

QString RtspVideoItem::source() const
{
    QMutexLocker locker(&m_mutex);
    return m_source;
}

void RtspVideoItem::setSource(const QString &source)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_source == source) {
            return;
        }
        m_source = source;
        m_frame = QImage();
        m_videoSize = QSize();
        m_startupDelayMs = -1;
        m_frameTimestampMs = -1;
        m_frameWallClockMs = -1;
        m_errorString.clear();
        m_pendingFrame = QImage();
        m_pendingStartupDelayMs = -1;
        m_pendingFrameTimestampMs = -1;
        m_pendingFrameWallClockMs = -1;
    }

    m_reconnectAttempt = 0;
    emit sourceChanged();
    emit videoSizeChanged();
    emit startupDelayMsChanged();
    emit frameTimestampMsChanged();
    emit frameWallClockMsChanged();
    emit frameClockTextChanged();
    emit errorStringChanged();
    update();
    startWorker();
}

QString RtspVideoItem::status() const
{
    QMutexLocker locker(&m_mutex);
    return m_status;
}

QString RtspVideoItem::errorString() const
{
    QMutexLocker locker(&m_mutex);
    return m_errorString;
}

QSize RtspVideoItem::videoSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_videoSize;
}

int RtspVideoItem::startupDelayMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_startupDelayMs;
}

qint64 RtspVideoItem::frameTimestampMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_frameTimestampMs;
}

qint64 RtspVideoItem::frameWallClockMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_frameWallClockMs;
}

QString RtspVideoItem::frameClockText() const
{
    qint64 frameWallClockMs = -1;
    {
        QMutexLocker locker(&m_mutex);
        frameWallClockMs = m_frameWallClockMs;
    }
    return formatKstClock(frameWallClockMs);
}

void RtspVideoItem::paint(QPainter *painter)
{
    QImage frame;
    {
        QMutexLocker locker(&m_mutex);
        frame = m_frame;
    }

    painter->fillRect(boundingRect(), Qt::black);
    if (frame.isNull()) {
        return;
    }

    const QSizeF itemSize = boundingRect().size();
    QSizeF frameSize = frame.size();
    frameSize.scale(itemSize, Qt::KeepAspectRatio);

    const QRectF target((itemSize.width() - frameSize.width()) / 2.0,
                        (itemSize.height() - frameSize.height()) / 2.0,
                        frameSize.width(),
                        frameSize.height());
    painter->drawImage(target, frame);
}

void RtspVideoItem::start()
{
    startWorker();
}

void RtspVideoItem::stop()
{
    stopWorker();
}

void RtspVideoItem::handleDecodedFrame(const QImage &image, int startupDelayMs, qint64 frameTimestampMs,
                                       qint64 frameWallClockMs)
{
    bool sizeChanged = false;
    bool startupChanged = false;
    bool timestampChanged = false;
    bool wallClockChanged = false;
    bool statusChangedNow = false;
    bool errorChangedNow = false;

    {
        QMutexLocker locker(&m_mutex);
        m_frame = image;
        if (m_videoSize != image.size()) {
            m_videoSize = image.size();
            sizeChanged = true;
        }
        if (m_startupDelayMs < 0 && startupDelayMs >= 0) {
            m_startupDelayMs = startupDelayMs;
            startupChanged = true;
        }
        if (m_frameTimestampMs != frameTimestampMs) {
            m_frameTimestampMs = frameTimestampMs;
            timestampChanged = true;
        }
        if (m_frameWallClockMs != frameWallClockMs) {
            m_frameWallClockMs = frameWallClockMs;
            wallClockChanged = true;
        }
        if (m_status != QStringLiteral("Playing")) {
            m_status = QStringLiteral("Playing");
            statusChangedNow = true;
        }
        if (!m_errorString.isEmpty()) {
            m_errorString.clear();
            errorChangedNow = true;
        }
        m_reconnectAttempt = 0;
    }

    if (statusChangedNow) {
        emit statusChanged();
    }
    if (errorChangedNow) {
        emit errorStringChanged();
    }
    if (sizeChanged) {
        emit videoSizeChanged();
    }
    if (startupChanged) {
        emit startupDelayMsChanged();
    }
    if (timestampChanged) {
        emit frameTimestampMsChanged();
    }
    if (wallClockChanged) {
        emit frameWallClockMsChanged();
        emit frameClockTextChanged();
    }
    update();
}

void RtspVideoItem::deliverPendingFrame()
{
    QImage image;
    int startupDelayMs = -1;
    qint64 frameTimestampMs = -1;
    qint64 frameWallClockMs = -1;
    {
        QMutexLocker locker(&m_mutex);
        image = std::move(m_pendingFrame);
        m_pendingFrame = QImage();
        startupDelayMs = m_pendingStartupDelayMs;
        m_pendingStartupDelayMs = -1;
        frameTimestampMs = m_pendingFrameTimestampMs;
        m_pendingFrameTimestampMs = -1;
        frameWallClockMs = m_pendingFrameWallClockMs;
        m_pendingFrameWallClockMs = -1;
    }

    if (!image.isNull()) {
        handleDecodedFrame(image, startupDelayMs, frameTimestampMs, frameWallClockMs);
    }

    m_frameDeliveryQueued.store(false);
    bool scheduleAgain = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_pendingFrame.isNull() && !m_frameDeliveryQueued.exchange(true)) {
            scheduleAgain = true;
        }
    }
    if (scheduleAgain) {
        QMetaObject::invokeMethod(this, "deliverPendingFrame", Qt::QueuedConnection);
    }
}

void RtspVideoItem::handleStatusChanged(const QString &status)
{
    setStatus(status);
}

void RtspVideoItem::handleErrorChanged(const QString &message)
{
    setErrorString(message);
}

void RtspVideoItem::handleStreamFailure(const QString &source, const QString &message)
{
    if (source != this->source()) {
        return;
    }

    setStatus(QStringLiteral("Reconnecting"));
    setErrorString(message);

    const int shift = std::min(m_reconnectAttempt, 5);
    const int delayMs = std::min(1000 * (1 << shift), 30000);
    ++m_reconnectAttempt;
    m_reconnectSource = source;
    m_reconnectTimer->start(delayMs);
}

void RtspVideoItem::startWorker()
{
    m_reconnectTimer->stop();

    QString currentSource;
    {
        QMutexLocker locker(&m_mutex);
        currentSource = m_source;
        m_frame = QImage();
        m_videoSize = QSize();
        m_startupDelayMs = -1;
        m_frameTimestampMs = -1;
        m_frameWallClockMs = -1;
        m_errorString.clear();
        m_pendingFrame = QImage();
        m_pendingStartupDelayMs = -1;
        m_pendingFrameTimestampMs = -1;
        m_pendingFrameWallClockMs = -1;
    }

    stopWorker();

    if (currentSource.isEmpty()) {
        setStatus(QStringLiteral("Waiting"));
        setErrorString(QString());
        return;
    }

    setStatus(QStringLiteral("Connecting"));
    setErrorString(QString());
    emit videoSizeChanged();
    emit startupDelayMsChanged();
    emit frameTimestampMsChanged();
    emit frameWallClockMsChanged();
    emit frameClockTextChanged();

    m_state = std::make_shared<WorkerState>();
    m_worker = std::thread(&RtspVideoItem::decodeLoop, this, currentSource, m_state);
}

void RtspVideoItem::stopWorker()
{
    if (m_state) {
        m_state->stopRequested.store(true);
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_state.reset();
}

void RtspVideoItem::setStatus(const QString &status)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_status != status) {
            m_status = status;
            changed = true;
        }
    }
    if (changed) {
        emit statusChanged();
    }
}

void RtspVideoItem::setErrorString(const QString &message)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_errorString != message) {
            m_errorString = message;
            changed = true;
        }
    }
    if (changed) {
        emit errorStringChanged();
    }
}

void RtspVideoItem::setVideoSize(const QSize &size)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_videoSize != size) {
            m_videoSize = size;
            changed = true;
        }
    }
    if (changed) {
        emit videoSizeChanged();
    }
}

void RtspVideoItem::setStartupDelayMs(int delayMs)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_startupDelayMs != delayMs) {
            m_startupDelayMs = delayMs;
            changed = true;
        }
    }
    if (changed) {
        emit startupDelayMsChanged();
    }
}

void RtspVideoItem::queueDecodedFrame(QImage image, int startupDelayMs, qint64 frameTimestampMs,
                                      qint64 frameWallClockMs)
{
    bool scheduleDelivery = false;
    {
        QMutexLocker locker(&m_mutex);
        m_pendingFrame = std::move(image);
        if (startupDelayMs >= 0 && m_pendingStartupDelayMs < 0 && m_startupDelayMs < 0) {
            m_pendingStartupDelayMs = startupDelayMs;
        }
        m_pendingFrameTimestampMs = frameTimestampMs;
        m_pendingFrameWallClockMs = frameWallClockMs;
        if (!m_frameDeliveryQueued.exchange(true)) {
            scheduleDelivery = true;
        }
    }

    if (scheduleDelivery) {
        QMetaObject::invokeMethod(this, "deliverPendingFrame", Qt::QueuedConnection);
    }
}

void RtspVideoItem::decodeLoop(QString source, std::shared_ptr<WorkerState> state)
{
    static std::once_flag networkInitFlag;
    std::call_once(networkInitFlag, []() { avformat_network_init(); });

    const qint64 startupStart = QDateTime::currentMSecsSinceEpoch();
    bool firstFrameDelivered = false;
    bool streamWallClockOffsetInitialized = false;
    qint64 streamWallClockOffsetMs = 0;

    AVFormatContext *formatContext = avformat_alloc_context();
    if (!formatContext) {
        QMetaObject::invokeMethod(this, "handleErrorChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("FFmpeg format context allocation failed.")));
        QMetaObject::invokeMethod(this, "handleStatusChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Error")));
        return;
    }
    formatContext->interrupt_callback.callback = interruptCallback;
    formatContext->interrupt_callback.opaque = state.get();

    AVDictionary *options = nullptr;
    setOption(&options, "rtsp_transport", "tcp");
    setOption(&options, "fflags", "nobuffer");
    setOption(&options, "flags", "low_delay");
    setOption(&options, "max_delay", "0");
    setOption(&options, "probesize", "1048576");
    setOption(&options, "analyzeduration", "1000000");
    setOption(&options, "reorder_queue_size", "0");
    setOption(&options, "stimeout", "3000000");
    setOption(&options, "timeout", "3000000");

    int result = avformat_open_input(&formatContext, source.toUtf8().constData(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0) {
        const QString message = QStringLiteral("FFmpeg open failed: ") + ffmpegErrorToString(result);
        avformat_free_context(formatContext);
        QMetaObject::invokeMethod(this, "handleStreamFailure", Qt::QueuedConnection,
                                  Q_ARG(QString, source), Q_ARG(QString, message));
        return;
    }


    result = avformat_find_stream_info(formatContext, nullptr);
    if (result < 0) {
        const QString message = QStringLiteral("Stream info failed: ") + ffmpegErrorToString(result);
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleStreamFailure", Qt::QueuedConnection,
                                  Q_ARG(QString, source), Q_ARG(QString, message));
        return;
    }

    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex < 0) {
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleErrorChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Video stream not found.")));
        QMetaObject::invokeMethod(this, "handleStatusChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Error")));
        return;
    }

    AVStream *videoStream = formatContext->streams[videoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleErrorChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Decoder not found.")));
        QMetaObject::invokeMethod(this, "handleStatusChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Error")));
        return;
    }

    AVCodecContext *codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleErrorChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Codec context allocation failed.")));
        QMetaObject::invokeMethod(this, "handleStatusChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Error")));
        return;
    }

    result = avcodec_parameters_to_context(codecContext, videoStream->codecpar);
    if (result < 0) {
        const QString message = QStringLiteral("Codec parameter copy failed: ") + ffmpegErrorToString(result);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleStreamFailure", Qt::QueuedConnection,
                                  Q_ARG(QString, source), Q_ARG(QString, message));
        return;
    }

    codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    const unsigned int logicalCores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned int reservedCores = logicalCores >= 8 ? 2u : 1u;
    const unsigned int usableCores = std::max(1u, logicalCores - reservedCores);
    codecContext->thread_count = static_cast<int>(std::clamp(usableCores / 4u, 1u, 4u));
    codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    result = avcodec_open2(codecContext, codec, nullptr);
    if (result < 0) {
        const QString message = QStringLiteral("Decoder open failed: ") + ffmpegErrorToString(result);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleStreamFailure", Qt::QueuedConnection,
                                  Q_ARG(QString, source), Q_ARG(QString, message));
        return;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgbFrame = av_frame_alloc();
    if (!packet || !frame || !rgbFrame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&rgbFrame);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        QMetaObject::invokeMethod(this, "handleErrorChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Frame allocation failed.")));
        QMetaObject::invokeMethod(this, "handleStatusChanged", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("Error")));
        return;
    }

    SwsContext *swsContext = nullptr;
    QByteArray rgbBuffer;
    int lastWidth = 0;
    int lastHeight = 0;
    AVPixelFormat lastFormat = AV_PIX_FMT_NONE;
    QString readFailure;

    while (!state->stopRequested.load()) {
        result = av_read_frame(formatContext, packet);
        if (result < 0) {
            if (result == AVERROR_EXIT || state->stopRequested.load()) {
                break;
            }
            if (result == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            readFailure = QStringLiteral("RTSP read failed: ") + ffmpegErrorToString(result);
            av_packet_unref(packet);
            break;
        }

        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        result = avcodec_send_packet(codecContext, packet);
        av_packet_unref(packet);
        if (result < 0) {
            continue;
        }

        while (!state->stopRequested.load()) {
            result = avcodec_receive_frame(codecContext, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                break;
            }
            if (result < 0) {
                break;
            }

            const int width = frame->width;
            const int height = frame->height;
            const auto format = static_cast<AVPixelFormat>(frame->format);
            if (!swsContext || width != lastWidth || height != lastHeight || format != lastFormat) {
                if (swsContext) {
                    sws_freeContext(swsContext);
                }
                swsContext = sws_getContext(width, height, format,
                                            width, height, AV_PIX_FMT_BGRA,
                                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                lastWidth = width;
                lastHeight = height;
                lastFormat = format;
                const int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, width, height, 1);
                rgbBuffer.resize(bufferSize);
                av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                                     reinterpret_cast<const uint8_t *>(rgbBuffer.constData()),
                                     AV_PIX_FMT_BGRA, width, height, 1);
            }

            if (!swsContext) {
                av_frame_unref(frame);
                continue;
            }

            sws_scale(swsContext, frame->data, frame->linesize, 0, height,
                      rgbFrame->data, rgbFrame->linesize);

            QImage image(reinterpret_cast<const uchar *>(rgbFrame->data[0]),
                         width, height, rgbFrame->linesize[0], QImage::Format_ARGB32);
            const int startupDelay = firstFrameDelivered
                                         ? -1
                                         : static_cast<int>(QDateTime::currentMSecsSinceEpoch() - startupStart);
            firstFrameDelivered = true;
            const int64_t timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                          ? frame->best_effort_timestamp
                                          : frame->pts;
            const qint64 frameTimestampMs = timestamp == AV_NOPTS_VALUE
                                                ? -1
                                                : av_rescale_q(timestamp, videoStream->time_base,
                                                               AVRational { 1, 1000 });
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            qint64 frameWallClockMs = nowMs;
            if (frameTimestampMs >= 0) {
                if (formatContext->start_time_realtime != AV_NOPTS_VALUE) {
                    const qint64 streamStartMs =
                        videoStream->start_time == AV_NOPTS_VALUE
                            ? 0
                            : av_rescale_q(videoStream->start_time, videoStream->time_base,
                                           AVRational { 1, 1000 });
                    frameWallClockMs = (formatContext->start_time_realtime / 1000)
                        + (frameTimestampMs - streamStartMs);
                } else {
                    if (!streamWallClockOffsetInitialized) {
                        streamWallClockOffsetMs = nowMs - frameTimestampMs;
                        streamWallClockOffsetInitialized = true;
                    }
                    frameWallClockMs = streamWallClockOffsetMs + frameTimestampMs;
                }
            }

            queueDecodedFrame(image.copy(), startupDelay, frameTimestampMs, frameWallClockMs);
            av_frame_unref(frame);
        }
    }

    if (swsContext) {
        sws_freeContext(swsContext);
    }
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);

    if (!readFailure.isEmpty() && !state->stopRequested.load()) {
        QMetaObject::invokeMethod(this, "handleStreamFailure", Qt::QueuedConnection,
                                  Q_ARG(QString, source), Q_ARG(QString, readFailure));
    }
}
