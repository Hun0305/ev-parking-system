#include "parkingmappage.h"

#include "api/imageloader.h"
#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QtMath>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace {
QString normalizedZoneId(const QString &raw)
{
    QString value = raw.trimmed().toUpper();
    value.replace(QLatin1Char(' '), QLatin1Char('_'));
    return value;
}

bool isIvaAreaId(const QString &value)
{
    return value == QStringLiteral("IVA1")
        || value == QStringLiteral("IVA2")
        || value == QStringLiteral("IVA3")
        || value == QStringLiteral("IVA4");
}

bool isCameraChannelId(const QString &value)
{
    return value == QStringLiteral("CH1")
        || value == QStringLiteral("CH2")
        || value == QStringLiteral("CH3")
        || value == QStringLiteral("CH4");
}

enum class OverviewChannelRole {
    Hidden,
    Parking,
    Entrance,
    Exit
};

struct OverviewLayoutMetrics {
    int gridRows = 3;
    int gridColumns = 4;
    qreal outerMargin = 16.0;
    qreal sceneHeaderHeight = 0.0;
    qreal cardSize = 300.0;
    qreal cardGap = 0.0;
    qreal cardHeaderHeight = 38.0;
    qreal channelEdgeInset = 12.0;
    qreal channelGap = 8.0;
    qreal horizontalChannelWidth = 130.0;
    qreal horizontalChannelHeight = 65.0;
    qreal verticalChannelWidth = 65.0;
    qreal verticalChannelHeight = 130.0;
    qreal accessLaneWidth = 86.0;
    qreal accessLaneHeight = 42.0;
    qreal accessGateInset = 10.0;

    constexpr qreal sceneWidth() const
    {
        return (outerMargin * 2.0)
            + (cardSize * gridColumns)
            + (cardGap * (gridColumns - 1));
    }

    constexpr qreal sceneHeight() const
    {
        return (outerMargin * 2.0) + sceneHeaderHeight
            + (cardSize * gridRows)
            + (cardGap * (gridRows - 1));
    }

    constexpr qreal channelFootprintWidth() const
    {
        return (verticalChannelWidth * 2.0)
            + (channelGap * 2.0)
            + horizontalChannelWidth;
    }
};

constexpr OverviewLayoutMetrics kOverviewLayoutMetrics{};
constexpr qreal kOverviewFitZoom = 0.75;
static_assert(kOverviewLayoutMetrics.channelFootprintWidth()
                  + (kOverviewLayoutMetrics.channelEdgeInset * 2.0)
              <= kOverviewLayoutMetrics.cardSize,
              "Overview channels must fit inside a camera card");

QLabel *addCompactSummaryChip(QWidget *parent, QHBoxLayout *row,
                               const QString &title, const QString &objectName,
                               const QString &accent)
{
    auto *chip = new QFrame(parent);
    chip->setFixedHeight(28);
    chip->setMinimumWidth(74);
    chip->setStyleSheet(QStringLiteral(
        "QFrame { background:#f8fafb; border:1px solid #c7d0d8; "
        "border-left:3px solid %1; border-radius:4px; }").arg(accent));
    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(6, 0, 6, 0);
    chipLayout->setSpacing(4);
    auto *titleLabel = new QLabel(title, chip);
    titleLabel->setStyleSheet(QStringLiteral(
        "border:none;color:#607d8b;font-size:10px;font-weight:700;"));
    auto *value = new QLabel(QStringLiteral("0"), chip);
    value->setObjectName(objectName);
    value->setStyleSheet(QStringLiteral(
        "border:none;color:#263238;font-size:13px;font-weight:900;"));
    chipLayout->addWidget(titleLabel);
    chipLayout->addWidget(value);
    row->addWidget(chip);
    return value;
}

OverviewChannelRole overviewChannelRole(const ParkingOverviewCameraLayout &camera,
                                        const QString &channel)
{
    const QString role = camera.channels.value(channel).role;
    if (role == QStringLiteral("PARKING")) return OverviewChannelRole::Parking;
    if (role == QStringLiteral("ENTRANCE")) return OverviewChannelRole::Entrance;
    if (role == QStringLiteral("EXIT")) return OverviewChannelRole::Exit;
    return OverviewChannelRole::Hidden;
}

QRectF overviewCameraRect(const ParkingOverviewCameraLayout &camera)
{
    const OverviewLayoutMetrics &metrics = kOverviewLayoutMetrics;
    return QRectF(
        metrics.outerMargin
            + (camera.gridColumn * (metrics.cardSize + metrics.cardGap)),
        metrics.outerMargin + metrics.sceneHeaderHeight
            + (camera.gridRow * (metrics.cardSize + metrics.cardGap)),
        metrics.cardSize, metrics.cardSize);
}

QRectF overviewChannelRect(const QRectF &cameraRect, const QString &channel)
{
    // Physical order is counter-clockwise from north: CH1 top, CH2 left,
    // CH3 bottom, and CH4 right.
    const OverviewLayoutMetrics &metrics = kOverviewLayoutMetrics;
    const qreal leftVerticalX = cameraRect.left() + metrics.channelEdgeInset;
    const qreal horizontalX = leftVerticalX
        + metrics.verticalChannelWidth + metrics.channelGap;
    const qreal rightVerticalX = horizontalX
        + metrics.horizontalChannelWidth + metrics.channelGap;
    const qreal topY = cameraRect.top() + metrics.cardHeaderHeight
        + metrics.channelEdgeInset;
    const qreal bottomY = cameraRect.bottom() - metrics.channelEdgeInset
        - metrics.horizontalChannelHeight;
    const qreal verticalCenterY = (topY + bottomY
                                   + metrics.horizontalChannelHeight) / 2.0;
    const qreal verticalY = verticalCenterY
        - (metrics.verticalChannelHeight / 2.0);

    const QString normalized = channel.trimmed().toUpper();
    if (normalized == QStringLiteral("CH1")) {
        return QRectF(horizontalX, topY,
                      metrics.horizontalChannelWidth,
                      metrics.horizontalChannelHeight);
    }
    if (normalized == QStringLiteral("CH2")) {
        return QRectF(leftVerticalX, verticalY,
                      metrics.verticalChannelWidth,
                      metrics.verticalChannelHeight);
    }
    if (normalized == QStringLiteral("CH3")) {
        return QRectF(horizontalX, bottomY,
                      metrics.horizontalChannelWidth,
                      metrics.horizontalChannelHeight);
    }
    if (normalized == QStringLiteral("CH4")) {
        return QRectF(rightVerticalX, verticalY,
                      metrics.verticalChannelWidth,
                      metrics.verticalChannelHeight);
    }
    return QRectF();
}

QRectF overviewAccessLaneRect(const QRectF &cameraRect,
                              const QRectF &channelRect,
                              const QString &channel)
{
    const OverviewLayoutMetrics &metrics = kOverviewLayoutMetrics;
    const QString normalized = channel.trimmed().toUpper();
    const bool northSouth = normalized == QStringLiteral("CH1")
        || normalized == QStringLiteral("CH3");
    if (northSouth) {
        const qreal laneWidth = metrics.accessLaneHeight;
        const qreal laneHeight = metrics.accessLaneWidth;
        const bool northSide = normalized == QStringLiteral("CH1");
        return QRectF(channelRect.center().x() - (laneWidth / 2.0),
                      northSide ? cameraRect.top()
                                : cameraRect.bottom() - laneHeight,
                      laneWidth, laneHeight);
    }

    const bool westSide = normalized == QStringLiteral("CH2");
    const qreal laneX = westSide
        ? cameraRect.left()
        : cameraRect.right() - metrics.accessLaneWidth;
    return QRectF(laneX,
                  channelRect.center().y() - (metrics.accessLaneHeight / 2.0),
                  metrics.accessLaneWidth,
                  metrics.accessLaneHeight);
}

constexpr qreal kParkingMapCanvasWidth = 920.0;
constexpr qreal kParkingMapBaseHeight = 460.0;

QRectF parkingSlotRectForChannel(const QRectF &panel, const QString &channel,
                                 int row, int column)
{
    Q_UNUSED(channel);
    constexpr qreal slotWidth = 124.0;
    constexpr qreal slotGap = 22.0;
    constexpr qreal slotBlockWidth = (slotWidth * 4.0) + (slotGap * 3.0);
    const qreal firstRowOffset = 58.0;
    const qreal slotOriginX = panel.x() + ((panel.width() - slotBlockWidth) / 2.0);
    return QRectF(slotOriginX + (column * (slotWidth + slotGap)),
                  panel.y() + firstRowOffset + (row * 94.0),
                  slotWidth, 72.0);
}

QString displayIvaText(const QString &ivaAreaId)
{
    return ivaAreaId.isEmpty() ? QStringLiteral("N/A") : ivaAreaId;
}

QCursor rotateCursor()
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arcRect(5, 5, 18, 18);
    const QPolygonF arrowHead = {
        QPointF(20.0, 5.0),
        QPointF(25.0, 7.5),
        QPointF(21.2, 11.4)
    };

    painter.setPen(QPen(QColor(QStringLiteral("#101418")), 3.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, 35 * 16, 280 * 16);
    painter.setBrush(QColor(QStringLiteral("#101418")));
    painter.drawPolygon(arrowHead);

    painter.setPen(QPen(QColor(QStringLiteral("#f7fbff")), 1.8, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(QColor(QStringLiteral("#f7fbff")));
    painter.drawArc(arcRect, 35 * 16, 280 * 16);
    painter.drawPolygon(arrowHead);

    return QCursor(pixmap, 14, 14);
}

class ParkingZoneGraphicsItem : public QGraphicsRectItem
{
public:
    explicit ParkingZoneGraphicsItem(const QString &zoneId, const QRectF &rect)
        : QGraphicsRectItem(rect)
        , m_zoneId(zoneId)
    {
    }

    void setEditingEnabled(bool enabled)
    {
        m_editingEnabled = enabled;
        setFlag(QGraphicsItem::ItemIsMovable, enabled);
        setAcceptHoverEvents(enabled);
        setCursor(enabled ? Qt::SizeAllCursor : Qt::ArrowCursor);
    }

    std::function<void(const QString &)> onDragStarted;
    std::function<void(const QString &)> onGeometryPreviewChanged;
    std::function<void(const QString &)> onDragFinished;
    std::function<void(const QString &, const QPoint &)> onContextMenuRequested;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == QGraphicsItem::ItemPositionChange && scene()) {
            QPointF newPos = value.toPointF();
            const QRectF sceneBounds = scene()->sceneRect();
            const QRectF itemRect = rect();
            const double maxX = sceneBounds.right() - itemRect.width();
            const double maxY = sceneBounds.bottom() - itemRect.height();

            if (newPos.x() < sceneBounds.left()) newPos.setX(sceneBounds.left());
            if (newPos.y() < sceneBounds.top()) newPos.setY(sceneBounds.top());
            if (newPos.x() > maxX) newPos.setX(maxX);
            if (newPos.y() > maxY) newPos.setY(maxY);
            return newPos;
        }

        return QGraphicsRectItem::itemChange(change, value);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (!m_editingEnabled) {
            m_interactionMode = InteractionMode::None;
            QGraphicsRectItem::mousePressEvent(event);
            return;
        }
        if (event->button() != Qt::LeftButton) {
            QGraphicsRectItem::mousePressEvent(event);
            return;
        }

        m_interactionMode = interactionModeFor(event->pos());
        m_pressItemPos = event->pos();
        m_pressScenePos = event->scenePos();
        m_startScenePos = pos();
        m_startRect = rect();
        m_startRotation = rotation();
        m_startAngle = angleFromCenter(event->scenePos());
        if (onDragStarted) onDragStarted(m_zoneId);

        if (m_interactionMode == InteractionMode::Move) {
            QGraphicsRectItem::mousePressEvent(event);
            return;
        }

        setSelected(true);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (m_interactionMode == InteractionMode::None
            || m_interactionMode == InteractionMode::Move) {
            QGraphicsRectItem::mouseMoveEvent(event);
            return;
        }

        applyHandleDrag(event->pos(), event->scenePos());
        if (onGeometryPreviewChanged) onGeometryPreviewChanged(m_zoneId);
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        const bool handledByItem = m_interactionMode == InteractionMode::None
            || m_interactionMode == InteractionMode::Move;
        if (handledByItem) {
            QGraphicsRectItem::mouseReleaseEvent(event);
        } else {
            event->accept();
        }

        if (event->button() == Qt::LeftButton && m_interactionMode != InteractionMode::None && onDragFinished) {
            onDragFinished(m_zoneId);
        }
        m_interactionMode = InteractionMode::None;
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override
    {
        if (onContextMenuRequested) {
            onContextMenuRequested(m_zoneId, event->screenPos());
            event->accept();
            return;
        }
        QGraphicsRectItem::contextMenuEvent(event);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override
    {
        if (!m_editingEnabled) {
            setCursor(Qt::ArrowCursor);
            QGraphicsRectItem::hoverMoveEvent(event);
            return;
        }
        setCursor(cursorForMode(interactionModeFor(event->pos())));
        QGraphicsRectItem::hoverMoveEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        setCursor(Qt::SizeAllCursor);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

private:
    enum class InteractionMode {
        None,
        Move,
        ResizeLeft,
        ResizeRight,
        ResizeTop,
        ResizeBottom,
        Rotate
    };

    static constexpr double kEdgeHandleSize = 7.0;
    static constexpr double kCornerHandleSize = 12.0;
    static constexpr double kMinimumSlotSize = 16.0;

    InteractionMode interactionModeFor(const QPointF &itemPoint) const
    {
        const QRectF itemRect = rect();
        const bool nearLeft = qAbs(itemPoint.x() - itemRect.left()) <= kEdgeHandleSize;
        const bool nearRight = qAbs(itemPoint.x() - itemRect.right()) <= kEdgeHandleSize;
        const bool nearTop = qAbs(itemPoint.y() - itemRect.top()) <= kEdgeHandleSize;
        const bool nearBottom = qAbs(itemPoint.y() - itemRect.bottom()) <= kEdgeHandleSize;
        const bool nearCorner =
            ((nearLeft || nearRight) && (nearTop || nearBottom))
            && (itemPoint.x() <= itemRect.left() + kCornerHandleSize
                || itemPoint.x() >= itemRect.right() - kCornerHandleSize)
            && (itemPoint.y() <= itemRect.top() + kCornerHandleSize
                || itemPoint.y() >= itemRect.bottom() - kCornerHandleSize);
        if (nearCorner) return InteractionMode::Rotate;
        if (nearLeft) return InteractionMode::ResizeLeft;
        if (nearRight) return InteractionMode::ResizeRight;
        if (nearTop) return InteractionMode::ResizeTop;
        if (nearBottom) return InteractionMode::ResizeBottom;
        return InteractionMode::Move;
    }

    QCursor cursorForMode(InteractionMode mode) const
    {
        switch (mode) {
        case InteractionMode::ResizeLeft:
        case InteractionMode::ResizeRight:
            return QCursor(Qt::SizeHorCursor);
        case InteractionMode::ResizeTop:
        case InteractionMode::ResizeBottom:
            return QCursor(Qt::SizeVerCursor);
        case InteractionMode::Rotate:
            return rotateCursor();
        case InteractionMode::Move:
        case InteractionMode::None:
            return QCursor(Qt::SizeAllCursor);
        }
        return QCursor(Qt::SizeAllCursor);
    }

    QPointF localVectorToScene(double localDx, double localDy) const
    {
        const double radians = qDegreesToRadians(m_startRotation);
        return QPointF((localDx * qCos(radians)) - (localDy * qSin(radians)),
                       (localDx * qSin(radians)) + (localDy * qCos(radians)));
    }

    double angleFromCenter(const QPointF &scenePoint) const
    {
        const QPointF center = mapToScene(rect().center());
        return qRadiansToDegrees(qAtan2(scenePoint.y() - center.y(),
                                        scenePoint.x() - center.x()));
    }

    void applyHandleDrag(const QPointF &itemPoint, const QPointF &scenePoint)
    {
        QRectF newRect = m_startRect;
        QPointF newPos = m_startScenePos;

        if (m_interactionMode == InteractionMode::Rotate) {
            const double delta = angleFromCenter(scenePoint) - m_startAngle;
            setRotation(m_startRotation + delta);
            return;
        }

        const QPointF delta = itemPoint - m_pressItemPos;
        if (m_interactionMode == InteractionMode::ResizeRight) {
            newRect.setWidth(qMax(kMinimumSlotSize, m_startRect.width() + delta.x()));
        } else if (m_interactionMode == InteractionMode::ResizeBottom) {
            newRect.setHeight(qMax(kMinimumSlotSize, m_startRect.height() + delta.y()));
        } else if (m_interactionMode == InteractionMode::ResizeLeft) {
            newRect.setWidth(qMax(kMinimumSlotSize, m_startRect.width() - delta.x()));
            const double localDx = m_startRect.width() - newRect.width();
            newPos = m_startScenePos + localVectorToScene(localDx, 0.0);
        } else if (m_interactionMode == InteractionMode::ResizeTop) {
            newRect.setHeight(qMax(kMinimumSlotSize, m_startRect.height() - delta.y()));
            const double localDy = m_startRect.height() - newRect.height();
            newPos = m_startScenePos + localVectorToScene(0.0, localDy);
        }

        setPos(newPos);
        setRect(QRectF(0, 0, newRect.width(), newRect.height()));
        setTransformOriginPoint(rect().center());
        setRotation(m_startRotation);
    }

    QString m_zoneId;
    InteractionMode m_interactionMode = InteractionMode::None;
    QPointF m_pressItemPos;
    QPointF m_pressScenePos;
    QPointF m_startScenePos;
    QRectF m_startRect;
    double m_startRotation = 0.0;
    double m_startAngle = 0.0;
    bool m_editingEnabled = false;
};

class OverviewMapGraphicsView : public QGraphicsView
{
public:
    explicit OverviewMapGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
    }

    std::function<void(const QPointF &)> onSceneDoubleClicked;
    std::function<void(int)> onZoomRequested;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onSceneDoubleClicked) {
            onSceneDoubleClicked(mapToScene(event->position().toPoint()));
            event->accept();
            return;
        }
        QGraphicsView::mouseDoubleClickEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers().testFlag(Qt::ControlModifier)
            && event->angleDelta().y() != 0 && onZoomRequested) {
            onZoomRequested(event->angleDelta().y());
            event->accept();
            return;
        }
        QGraphicsView::wheelEvent(event);
    }
};

QColor stateColor(SlotState state)
{
    switch (state) {
    case SlotState::Vacant: return QColor(QStringLiteral("#2ecc71"));
    case SlotState::Occupied: return QColor(QStringLiteral("#8da2b5"));
    case SlotState::NonEvAlert: return QColor(QStringLiteral("#e53935"));
    case SlotState::OvertimeAlert: return QColor(QStringLiteral("#fb8c00"));
    case SlotState::SensorError: return QColor(QStringLiteral("#7e57c2"));
    case SlotState::Acked: return QColor(QStringLiteral("#b0bec5"));
    }
    return QColor(QStringLiteral("#b0bec5"));
}

QColor slotFillColor(bool known, const SlotVisualState &visual, bool enabled)
{
    if (!enabled) return QColor(QStringLiteral("#2f353a"));
    if (!known) return QColor(QStringLiteral("#343b41"));
    if (visual.occupancy == SlotOccupancy::Vacant) {
        return QColor(QStringLiteral("#1f5a3a"));
    }
    if (visual.occupancy == SlotOccupancy::Unknown) {
        return QColor(QStringLiteral("#3c474f"));
    }
    switch (visual.vehicleClass) {
    case VehicleClass::Electric: return QColor(QStringLiteral("#155d7a"));
    case VehicleClass::General: return QColor(QStringLiteral("#46535f"));
    case VehicleClass::Unknown: return QColor(QStringLiteral("#39434b"));
    }
    return QColor(QStringLiteral("#343b41"));
}

QColor overviewSlotFillColor(bool known, const SlotVisualState &visual, bool enabled)
{
    if (!enabled) return QColor(QStringLiteral("#4b5359"));
    if (!known || visual.occupancy == SlotOccupancy::Unknown) {
        return QColor(QStringLiteral("#59646c"));
    }
    if (visual.occupancy == SlotOccupancy::Vacant) {
        return QColor(QStringLiteral("#2e7d32"));
    }
    switch (visual.vehicleClass) {
    case VehicleClass::Electric: return QColor(QStringLiteral("#1565c0"));
    case VehicleClass::General: return QColor(QStringLiteral("#546e7a"));
    case VehicleClass::Unknown: return QColor(QStringLiteral("#455a64"));
    }
    return QColor(QStringLiteral("#455a64"));
}

QString compactVehicleText(bool known, const SlotVisualState &visual)
{
    if (!known) return QStringLiteral("WAITING");
    if (visual.occupancy == SlotOccupancy::Vacant) return QStringLiteral("VACANT");
    if (visual.occupancy == SlotOccupancy::Unknown) return QStringLiteral("UNKNOWN");
    return vehicleClassText(visual.vehicleClass);
}

QString plateNumberForZone(const ParkingViewState &state, const QString &zoneId)
{
    const auto usablePlateNumber = [](const QString &rawPlateNumber) {
        const QString plateNumber = rawPlateNumber.trimmed();
        if (plateNumber.isEmpty() || plateNumber == QStringLiteral("-")
            || plateNumber.compare(QStringLiteral("N/A"),
                                   Qt::CaseInsensitive) == 0
            || plateNumber.compare(QStringLiteral("NOT PROVIDED"),
                                   Qt::CaseInsensitive) == 0) {
            return QString();
        }
        return plateNumber;
    };
    if (state.evSlots.contains(zoneId)) {
        const QString plateNumber = usablePlateNumber(
            state.evSlots.value(zoneId).plateNumber);
        if (!plateNumber.isEmpty()) return plateNumber;
    }
    return usablePlateNumber(state.slotPlateNumbers.value(zoneId));
}

QColor alarmColor(SlotAlarmKind alarm)
{
    switch (alarm) {
    case SlotAlarmKind::NonEvViolation: return QColor(QStringLiteral("#ff1744"));
    case SlotAlarmKind::Overstay: return QColor(QStringLiteral("#fb8c00"));
    case SlotAlarmKind::SensorError: return QColor(QStringLiteral("#b388ff"));
    case SlotAlarmKind::None: return QColor(QStringLiteral("#607d8b"));
    }
    return QColor(QStringLiteral("#607d8b"));
}

QString compactAlarmText(SlotAlarmKind alarm, bool acknowledged)
{
    if (acknowledged) return QStringLiteral("ACK");
    switch (alarm) {
    case SlotAlarmKind::NonEvViolation: return QStringLiteral("NON-EV");
    case SlotAlarmKind::Overstay: return QStringLiteral("OVER");
    case SlotAlarmKind::SensorError: return QStringLiteral("SENSOR");
    case SlotAlarmKind::None: return QString();
    }
    return QString();
}

QColor slotBorderColor(const ParkingZoneLayout &zone)
{
    return zone.zoneType == QStringLiteral("EV")
        ? QColor(QStringLiteral("#2d9cff"))
        : QColor(QStringLiteral("#ffd447"));
}

QColor slotTextColor(bool enabled)
{
    return enabled ? QColor(QStringLiteral("#f7fbff")) : QColor(QStringLiteral("#9aa5ad"));
}

QString slotMetaText(const ParkingZoneLayout &zone)
{
    Q_UNUSED(zone);
    return QString();
}

QString statePillStyle(bool known, SlotState state)
{
    const QColor color = known ? stateColor(state) : QColor(QStringLiteral("#607d8b"));
    const bool darkText = state == SlotState::Vacant || state == SlotState::OvertimeAlert || state == SlotState::Acked;
    return QStringLiteral("QLabel { background: %1; color: %2; border-radius: 4px; padding: 4px 7px; font-weight: 800; }")
        .arg(color.name(), darkText ? QStringLiteral("#101418") : QStringLiteral("#ffffff"));
}

QString displayStateText(bool known, SlotState state)
{
    return known ? slotStateText(state) : QStringLiteral("WAITING DATA");
}

QString durationTextFromSeconds(qint64 seconds)
{
    if (seconds < 0) return QStringLiteral("-");
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString runtimeDataStatusStyle(bool available)
{
    return QStringLiteral(
        "QLabel { color: %1; font-weight: 800; }")
        .arg(available ? QStringLiteral("#1b5e20") : QStringLiteral("#607d8b"));
}

QRectF channelPanelRect(const QString &channel)
{
    if (channel == QStringLiteral("CH2")) return QRectF(812, 190, 78, 86);
    if (channel == QStringLiteral("CH3")) return QRectF(30, 282, 860, 150);
    if (channel == QStringLiteral("CH4")) return QRectF(30, 190, 78, 86);
    return QRectF(30, 32, 860, 150);
}

bool sameZoneLayout(const ParkingZoneLayout &left, const ParkingZoneLayout &right)
{
    return left.zoneId == right.zoneId
        && left.zoneType == right.zoneType
        && left.displayName == right.displayName
        && left.slotOrder == right.slotOrder
        && left.rect == right.rect
        && left.rotation == right.rotation
        && left.cameraChannel == right.cameraChannel
        && left.ivaAreaId == right.ivaAreaId
        && left.hallSensorId == right.hallSensorId
        && left.enabled == right.enabled;
}

bool sameOverviewLayouts(const ParkingOverviewLayouts &left,
                        const ParkingOverviewLayouts &right)
{
    if (left.size() != right.size()) return false;
    for (const ParkingOverviewCameraLayout &leftCamera : left) {
        const auto rightCamera = std::find_if(
            right.cbegin(), right.cend(),
            [&leftCamera](const ParkingOverviewCameraLayout &camera) {
                return camera.cameraNumber == leftCamera.cameraNumber;
            });
        if (rightCamera == right.cend()
            || leftCamera.gridRow != rightCamera->gridRow
            || leftCamera.gridColumn != rightCamera->gridColumn) {
            return false;
        }
        for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
            const QString channel = QStringLiteral("CH%1").arg(channelNumber);
            const ParkingOverviewChannelLayout leftChannel = leftCamera.channels.value(channel);
            const ParkingOverviewChannelLayout rightChannel = rightCamera->channels.value(channel);
            if (leftChannel.role != rightChannel.role
                || leftChannel.slotCount != rightChannel.slotCount) {
                return false;
            }
        }
    }
    return true;
}

constexpr int kUndoHistoryLimit = 30;
}

ParkingMapPage::ParkingMapPage(const QString &layoutPath, QWidget *parent)
    : QWidget(parent)
    , m_layoutPath(layoutPath)
{
    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    m_operationViewStack = new QStackedWidget(this);
    m_operationViewStack->setObjectName(QStringLiteral("parkingMapOperationViewStack"));
    pageLayout->addWidget(m_operationViewStack);

    auto *overviewPage = new QWidget(m_operationViewStack);
    overviewPage->setObjectName(QStringLiteral("parkingMapOverviewPage"));
    auto *overviewLayout = new QVBoxLayout(overviewPage);
    overviewLayout->setContentsMargins(12, 12, 12, 12);
    overviewLayout->setSpacing(12);

    auto *overviewTitleRow = new QHBoxLayout;
    overviewTitleRow->setContentsMargins(0, 0, 0, 0);
    auto *overviewTitle = new QLabel(QStringLiteral("Parking Overview"), overviewPage);
    overviewTitle->setStyleSheet(QStringLiteral("color:#202124;font-size:20px;font-weight:900;"));
    auto *overviewLayoutButton = new QPushButton(QStringLiteral("Edit overview layout"), overviewPage);
    overviewLayoutButton->setObjectName(QStringLiteral("parkingOverviewLayoutEditButton"));
    overviewLayoutButton->setCursor(Qt::PointingHandCursor);
    const QString overviewZoomButtonStyle = QStringLiteral(
        "QPushButton { background:#eef3f6; color:#263238; border:1px solid #b8c4cc; "
        "border-radius:4px; padding:0; font-size:14px; font-weight:800; }"
        "QPushButton:hover { background:#dceaf3; border-color:#2d9cff; }"
        "QPushButton:pressed { background:#cbdce7; }");
    auto *overviewZoomOutButton = new QPushButton(QStringLiteral("-"), overviewPage);
    overviewZoomOutButton->setObjectName(QStringLiteral("parkingOverviewZoomOutButton"));
    overviewZoomOutButton->setToolTip(QStringLiteral("Zoom out (Ctrl+mouse wheel)"));
    auto *overviewZoomLabel = new QPushButton(QStringLiteral("100%"), overviewPage);
    overviewZoomLabel->setObjectName(QStringLiteral("parkingOverviewZoomLabel"));
    overviewZoomLabel->setToolTip(QStringLiteral("Reset zoom to fitted overview (100%)"));
    overviewZoomLabel->setFixedSize(48, 28);
    overviewZoomLabel->setCursor(Qt::PointingHandCursor);
    overviewZoomLabel->setStyleSheet(overviewZoomButtonStyle);
    auto *overviewZoomInButton = new QPushButton(QStringLiteral("+"), overviewPage);
    overviewZoomInButton->setObjectName(QStringLiteral("parkingOverviewZoomInButton"));
    overviewZoomInButton->setToolTip(QStringLiteral("Zoom in (Ctrl+mouse wheel)"));
    for (QPushButton *button : {overviewZoomOutButton, overviewZoomInButton}) {
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(overviewZoomButtonStyle);
    }

    overviewTitleRow->addWidget(overviewTitle);
    m_overviewTotalSummaryLabel = addCompactSummaryChip(
        overviewPage, overviewTitleRow, QStringLiteral("TOTAL"),
        QStringLiteral("parkingOverviewSummaryTotal"), QStringLiteral("#607d8b"));
    m_overviewVacantSummaryLabel = addCompactSummaryChip(
        overviewPage, overviewTitleRow, QStringLiteral("VACANT"),
        QStringLiteral("parkingOverviewSummaryVacant"), QStringLiteral("#2ecc71"));
    m_overviewOccupiedSummaryLabel = addCompactSummaryChip(
        overviewPage, overviewTitleRow, QStringLiteral("OCCUPIED"),
        QStringLiteral("parkingOverviewSummaryOccupied"), QStringLiteral("#2d9cff"));
    m_overviewAlertSummaryLabel = addCompactSummaryChip(
        overviewPage, overviewTitleRow, QStringLiteral("ALERT"),
        QStringLiteral("parkingOverviewSummaryAlert"), QStringLiteral("#ff1744"));
    overviewTitleRow->addStretch();
    overviewTitleRow->addWidget(overviewZoomOutButton);
    overviewTitleRow->addWidget(overviewZoomLabel);
    overviewTitleRow->addWidget(overviewZoomInButton);
    overviewTitleRow->addWidget(overviewLayoutButton);
    m_overviewSearchEdit = new QLineEdit(overviewPage);
    m_overviewSearchEdit->setObjectName(QStringLiteral("parkingOverviewSearchEdit"));
    m_overviewSearchEdit->setPlaceholderText(
        QStringLiteral("Search plate, zone, channel"));
    m_overviewSearchEdit->setClearButtonEnabled(true);
    m_overviewSearchEdit->setMaximumWidth(260);
    m_overviewSearchEdit->setToolTip(
        QStringLiteral("Search a vehicle plate, parking zone, or camera channel"));
    overviewTitleRow->insertWidget(1, m_overviewSearchEdit);
    overviewLayout->addLayout(overviewTitleRow);
    connect(overviewLayoutButton, &QPushButton::clicked,
            this, &ParkingMapPage::editOverviewLayout);

    m_overviewSearchPopup = new QFrame(overviewPage);
    m_overviewSearchPopup->setObjectName(QStringLiteral("parkingOverviewSearchPopup"));
    m_overviewSearchPopup->setStyleSheet(QStringLiteral(
        "QFrame#parkingOverviewSearchPopup { background:#ffffff;"
        "border:1px solid #aebbc5;border-radius:5px; }"));
    auto *overviewSearchPopupLayout = new QVBoxLayout(m_overviewSearchPopup);
    overviewSearchPopupLayout->setContentsMargins(4, 4, 4, 4);
    overviewSearchPopupLayout->setSpacing(3);

    m_overviewSearchStatusLabel = new QLabel(m_overviewSearchPopup);
    m_overviewSearchStatusLabel->setObjectName(QStringLiteral("parkingOverviewSearchStatusLabel"));
    m_overviewSearchStatusLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:10px;font-weight:700;"));
    m_overviewSearchStatusLabel->setVisible(false);
    overviewSearchPopupLayout->addWidget(m_overviewSearchStatusLabel);

    m_overviewSearchResults = new QListWidget(m_overviewSearchPopup);
    m_overviewSearchResults->setObjectName(QStringLiteral("parkingOverviewSearchResults"));
    m_overviewSearchResults->setMaximumHeight(126);
    m_overviewSearchResults->setSelectionMode(QAbstractItemView::SingleSelection);
    m_overviewSearchResults->setStyleSheet(QStringLiteral(
        "QListWidget { background:#f8fafb;border:0;padding:2px; }"
        "QListWidget::item { padding:5px 7px; }"
        "QListWidget::item:hover { background:#eef5f9; }"
        "QListWidget::item:selected { background:#dceaf5;color:#10212c; }"));
    m_overviewSearchResults->setVisible(false);
    overviewSearchPopupLayout->addWidget(m_overviewSearchResults);
    m_overviewSearchPopup->setVisible(false);
    m_overviewSearchPopup->raise();

    auto *overviewMapGroup = new QGroupBox(overviewPage);
    overviewMapGroup->setObjectName(QStringLiteral("parkingOverviewMapGroup"));
    auto *overviewMapLayout = new QVBoxLayout(overviewMapGroup);
    overviewMapLayout->setContentsMargins(8, 10, 8, 8);
    m_overviewScene = new QGraphicsScene(
        0, 0,
        kOverviewLayoutMetrics.sceneWidth(),
        kOverviewLayoutMetrics.sceneHeight(),
        this);
    m_overviewScene->setBackgroundBrush(QColor(QStringLiteral("#1b1f23")));
    auto *overviewMapView = new OverviewMapGraphicsView(m_overviewScene, overviewMapGroup);
    overviewMapView->onSceneDoubleClicked = [this](const QPointF &scenePosition) {
        const auto liveCamera = std::find_if(
            m_overviewLayouts.cbegin(), m_overviewLayouts.cend(),
            [](const ParkingOverviewCameraLayout &camera) { return camera.cameraNumber == 1; });
        if (liveCamera == m_overviewLayouts.cend()
            || !overviewCameraRect(*liveCamera).contains(scenePosition)) {
            return;
        }

        for (auto it = m_overviewSlotItems.cbegin(); it != m_overviewSlotItems.cend(); ++it) {
            QGraphicsRectItem *slot = it.value();
            if (slot && slot->contains(slot->mapFromScene(scenePosition))) {
                selectZoneById(it.key());
                break;
            }
        }
        showCurrentZoneDetail();
    };
    m_overviewMapView = overviewMapView;
    m_overviewMapView->setObjectName(QStringLiteral("parkingOverviewMapView"));
    m_overviewMapView->setMinimumHeight(400);
    m_overviewMapView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_overviewMapView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_overviewMapView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_overviewMapView->setRenderHint(QPainter::Antialiasing, true);
    m_overviewMapView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_overviewMapView->setCursor(Qt::OpenHandCursor);
    m_overviewMapView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_overviewMapView->scale(kOverviewFitZoom, kOverviewFitZoom);
    m_overviewMapView->setProperty("overviewZoom", 1.0);
    m_overviewMapView->setToolTip(QStringLiteral(
        "Left-drag to pan. Double-click Camera 1 to open detail. Ctrl+mouse wheel zooms."));
    m_overviewMapView->setStyleSheet(QStringLiteral(
        "QGraphicsView { background:#1b1f23;border:1px solid #3f4a53;border-radius:5px; }"));
    overviewMapLayout->addWidget(m_overviewMapView);
    overviewLayout->addWidget(overviewMapGroup, 1);
    overviewLayout->addStretch();
    const auto applyOverviewZoom = [this, overviewZoomLabel](qreal requestedZoom) {
        if (!m_overviewMapView) return;
        const qreal currentZoom = m_overviewMapView->property("overviewZoom").toReal();
        const qreal normalizedCurrentZoom = currentZoom > 0.0 ? currentZoom : 1.0;
        const qreal zoom = qBound(0.55, requestedZoom, 2.5);
        m_overviewMapView->scale(zoom / normalizedCurrentZoom,
                                  zoom / normalizedCurrentZoom);
        m_overviewMapView->setProperty("overviewZoom", zoom);
        overviewZoomLabel->setText(QStringLiteral("%1%")
                                       .arg(qRound(zoom * 100.0)));
    };
    overviewMapView->onZoomRequested = [this, applyOverviewZoom](int delta) {
        const qreal currentZoom = m_overviewMapView
            ? m_overviewMapView->property("overviewZoom").toReal() : 1.0;
        applyOverviewZoom((currentZoom > 0.0 ? currentZoom : 1.0)
                          * (delta > 0 ? 1.15 : (1.0 / 1.15)));
    };
    connect(overviewZoomOutButton, &QPushButton::clicked, this, [this, applyOverviewZoom]() {
        const qreal currentZoom = m_overviewMapView
            ? m_overviewMapView->property("overviewZoom").toReal() : 1.0;
        applyOverviewZoom((currentZoom > 0.0 ? currentZoom : 1.0) / 1.15);
    });
    connect(overviewZoomLabel, &QPushButton::clicked, this, [applyOverviewZoom]() {
        applyOverviewZoom(1.0);
    });
    connect(overviewZoomInButton, &QPushButton::clicked, this, [this, applyOverviewZoom]() {
        const qreal currentZoom = m_overviewMapView
            ? m_overviewMapView->property("overviewZoom").toReal() : 1.0;
        applyOverviewZoom((currentZoom > 0.0 ? currentZoom : 1.0) * 1.15);
    });
    m_operationViewStack->addWidget(overviewPage);

    auto *detailPage = new QWidget(m_operationViewStack);
    detailPage->setObjectName(QStringLiteral("parkingMapZoneDetailPage"));
    auto *detailLayout = new QHBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(10);
    m_operationViewStack->addWidget(detailPage);

    auto *mapGroup = new QGroupBox(QStringLiteral("Camera 1 Detail"), detailPage);
    auto *mapLayout = new QVBoxLayout(mapGroup);
    // QGroupBox titles occupy the top border band. Keep the detail toolbar and
    // status row below that band so the title never overlaps a subtitle/chip.
    mapLayout->setContentsMargins(12, 30, 12, 12);
    mapLayout->setSpacing(8);

    // Layout data is retained for configuration compatibility, but this page is
    // deliberately an operator view.  Position/mapping controls belong to the
    // dedicated settings tabs, not beside the live parking map.
    auto *adminToolbar = new QWidget(mapGroup);
    adminToolbar->setObjectName(QStringLiteral("parkingMapAdminToolbar"));
    auto *toolbarLayout = new QHBoxLayout(adminToolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    m_editToggleButton = new QPushButton(QStringLiteral("Admin layout edit"), mapGroup);
    m_editToggleButton->setCheckable(true);
    m_undoButton = new QPushButton(QStringLiteral("Undo"), mapGroup);
    m_undoButton->setObjectName(QStringLiteral("undoLayoutButton"));
    m_undoButton->setShortcut(QKeySequence(QKeySequence::Undo));
    m_undoButton->setToolTip(QStringLiteral("Undo last layout edit (Ctrl+Z)"));
    m_undoButton->setEnabled(false);
    m_editChannelNamesButton = new QPushButton(QStringLiteral("Channel names"), mapGroup);
    m_editChannelNamesButton->setObjectName(QStringLiteral("editChannelNamesButton"));
    m_editChannelNamesButton->setEnabled(false);
    auto *addGeneralButton = new QPushButton(QStringLiteral("Add General Slot"), mapGroup);
    auto *addEvButton = new QPushButton(QStringLiteral("Add EV Slot"), mapGroup);
    addGeneralButton->setEnabled(false);
    addEvButton->setEnabled(false);
    auto *saveButton = new QPushButton(QStringLiteral("Save layout"), mapGroup);
    auto *reloadButton = new QPushButton(QStringLiteral("Reload"), mapGroup);
    auto *resetButton = new QPushButton(QStringLiteral("Reset default"), mapGroup);
    toolbarLayout->addWidget(m_editToggleButton);
    toolbarLayout->addWidget(m_undoButton);
    toolbarLayout->addWidget(m_editChannelNamesButton);
    toolbarLayout->addWidget(addGeneralButton);
    toolbarLayout->addWidget(addEvButton);
    toolbarLayout->addWidget(saveButton);
    toolbarLayout->addWidget(reloadButton);
    toolbarLayout->addWidget(resetButton);
    toolbarLayout->addStretch();
    mapLayout->addWidget(adminToolbar);
    adminToolbar->setVisible(false);

    auto *statusLayout = new QHBoxLayout;
    auto *overviewButton = new QPushButton(QStringLiteral("← Overview"), mapGroup);
    overviewButton->setObjectName(QStringLiteral("parkingBackToOverviewButton"));
    overviewButton->setAccessibleName(QStringLiteral("Back to parking overview"));
    overviewButton->setToolTip(QStringLiteral("Back to parking overview"));
    overviewButton->setMinimumWidth(104);
    overviewButton->setCursor(Qt::PointingHandCursor);
    overviewButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#eef3f6; color:#263238; border:1px solid #b8c4cc; "
        "border-radius:5px; padding:6px 12px; font-weight:800; }"
        "QPushButton:hover { background:#dceaf3; border-color:#2d9cff; }"
        "QPushButton:pressed { background:#cbdce7; }"));
    statusLayout->addWidget(overviewButton);
    m_totalSummaryLabel = addCompactSummaryChip(
        mapGroup, statusLayout, QStringLiteral("TOTAL"),
        QStringLiteral("parkingSummaryTotal"), QStringLiteral("#607d8b"));
    m_vacantSummaryLabel = addCompactSummaryChip(
        mapGroup, statusLayout, QStringLiteral("VACANT"),
        QStringLiteral("parkingSummaryVacant"), QStringLiteral("#2ecc71"));
    m_occupiedSummaryLabel = addCompactSummaryChip(
        mapGroup, statusLayout, QStringLiteral("OCCUPIED"),
        QStringLiteral("parkingSummaryOccupied"), QStringLiteral("#2d9cff"));
    m_alertSummaryLabel = addCompactSummaryChip(
        mapGroup, statusLayout, QStringLiteral("ALERT"),
        QStringLiteral("parkingSummaryAlert"), QStringLiteral("#ff1744"));
    statusLayout->addStretch();
    auto *helpButton = new QPushButton(QStringLiteral("Parking Map 안내"), mapGroup);
    helpButton->setObjectName(QStringLiteral("parkingMapHelpButton"));
    helpButton->setAccessibleName(QStringLiteral("Parking Map 운영 및 편집 안내"));
    helpButton->setToolTip(QStringLiteral("슬롯 상태 판독과 배치 편집 방법 보기"));
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setIcon(pageHelpIcon());
    helpButton->setIconSize(QSize(22, 22));
    helpButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238; color:white; border:1px solid #455a64; "
        "border-radius:6px; padding:6px 11px; font-weight:800; }"
        "QPushButton:hover { background:#37474f; border-color:#fb8c00; }"
        "QPushButton:pressed { background:#1c252a; }"));
    statusLayout->addWidget(helpButton);
    mapLayout->addLayout(statusLayout);

    m_scene = new QGraphicsScene(0, 0, kParkingMapCanvasWidth,
                                 kParkingMapBaseHeight, this);
    m_scene->setBackgroundBrush(QColor(QStringLiteral("#1b1f23")));
    m_mapView = new QGraphicsView(m_scene, mapGroup);
    m_mapView->setObjectName(QStringLiteral("parkingMapDetailView"));
    m_mapView->setMinimumSize(680, 520);
    m_mapView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_mapView->setResizeAnchor(QGraphicsView::NoAnchor);
    m_mapView->setTransformationAnchor(QGraphicsView::NoAnchor);
    m_mapView->setRenderHint(QPainter::Antialiasing, true);
    m_mapView->setDragMode(QGraphicsView::NoDrag);
    m_mapView->setStyleSheet(QStringLiteral(
        "QGraphicsView { background: #1b1f23; border: 1px solid #3f4a53; border-radius: 6px; }"
        "QGraphicsView QScrollBar:horizontal {"
        "  background: #252c31; height: 12px; margin: 2px 4px; border: none;"
        "}"
        "QGraphicsView QScrollBar::handle:horizontal {"
        "  background: #ffffff; min-width: 56px; border-radius: 4px;"
        "}"
        "QGraphicsView QScrollBar::handle:horizontal:hover { background: #dff3ff; }"
        "QGraphicsView QScrollBar::add-line:horizontal,"
        "QGraphicsView QScrollBar::sub-line:horizontal { width: 0px; border: none; }"
        "QGraphicsView QScrollBar::add-page:horizontal,"
        "QGraphicsView QScrollBar::sub-page:horizontal { background: #3a444b; border-radius: 4px; }"));
    mapLayout->addWidget(m_mapView, 1);

    m_alarmPulseTimer = new QTimer(this);
    m_alarmPulseTimer->setInterval(90);
    connect(m_alarmPulseTimer, &QTimer::timeout, this, [this]() {
        m_alarmPulsePhase += 0.48;
        constexpr double fullCycle = 6.28318530717958647692;
        if (m_alarmPulsePhase >= fullCycle) m_alarmPulsePhase -= fullCycle;
        updateAlarmPulse();
    });
    m_runtimeClockTimer = new QTimer(this);
    m_runtimeClockTimer->setInterval(1000);
    connect(m_runtimeClockTimer, &QTimer::timeout,
            this, &ParkingMapPage::updateRuntimeStatusFromSelection);
    m_runtimeClockTimer->start();

    detailLayout->addWidget(mapGroup, 3);

    auto *rightPanel = new QWidget(detailPage);
    rightPanel->setFixedWidth(280);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto *mappingGroup = new QGroupBox(QStringLiteral("Parking Zone Search"), rightPanel);
    mappingGroup->setObjectName(QStringLiteral("parkingZoneSearchGroup"));
    auto *mappingLayout = new QVBoxLayout(mappingGroup);
    auto *filterLayout = new QHBoxLayout;
    m_zoneSearchEdit = new QLineEdit(mappingGroup);
    m_zoneSearchEdit->setObjectName(QStringLiteral("parkingZoneSearchEdit"));
    m_zoneSearchEdit->setPlaceholderText(
        QStringLiteral("Search zone, plate, name, channel, type"));
    m_stateFilterCombo = new QComboBox(mappingGroup);
    m_stateFilterCombo->setObjectName(QStringLiteral("parkingStateFilterCombo"));
    m_stateFilterCombo->addItem(QStringLiteral("All states"), QStringLiteral("ALL"));
    m_stateFilterCombo->addItem(QStringLiteral("Vacant"), QStringLiteral("VACANT"));
    m_stateFilterCombo->addItem(QStringLiteral("Occupied"), QStringLiteral("OCCUPIED"));
    m_stateFilterCombo->addItem(QStringLiteral("Waiting data"), QStringLiteral("WAITING"));
    m_stateFilterCombo->addItem(QStringLiteral("Active alerts"), QStringLiteral("ALERT"));
    m_filterResultLabel = new QLabel(QStringLiteral("0 zones"), mappingGroup);
    m_filterResultLabel->setObjectName(QStringLiteral("parkingFilterResultLabel"));
    m_filterResultLabel->setStyleSheet(QStringLiteral("color:#607d8b;font-size:10px;font-weight:700;"));
    filterLayout->addWidget(m_zoneSearchEdit, 1);
    filterLayout->addWidget(m_stateFilterCombo);
    mappingLayout->addLayout(filterLayout);
    mappingLayout->addWidget(m_filterResultLabel);
    m_zoneTable = new QTableWidget(0, 7, mappingGroup);
    m_zoneTable->setHorizontalHeaderLabels({
        QStringLiteral("Zone"),
        QStringLiteral("Type"),
        QStringLiteral("Channel"),
        QStringLiteral("IVA"),
        QStringLiteral("Hall Sensor"),
        QStringLiteral("State"),
        QStringLiteral("Enabled")
    });
    m_zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_zoneTable->verticalHeader()->setVisible(false);
    m_zoneTable->verticalHeader()->setDefaultSectionSize(24);
    m_zoneTable->setAlternatingRowColors(true);
    m_zoneTable->setShowGrid(false);
    m_zoneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_zoneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_zoneTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_zoneTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background: #f8fafb; alternate-background-color: #eef3f6; border: 1px solid #c7d0d8; }"
        "QTableWidget::item { padding: 3px 5px; }"
        "QTableWidget::item:selected { background: #dceaf5; color: #10212c; }"));
    mappingLayout->addWidget(m_zoneTable);
    auto *mappingActions = new QHBoxLayout;
    auto *ivaSettingsButton = new QPushButton(QStringLiteral("Open IVA Setup"), mappingGroup);
    ivaSettingsButton->setObjectName(QStringLiteral("parkingOpenIvaButton"));
    mappingActions->addWidget(ivaSettingsButton);
    mappingLayout->addLayout(mappingActions);
    rightLayout->addWidget(mappingGroup, 2);
    mappingGroup->setVisible(false);

    auto *runtimeGroup = new QGroupBox(rightPanel);
    runtimeGroup->setObjectName(QStringLiteral("runtimeStatusGroup"));
    auto *runtimeLayout = new QVBoxLayout(runtimeGroup);
    runtimeLayout->setContentsMargins(20, 16, 16, 14);
    runtimeLayout->setSpacing(8);
    auto *selectedHeader = new QWidget(runtimeGroup);
    selectedHeader->setObjectName(QStringLiteral("selectedSlotHeader"));
    auto *selectedHeaderLayout = new QVBoxLayout(selectedHeader);
    selectedHeaderLayout->setContentsMargins(4, 0, 0, 8);
    selectedHeaderLayout->setSpacing(4);
    auto *selectedTitleRow = new QHBoxLayout;
    selectedTitleRow->setContentsMargins(0, 0, 0, 0);
    m_selectedTitleLabel = new QLabel(QStringLiteral("No slot selected"), selectedHeader);
    m_selectedTitleLabel->setWordWrap(true);
    m_selectedTitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_selectedTitleLabel->setStyleSheet(QStringLiteral("color: #202124; font-size: 15px; font-weight: 900;"));
    m_selectedStateLabel = new QLabel(QStringLiteral("WAITING"), selectedHeader);
    m_selectedStateLabel->setObjectName(QStringLiteral("selectedSlotStateLabel"));
    m_selectedStateLabel->setAlignment(Qt::AlignCenter);
    m_selectedStateLabel->setMinimumHeight(24);
    m_selectedStateLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_selectedStateLabel->setStyleSheet(statePillStyle(false, SlotState::Vacant));
    selectedTitleRow->addWidget(m_selectedTitleLabel, 1);
    selectedTitleRow->addWidget(m_selectedStateLabel);
    selectedHeaderLayout->addLayout(selectedTitleRow);
    runtimeLayout->addWidget(selectedHeader);

    auto *runtimeGrid = new QGridLayout;
    runtimeGrid->setHorizontalSpacing(10);
    runtimeGrid->setVerticalSpacing(5);
    auto makeRuntimeValue = [runtimeGroup](const QString &objectName) {
        auto *label = new QLabel(QStringLiteral("-"), runtimeGroup);
        label->setObjectName(objectName);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setStyleSheet(QStringLiteral("color: #263238; font-weight: 800;"));
        return label;
    };
    m_runtimeDataStatusLabel = makeRuntimeValue(QStringLiteral("runtimeDataStatusLabel"));
    selectedHeaderLayout->addWidget(m_runtimeDataStatusLabel);
    m_runtimeVehicleLabel = makeRuntimeValue(QStringLiteral("runtimeVehicleLabel"));
    m_runtimeOccupiedSinceLabel = makeRuntimeValue(QStringLiteral("runtimeOccupiedSinceLabel"));
    m_runtimeOccupiedTimeLabel = makeRuntimeValue(QStringLiteral("runtimeOccupiedTimeLabel"));
    m_runtimeAlarmLabel = makeRuntimeValue(QStringLiteral("runtimeAlarmLabel"));
    m_runtimeVehicleImageLabel = new QLabel(QStringLiteral("Select a slot to view vehicle image"), runtimeGroup);
    m_runtimeVehicleImageLabel->setObjectName(QStringLiteral("runtimeVehicleImageLabel"));
    m_runtimeVehicleImageLabel->setAlignment(Qt::AlignCenter);
    m_runtimeVehicleImageLabel->setMinimumHeight(118);
    m_runtimeVehicleImageLabel->setStyleSheet(QStringLiteral(
        "QLabel { background:#111820;color:#b0bec5;border:1px solid #455a64;border-radius:5px;padding:6px; }"));
    runtimeLayout->addWidget(m_runtimeVehicleImageLabel);
    auto addRuntimeField = [runtimeGrid, runtimeGroup](int row, int column,
                                                       const QString &title, QLabel *value) {
        auto *titleLabel = new QLabel(title, runtimeGroup);
        titleLabel->setStyleSheet(QStringLiteral("color: #607d8b; font-size: 11px;"));
        runtimeGrid->addWidget(titleLabel, row, column * 2);
        runtimeGrid->addWidget(value, row, (column * 2) + 1);
    };
    addRuntimeField(0, 0, QStringLiteral("Vehicle"), m_runtimeVehicleLabel);
    addRuntimeField(1, 0, QStringLiteral("Occupied since"), m_runtimeOccupiedSinceLabel);
    addRuntimeField(2, 0, QStringLiteral("Occupied duration"), m_runtimeOccupiedTimeLabel);
    addRuntimeField(3, 0, QStringLiteral("Warning"), m_runtimeAlarmLabel);
    runtimeGrid->setColumnStretch(1, 1);
    runtimeGrid->setColumnStretch(3, 1);
    runtimeLayout->addLayout(runtimeGrid);
    auto *runtimeActions = new QHBoxLayout;
    m_eventsButton = new QPushButton(QStringLiteral("Open Events"), runtimeGroup);
    m_cameraButton = new QPushButton(QStringLiteral("View Camera"), runtimeGroup);
    m_eventsButton->setObjectName(QStringLiteral("parkingOpenEventsButton"));
    m_cameraButton->setObjectName(QStringLiteral("parkingOpenCameraButton"));
    runtimeActions->addWidget(m_eventsButton);
    runtimeActions->addWidget(m_cameraButton);
    runtimeLayout->addLayout(runtimeActions);
    rightLayout->insertWidget(0, runtimeGroup);

    auto *recentGroup = new QGroupBox(QStringLiteral("Recent Events"), rightPanel);
    recentGroup->setObjectName(QStringLiteral("parkingRecentEventsGroup"));
    // Keep the recent-event table compact. Without a fixed vertical policy the
    // right-side layout gives this group half of the available height and the
    // capped table is vertically centred inside a large empty card.
    recentGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    recentGroup->setMaximumHeight(184);
    auto *recentLayout = new QVBoxLayout(recentGroup);
    m_recentEventsTable = new QTableWidget(0, 3, recentGroup);
    m_recentEventsTable->setObjectName(QStringLiteral("parkingRecentEventsTable"));
    m_recentEventsTable->setHorizontalHeaderLabels({
        QStringLiteral("Time"), QStringLiteral("Event"), QStringLiteral("Status")
    });
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_recentEventsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_recentEventsTable->verticalHeader()->setVisible(false);
    m_recentEventsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recentEventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recentEventsTable->setFixedHeight(142);
    recentLayout->addWidget(m_recentEventsTable);
    rightLayout->insertWidget(1, recentGroup);

    auto *editorGroup = new QGroupBox(QStringLiteral("Layout Editor"), rightPanel);
    editorGroup->setObjectName(QStringLiteral("parkingMapLayoutEditor"));
    auto *editorLayout = new QFormLayout(editorGroup);
    m_zoneIdEdit = new QLineEdit(editorGroup);
    m_displayNameEdit = new QLineEdit(editorGroup);
    m_zoneTypeCombo = new QComboBox(editorGroup);
    m_zoneTypeCombo->addItems({QStringLiteral("GENERAL"), QStringLiteral("EV")});
    m_cameraChannelCombo = new QComboBox(editorGroup);
    m_cameraChannelCombo->setObjectName(QStringLiteral("parkingCameraChannelCombo"));
    m_cameraChannelCombo->addItems({QStringLiteral("CH1"), QStringLiteral("CH3")});
    m_cameraChannelCombo->setToolTip(QStringLiteral("Parking slots are controlled only by CH1 and CH3."));
    m_ivaAreaCombo = new QComboBox(editorGroup);
    m_ivaAreaCombo->setObjectName(QStringLiteral("parkingIvaAreaCombo"));
    m_ivaAreaCombo->addItems({
        QStringLiteral("N/A"),
        QStringLiteral("IVA1"),
        QStringLiteral("IVA2"),
        QStringLiteral("IVA3"),
        QStringLiteral("IVA4")
    });
    m_hallSensorEdit = new QLineEdit(editorGroup);
    m_hallSensorEdit->setObjectName(QStringLiteral("parkingHallSensorEdit"));
    m_enabledCheck = new QCheckBox(QStringLiteral("Enabled"), editorGroup);
    auto makeSpin = [editorGroup](double max, double step) {
        auto *spin = new QDoubleSpinBox(editorGroup);
        spin->setRange(-2000.0, max);
        spin->setSingleStep(step);
        spin->setDecimals(1);
        return spin;
    };
    m_xSpin = makeSpin(3000.0, 5.0);
    m_ySpin = makeSpin(3000.0, 5.0);
    auto makeSliderControl = [editorGroup](QSlider **slider, QLabel **valueLabel,
                                           QSpinBox **valueSpin,
                                           int minimum, int maximum, int step) {
        auto *container = new QWidget(editorGroup);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *createdSlider = new QSlider(Qt::Horizontal, container);
        createdSlider->setRange(minimum, maximum);
        createdSlider->setSingleStep(step);
        createdSlider->setPageStep(step * 4);
        createdSlider->setTickPosition(QSlider::TicksBelow);
        createdSlider->setTickInterval(step * 4);
        layout->addWidget(createdSlider, 1);
        if (valueSpin) {
            auto *createdSpin = new QSpinBox(container);
            createdSpin->setRange(minimum, maximum);
            createdSpin->setSingleStep(step);
            createdSpin->setSuffix(QStringLiteral(" px"));
            createdSpin->setMinimumWidth(92);
            layout->addWidget(createdSpin);
            *valueSpin = createdSpin;
        } else {
            auto *createdLabel = new QLabel(container);
            createdLabel->setMinimumWidth(56);
            createdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            createdLabel->setStyleSheet(QStringLiteral("color: #455a64; font-weight: 700;"));
            layout->addWidget(createdLabel);
            *valueLabel = createdLabel;
        }
        *slider = createdSlider;
        return container;
    };
    QWidget *widthControl = makeSliderControl(&m_widthSlider, &m_widthValueLabel,
                                              &m_widthSpin, 16, 420, 1);
    QWidget *heightControl = makeSliderControl(&m_heightSlider, &m_heightValueLabel,
                                               &m_heightSpin, 16, 220, 1);
    QWidget *rotationControl = makeSliderControl(&m_rotationSlider, &m_rotationValueLabel,
                                                 nullptr, -180, 180, 5);
    m_widthSpin->setObjectName(QStringLiteral("parkingZoneWidthSpin"));
    m_heightSpin->setObjectName(QStringLiteral("parkingZoneHeightSpin"));
    m_deleteButton = new QPushButton(QStringLiteral("Delete zone"), editorGroup);
    editorLayout->addRow(QStringLiteral("Zone ID"), m_zoneIdEdit);
    editorLayout->addRow(QStringLiteral("Display"), m_displayNameEdit);
    editorLayout->addRow(QStringLiteral("Type"), m_zoneTypeCombo);
    editorLayout->addRow(QStringLiteral("Channel"), m_cameraChannelCombo);
    editorLayout->addRow(QStringLiteral("IVA"), m_ivaAreaCombo);
    editorLayout->addRow(QStringLiteral("Hall Sensor"), m_hallSensorEdit);
    editorLayout->addRow(QStringLiteral("X"), m_xSpin);
    editorLayout->addRow(QStringLiteral("Y"), m_ySpin);
    editorLayout->addRow(QStringLiteral("Width"), widthControl);
    editorLayout->addRow(QStringLiteral("Height"), heightControl);
    editorLayout->addRow(QStringLiteral("Rotation"), rotationControl);
    editorLayout->addRow(QString(), m_enabledCheck);
    editorLayout->addRow(QString(), m_deleteButton);
    rightLayout->insertWidget(1, editorGroup, 1);
    editorGroup->setVisible(false);
    detailLayout->addWidget(rightPanel, 2);

    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &ParkingMapPage::handleSceneSelectionChanged);
    connect(m_zoneTable, &QTableWidget::cellClicked,
            this, &ParkingMapPage::handleZoneTableClicked);
    connect(m_zoneSearchEdit, &QLineEdit::textChanged,
            this, &ParkingMapPage::applyZoneFilters);
    connect(m_overviewSearchEdit, &QLineEdit::textChanged,
            this, &ParkingMapPage::updateOverviewSearchResults);
    connect(m_overviewSearchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (!m_overviewSearchResults || m_overviewSearchResults->count() != 1) return;
        openOverviewSearchResult(m_overviewSearchResults->item(0));
    });
    connect(m_overviewSearchResults, &QListWidget::itemClicked,
            this, &ParkingMapPage::openOverviewSearchResult);
    connect(m_stateFilterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ParkingMapPage::applyZoneFilters);
    connect(ivaSettingsButton, &QPushButton::clicked, this, [this]() {
        const ParkingZoneLayout *zone = selectedZone();
        if (!zone) return;
        emit ivaSettingsRequested(zone->zoneId,
                                  zone->cameraChannel,
                                  zone->ivaAreaId);
    });
    connect(m_editToggleButton, &QPushButton::toggled,
            this, &ParkingMapPage::setEditMode);
    connect(m_editToggleButton, &QPushButton::toggled, this,
            [addGeneralButton, addEvButton](bool enabled) {
                addGeneralButton->setEnabled(enabled);
                addEvButton->setEnabled(enabled);
            });
    connect(m_undoButton, &QPushButton::clicked,
            this, &ParkingMapPage::undoLastLayoutChange);
    connect(m_editChannelNamesButton, &QPushButton::clicked,
            this, &ParkingMapPage::editChannelDisplayNames);
    connect(addGeneralButton, &QPushButton::clicked,
            this, &ParkingMapPage::addGeneralZone);
    connect(addEvButton, &QPushButton::clicked,
            this, &ParkingMapPage::addEvZone);
    connect(saveButton, &QPushButton::clicked,
            this, &ParkingMapPage::saveLayout);
    connect(reloadButton, &QPushButton::clicked,
            this, &ParkingMapPage::reloadLayout);
    connect(resetButton, &QPushButton::clicked,
            this, &ParkingMapPage::resetDefaultLayout);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &ParkingMapPage::deleteSelectedZone);
    connect(helpButton, &QPushButton::clicked,
            this, &ParkingMapPage::showHelpDialog);
    connect(m_eventsButton, &QPushButton::clicked, this, [this]() {
        const ParkingZoneLayout *zone = selectedZone();
        if (!zone) return;
        QString eventId;
        if (m_lastState.evSlots.contains(zone->zoneId)) eventId = m_lastState.evSlots.value(zone->zoneId).eventId;
        else if (m_lastState.parkingSlots.contains(zone->zoneId)) eventId = m_lastState.parkingSlots.value(zone->zoneId).eventId;
        emit eventsRequested(zone->zoneId, eventId);
    });
    connect(m_cameraButton, &QPushButton::clicked, this, [this]() {
        const ParkingZoneLayout *zone = selectedZone();
        if (zone) emit cameraRequested(zone->cameraChannel);
    });
    connect(overviewButton, &QPushButton::clicked,
            this, &ParkingMapPage::showOverview);

    const QList<QLineEdit *> lineEdits = {m_zoneIdEdit, m_displayNameEdit, m_hallSensorEdit};
    for (QLineEdit *edit : lineEdits) {
        connect(edit, &QLineEdit::editingFinished, this, &ParkingMapPage::applyEditorFields);
    }
    connect(m_zoneTypeCombo, &QComboBox::currentTextChanged, this, &ParkingMapPage::applyEditorFields);
    connect(m_cameraChannelCombo, &QComboBox::currentTextChanged, this, &ParkingMapPage::applyEditorFields);
    connect(m_ivaAreaCombo, &QComboBox::currentTextChanged, this, &ParkingMapPage::applyEditorFields);
    connect(m_enabledCheck, &QCheckBox::toggled, this, &ParkingMapPage::applyEditorFields);
    for (QDoubleSpinBox *spin : {m_xSpin, m_ySpin}) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &ParkingMapPage::applyEditorFields);
    }
    for (QSlider *slider : {m_widthSlider, m_heightSlider, m_rotationSlider}) {
        connect(slider, &QSlider::sliderPressed,
                this, &ParkingMapPage::beginEditorSliderGesture);
        connect(slider, &QSlider::sliderReleased,
                this, &ParkingMapPage::endEditorSliderGesture);
        connect(slider, &QSlider::valueChanged, this, [this]() {
            updateGeometrySliderLabels();
            applyEditorFields();
        });
    }
    connect(m_widthSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        if (m_updatingEditor) return;
        QSignalBlocker blocker(m_widthSlider);
        m_widthSlider->setValue(value);
        applyEditorFields();
    });
    connect(m_heightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int value) {
        if (m_updatingEditor) return;
        QSignalBlocker blocker(m_heightSlider);
        m_heightSlider->setValue(value);
        applyEditorFields();
    });

    loadLayout();
    rebuildScene();
    rebuildOverviewScene();
    updateZoneTable();
    updateEditorFromSelection();
    updateOverviewSummary();
    showOverview();
    scrollMapToOrigin();
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
}

void ParkingMapPage::showHelpDialog()
{
    if (QDialog *existing = findChild<QDialog *>(
            QStringLiteral("parkingMapHelpDialog"))) {
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("parkingMapHelpDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Parking Map 운영 가이드"));
    dialog->setModal(true);
    dialog->setMinimumSize(780, 580);
    dialog->resize(940, 760);

    auto *dialogLayout = new QVBoxLayout(dialog);
    dialogLayout->setContentsMargins(14, 14, 14, 14);
    dialogLayout->setSpacing(10);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setObjectName(QStringLiteral("parkingMapHelpScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("Parking Map 운영 가이드"), content);
    title->setObjectName(QStringLiteral("parkingMapHelpTitle"));
    title->setStyleSheet(QStringLiteral(
        "font-size:22px;font-weight:900;color:#1f2d35;"));
    layout->addWidget(title);

    auto *intro = new QLabel(
        QStringLiteral("Parking Map은 주차면이 연결된 채널과 부지 입·출구를 표시합니다. "
                       "Camera 1은 CH1/CH3의 주차면을 표시하며 슬롯 종류와 무관하게 "
                       "채널 내부 IVA Area 매핑을 사용합니다. "
                       "CH2 입구 표식을 표시합니다. Camera 12에서 EXIT로 설정된 채널은 "
                       "각 채널의 동서남북 방향에 맞춰 출구 표식을 표시합니다. "
                       "입·출구는 주차면 집계에 포함되지 않습니다. "
                       "Camera 2~12의 UI Preview 채널 역할과 주차면 수는 Overview의 "
                       "Edit overview layout에서 설정하고 로컬 배치 파일에 저장합니다. "
                       "입·출구를 제외한 통로 관제 채널은 이 화면에서 제외됩니다. "
                       "이 화면은 고정 도면에서 실시간 상태·차량 이미지·경고를 확인하는 관제 화면입니다. "
                       "주차면 배치와 IVA 매핑 변경은 전용 설정 탭에서 관리합니다."),
        content);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral(
        "background:#e3f2fd;color:#0d47a1;border:1px solid #90caf9;"
        "border-radius:7px;padding:10px;font-weight:700;"));
    layout->addWidget(intro);

    auto *monitorGroup = new QGroupBox(
        QStringLiteral("1. 관제 · 지도와 선택 슬롯 읽기"), content);
    auto *monitorLayout = new QVBoxLayout(monitorGroup);
    auto *monitorHint = new QLabel(
        QStringLiteral("지도에서 슬롯을 선택하면 차량 전체 이미지, 차량 종류, "
                       "점유 시작 시각·지속시간, 경고 종류와 ACK 상태가 표시됩니다. 번호판은 표시하지 않습니다."),
        monitorGroup);
    monitorHint->setWordWrap(true);
    monitorLayout->addWidget(monitorHint);

    auto *miniMap = new QWidget(monitorGroup);
    miniMap->setObjectName(QStringLiteral("parkingMapHelpMiniMap"));
    auto *miniMapLayout = new QGridLayout(miniMap);
    miniMapLayout->setContentsMargins(0, 4, 0, 0);
    miniMapLayout->setSpacing(8);
    for (int channel = 0; channel < 4; ++channel) {
        const int channelNumber = channel + 1;
        const bool parkingControl = channelNumber == 1 || channelNumber == 3;
        if (!parkingControl) continue;
        auto *channelFrame = new QFrame(miniMap);
        channelFrame->setMinimumSize(360, 90);
        channelFrame->setStyleSheet(QStringLiteral(
            "QFrame { background:#1b1f23;border:1px solid #455a64;border-radius:6px; }"));
        auto *channelLayout = new QVBoxLayout(channelFrame);
        channelLayout->setContentsMargins(10, 8, 10, 8);
        channelLayout->setSpacing(5);
        auto *channelTitle = new QLabel(
            QStringLiteral("CH%1").arg(channelNumber), channelFrame);
        channelTitle->setStyleSheet(QStringLiteral(
            "border:none;color:white;font-weight:800;"));
        channelLayout->addWidget(channelTitle);
        auto makeSlotSample = [channelFrame](const QString &name,
                                             const QString &meta,
                                             const QString &border) {
            auto *slot = new QLabel(QStringLiteral("%1    %2").arg(name, meta), channelFrame);
            slot->setStyleSheet(QStringLiteral(
                "background:#2a3035;color:#eceff1;border:2px solid %1;"
                "border-radius:4px;padding:5px;font-size:11px;").arg(border));
            return slot;
        };
        const bool evChannel = channelNumber == 1;
        channelLayout->addWidget(makeSlotSample(
            evChannel ? QStringLiteral("EV-01 ~ EV-04")
                      : QStringLiteral("P-01 ~ P-04"),
            QStringLiteral("IVA mapped"),
            evChannel ? QStringLiteral("#2d9cff") : QStringLiteral("#ffd447")));
        const int gridRow = channelNumber == 1 ? 0 : (channelNumber == 3 ? 2 : 1);
        const int gridColumn = channelNumber == 2 ? 2 : (channelNumber == 4 ? 0 : 1);
        miniMapLayout->addWidget(channelFrame, gridRow, gridColumn);
    }
    monitorLayout->addWidget(miniMap);
    layout->addWidget(monitorGroup);

    auto *legendGroup = new QGroupBox(
        QStringLiteral("2. 색과 경고 표식 · 무엇을 뜻하는가"), content);
    auto *legendLayout = new QVBoxLayout(legendGroup);
    auto *legendHint = new QLabel(
        QStringLiteral("슬롯 안쪽 색은 현재 차량 상태, 테두리는 구역 종류를 뜻합니다. "
                       "경고는 차량 색을 덮지 않고 위반 빨강, 장기주차 주황, 센서 오류 보라로 표시됩니다."),
        legendGroup);
    legendHint->setWordWrap(true);
    legendLayout->addWidget(legendHint);
    auto *stateLegend = new QWidget(legendGroup);
    stateLegend->setObjectName(QStringLiteral("parkingMapHelpStateLegend"));
    auto *stateLayout = new QGridLayout(stateLegend);
    stateLayout->setContentsMargins(0, 2, 0, 0);
    stateLayout->setSpacing(7);
    struct HelpState {
        QString name;
        QString meaning;
        QString fill;
        QString border;
    };
    const QList<HelpState> states{
        {QStringLiteral("VACANT"), QStringLiteral("빈 슬롯"),
         QStringLiteral("#1f5a3a"), QStringLiteral("#38d978")},
        {QStringLiteral("EV CAR"), QStringLiteral("전기차 점유"),
         QStringLiteral("#155d7a"), QStringLiteral("#38bdf8")},
        {QStringLiteral("GENERAL CAR"), QStringLiteral("일반차 점유"),
         QStringLiteral("#46535f"), QStringLiteral("#aebbc5")},
        {QStringLiteral("EV ZONE"), QStringLiteral("파란 테두리"),
         QStringLiteral("#242a2f"), QStringLiteral("#2d9cff")},
        {QStringLiteral("GENERAL ZONE"), QStringLiteral("노란 테두리"),
         QStringLiteral("#242a2f"), QStringLiteral("#ffd447")},
        {QStringLiteral("NON-EV"), QStringLiteral("위반 · 빨간 점멸"),
         QStringLiteral("#5b1824"), QStringLiteral("#ff1744")},
        {QStringLiteral("OVERSTAY"), QStringLiteral("장기주차 · 주황 점멸"),
         QStringLiteral("#5d3510"), QStringLiteral("#fb8c00")},
        {QStringLiteral("SENSOR"), QStringLiteral("센서 오류 · 보라 점멸"),
         QStringLiteral("#38245b"), QStringLiteral("#b388ff")},
        {QStringLiteral("WAITING DATA"), QStringLiteral("runtime 상태 미수신"),
         QStringLiteral("#eceff1"), QStringLiteral("#90a4ae")}
    };
    for (int index = 0; index < states.size(); ++index) {
        const HelpState &state = states.at(index);
        auto *card = new QFrame(stateLegend);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:white;border:1px solid #cfd8dc;border-radius:6px; }"));
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(8, 7, 8, 7);
        auto *swatch = new QLabel(card);
        swatch->setFixedSize(28, 22);
        swatch->setStyleSheet(QStringLiteral(
            "background:%1;border:2px solid %2;border-radius:4px;")
                                  .arg(state.fill, state.border));
        auto *textLayout = new QVBoxLayout;
        textLayout->setSpacing(0);
        auto *name = new QLabel(state.name, card);
        name->setStyleSheet(QStringLiteral(
            "border:none;color:#263238;font-weight:800;font-size:11px;"));
        auto *meaning = new QLabel(state.meaning, card);
        meaning->setStyleSheet(QStringLiteral(
            "border:none;color:#607d8b;font-size:10px;"));
        textLayout->addWidget(name);
        textLayout->addWidget(meaning);
        cardLayout->addWidget(swatch);
        cardLayout->addLayout(textLayout, 1);
        stateLayout->addWidget(card, index / 4, index % 4);
    }
    legendLayout->addWidget(stateLegend);
    layout->addWidget(legendGroup);

    auto *safetyNotes = new QLabel(
        QStringLiteral(
            "관제 동작\n"
            "• 빨간색·주황색·보라색 경고가 있는 슬롯을 선택하고 Open Events로 전체 이력과 처리 화면으로 이동합니다.\n"
            "• View Camera는 선택 슬롯과 연결된 기존 채널 스트림을 엽니다.\n"
            "• 주차면이 없는 채널은 Parking Map에 표시하지 않으며 주차면 점유 수에도 포함하지 않습니다.\n"
            "• 이 화면의 EV/P Zone ID와 서버의 slot_id는 자동으로 같은 ID라고 가정하지 않습니다."),
        content);
    safetyNotes->setObjectName(QStringLiteral("parkingMapHelpSafetyNotes"));
    safetyNotes->setWordWrap(true);
    safetyNotes->setStyleSheet(QStringLiteral(
        "background:#fff3e0;color:#5d4037;border:1px solid #ffcc80;"
        "border-radius:7px;padding:11px;"));
    layout->addWidget(safetyNotes);
    layout->addStretch();

    scrollArea->setWidget(content);
    dialogLayout->addWidget(scrollArea, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->setObjectName(QStringLiteral("parkingMapHelpButtons"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialogLayout->addWidget(buttons);
    dialog->open();
}

ParkingMapPage::~ParkingMapPage()
{
    m_rebuildingScene = true;
    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
    }
}

void ParkingMapPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    scrollMapToOrigin();
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
    QTimer::singleShot(50, this, &ParkingMapPage::scrollMapToOrigin);
}

void ParkingMapPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionOverviewSearchPopup();
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
}

void ParkingMapPage::render(const ParkingViewState &state)
{
    m_lastState = state;
    updateAllZoneVisuals();
    updateAlarmAnimationState();
    updateZoneTable();
    updateOperationalSummary();
    applyZoneFilters();
    updateEditorFromSelection();
}

void ParkingMapPage::appendEvent(const MonitoringEvent &event)
{
    if (event.sourceId.trimmed().isEmpty()) return;
    for (int index = 0; index < m_recentEvents.size(); ++index) {
        if (!event.id.isEmpty() && m_recentEvents.at(index).id == event.id) {
            m_recentEvents.removeAt(index);
            break;
        }
    }
    m_recentEvents.prepend(event);
    while (m_recentEvents.size() > 100) m_recentEvents.removeLast();
    updateOperationalSummary();
    updateRecentEvents();
}

void ParkingMapPage::setImageLoader(ImageLoader *imageLoader)
{
    if (m_imageLoader == imageLoader) return;
    if (m_imageLoader) disconnect(m_imageLoader, nullptr, this, nullptr);
    m_imageLoader = imageLoader;
    if (!m_imageLoader) {
        updateVehicleImage();
        return;
    }
    connect(m_imageLoader, &ImageLoader::imageLoaded, this,
            [this](const QString &requestId, const QPixmap &pixmap) {
                if (requestId != m_vehicleImageRequestId || !m_runtimeVehicleImageLabel) return;
                m_runtimeVehicleImageLabel->setText(QString());
                m_runtimeVehicleImageLabel->setPixmap(pixmap.scaled(
                    m_runtimeVehicleImageLabel->size() - QSize(12, 12),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            });
    connect(m_imageLoader, &ImageLoader::imageFailed, this,
            [this](const QString &requestId, const QString &) {
                if (requestId != m_vehicleImageRequestId || !m_runtimeVehicleImageLabel) return;
                m_runtimeVehicleImageLabel->setPixmap(QPixmap());
                m_runtimeVehicleImageLabel->setText(QStringLiteral("Vehicle image unavailable"));
            });
    updateVehicleImage();
}

ParkingMapPage::LayoutSnapshot ParkingMapPage::captureLayoutSnapshot() const
{
    LayoutSnapshot snapshot;
    snapshot.zones = m_zones;
    snapshot.channelDisplayNames = m_channelDisplayNames;
    snapshot.overviewLayouts = m_overviewLayouts;
    snapshot.selectedZoneId = selectedZoneId();
    return snapshot;
}

void ParkingMapPage::pushUndoSnapshot(const LayoutSnapshot &snapshot)
{
    if (m_undoHistory.size() >= kUndoHistoryLimit) {
        m_undoHistory.removeFirst();
    }
    m_undoHistory.append(snapshot);
    updateUndoButtonState();
}

void ParkingMapPage::pushCurrentLayoutToUndoHistory()
{
    pushUndoSnapshot(captureLayoutSnapshot());
}

void ParkingMapPage::clearUndoHistory()
{
    m_undoHistory.clear();
    m_hasPendingDragSnapshot = false;
    m_editorSliderGestureActive = false;
    m_editorSliderSnapshotRecorded = false;
    m_cleanLayoutSnapshot = captureLayoutSnapshot();
    updateUndoButtonState();
}

void ParkingMapPage::restoreLayoutSnapshot(const LayoutSnapshot &snapshot)
{
    m_zones = snapshot.zones;
    m_channelDisplayNames = snapshot.channelDisplayNames;
    m_overviewLayouts = snapshot.overviewLayouts;
    m_hasPendingDragSnapshot = false;
    m_editorSliderGestureActive = false;
    m_editorSliderSnapshotRecorded = false;

    rebuildScene();
    rebuildOverviewScene();
    updateZoneTable();
    if (!snapshot.selectedZoneId.isEmpty()
        && zoneIndexById(snapshot.selectedZoneId) >= 0) {
        selectZoneById(snapshot.selectedZoneId);
    } else {
        updateEditorFromSelection();
    }
}

bool ParkingMapPage::layoutMatchesCleanSnapshot() const
{
    if (m_channelDisplayNames != m_cleanLayoutSnapshot.channelDisplayNames
        || !sameOverviewLayouts(m_overviewLayouts, m_cleanLayoutSnapshot.overviewLayouts)
        || m_zones.size() != m_cleanLayoutSnapshot.zones.size()) {
        return false;
    }
    for (int index = 0; index < m_zones.size(); ++index) {
        if (!sameZoneLayout(m_zones.at(index), m_cleanLayoutSnapshot.zones.at(index))) {
            return false;
        }
    }
    return true;
}

void ParkingMapPage::updateUndoButtonState()
{
    if (m_undoButton) {
        m_undoButton->setEnabled(!m_undoHistory.isEmpty());
    }
}

void ParkingMapPage::beginEditorSliderGesture()
{
    if (m_updatingEditor || !m_editMode) return;
    m_editorSliderSnapshot = captureLayoutSnapshot();
    m_editorSliderGestureActive = true;
    m_editorSliderSnapshotRecorded = false;
}

void ParkingMapPage::endEditorSliderGesture()
{
    m_editorSliderGestureActive = false;
    m_editorSliderSnapshotRecorded = false;
}

void ParkingMapPage::undoLastLayoutChange()
{
    if (m_undoHistory.isEmpty()) return;

    const LayoutSnapshot snapshot = m_undoHistory.takeLast();
    restoreLayoutSnapshot(snapshot);
    updateUndoButtonState();
    const bool matchesCleanSnapshot = layoutMatchesCleanSnapshot();
    setLayoutDirty(!matchesCleanSnapshot,
                   matchesCleanSnapshot
                       ? QStringLiteral("Restored saved layout")
                       : QStringLiteral("Undo applied"));
}

void ParkingMapPage::addGeneralZone()
{
    syncZonesFromItems();
    pushCurrentLayoutToUndoHistory();
    ParkingZoneLayout zone;
    zone.zoneId = nextZoneId(QStringLiteral("P"));
    zone.zoneType = QStringLiteral("GENERAL");
    zone.displayName = zone.zoneId;
    zone.cameraChannel = m_cameraChannelCombo ? m_cameraChannelCombo->currentText() : QStringLiteral("CH3");
    if (!isParkingControlChannel(zone.cameraChannel)) zone.cameraChannel = QStringLiteral("CH3");
    zone.slotOrder = 5;
    zone.rect = nextZoneRectForChannel(zone.cameraChannel);
    QString compactId = zone.zoneId;
    compactId.replace(QLatin1Char('-'), QLatin1Char('_'));
    zone.ivaAreaId.clear();
    zone.hallSensorId = QStringLiteral("HALL_") + compactId;
    applyZoneTypeRules(&zone);
    m_zones.append(zone);
    if (!m_editMode) m_editToggleButton->setChecked(true);
    rebuildScene();
    if (QGraphicsRectItem *item = m_zoneItems.value(zone.zoneId)) {
        item->setSelected(true);
        m_mapView->ensureVisible(item, 24, 24);
    }
    markLayoutDirty(QStringLiteral("%1 added").arg(zone.zoneId));
    updateZoneTable();
}

void ParkingMapPage::addEvZone()
{
    syncZonesFromItems();
    pushCurrentLayoutToUndoHistory();
    ParkingZoneLayout zone;
    zone.zoneId = nextZoneId(QStringLiteral("EV"));
    zone.zoneType = QStringLiteral("EV");
    zone.displayName = zone.zoneId;
    zone.cameraChannel = m_cameraChannelCombo ? m_cameraChannelCombo->currentText() : QStringLiteral("CH1");
    if (!isParkingControlChannel(zone.cameraChannel)) zone.cameraChannel = QStringLiteral("CH1");
    zone.slotOrder = 5;
    zone.rect = nextZoneRectForChannel(zone.cameraChannel);
    QString compactId = zone.zoneId;
    compactId.replace(QLatin1Char('-'), QLatin1Char('_'));
    const QString preferredIva = m_ivaAreaCombo ? m_ivaAreaCombo->currentText() : QString();
    zone.ivaAreaId = preferredIva;
    zone.hallSensorId = QStringLiteral("HALL_") + compactId;
    applyZoneTypeRules(&zone, preferredIva);
    m_zones.append(zone);
    if (!m_editMode) m_editToggleButton->setChecked(true);
    rebuildScene();
    if (QGraphicsRectItem *item = m_zoneItems.value(zone.zoneId)) {
        item->setSelected(true);
        m_mapView->ensureVisible(item, 24, 24);
    }
    markLayoutDirty(QStringLiteral("%1 added").arg(zone.zoneId));
    updateZoneTable();
}

void ParkingMapPage::deleteSelectedZone()
{
    syncZonesFromItems();
    const QString zoneId = selectedZoneId();
    const int index = zoneIndexById(zoneId);
    if (index < 0) return;
    pushCurrentLayoutToUndoHistory();
    m_zones.removeAt(index);
    rebuildScene();
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty(QStringLiteral("%1 deleted").arg(zoneId));
}

void ParkingMapPage::saveLayout()
{
    QString error;
    if (!saveLayoutNow(&error)) {
        QMessageBox::warning(this, QStringLiteral("Parking map layout"), error);
    }
}

bool ParkingMapPage::saveLayoutNow(QString *errorMessage)
{
    syncZonesFromItems();
    QString error;
    if (!validateLayout(&error)) {
        if (errorMessage) *errorMessage = error;
        if (m_layoutStatusLabel) {
            m_layoutStatusLabel->setText(QStringLiteral("Save blocked | %1").arg(error));
        }
        emit layoutSaveResult(false, error);
        return false;
    }
    if (!saveParkingZoneLayout(m_layoutPath, m_zones, m_channelDisplayNames,
                               m_overviewLayouts, &error)) {
        if (errorMessage) *errorMessage = error;
        if (m_layoutStatusLabel) {
            m_layoutStatusLabel->setText(
                QStringLiteral("Save failed | %1").arg(QDir::toNativeSeparators(m_layoutPath)));
        }
        emit layoutSaveResult(false, error);
        return false;
    }

    if (errorMessage) errorMessage->clear();
    clearUndoHistory();
    setLayoutDirty(false, QStringLiteral("Saved local layout"));
    emit layoutSaveResult(
        true,
        QStringLiteral("Parking map layout saved: %1")
            .arg(QDir::toNativeSeparators(m_layoutPath)));
    return true;
}

void ParkingMapPage::reloadLayout()
{
    loadLayout();
    rebuildScene();
    rebuildOverviewScene();
    updateZoneTable();
    updateEditorFromSelection();
    scrollMapToOrigin();
}

void ParkingMapPage::resetDefaultLayout()
{
    syncZonesFromItems();
    const QList<ParkingZoneLayout> defaultZones = defaultParkingZoneLayout();
    const ParkingOverviewLayouts defaultOverviewLayouts = defaultParkingOverviewLayouts();
    if (m_channelDisplayNames.isEmpty()
        && sameOverviewLayouts(m_overviewLayouts, defaultOverviewLayouts)
        && m_zones.size() == defaultZones.size()) {
        bool alreadyDefault = true;
        for (int index = 0; index < m_zones.size(); ++index) {
            if (!sameZoneLayout(m_zones.at(index), defaultZones.at(index))) {
                alreadyDefault = false;
                break;
            }
        }
        if (alreadyDefault) return;
    }
    pushCurrentLayoutToUndoHistory();
    m_zones = defaultZones;
    m_channelDisplayNames.clear();
    m_overviewLayouts = defaultOverviewLayouts;
    rebuildScene();
    rebuildOverviewScene();
    updateZoneTable();
    updateEditorFromSelection();
    scrollMapToOrigin();
    markLayoutDirty(QStringLiteral("Default layout restored"));
}

void ParkingMapPage::handleSceneSelectionChanged()
{
    if (m_rebuildingScene) return;
    updateEditorFromSelection();
    updateAllZoneVisuals();
    const QString zoneId = selectedZoneId();
    if (zoneId.isEmpty()) return;
    const int row = zoneIndexById(zoneId);
    if (row >= 0) {
        QSignalBlocker blocker(m_zoneTable);
        m_zoneTable->selectRow(row);
    }
    emit slotDetailRequested(zoneId);
}

void ParkingMapPage::handleZoneTableClicked(int row, int column)
{
    Q_UNUSED(column)
    if (row < 0 || row >= m_zoneTable->rowCount()) return;
    const QTableWidgetItem *item = m_zoneTable->item(row, 0);
    if (!item) return;
    const QString zoneId = item->text();
    if (QGraphicsRectItem *zoneItem = m_zoneItems.value(zoneId)) {
        m_scene->clearSelection();
        zoneItem->setSelected(true);
        m_mapView->ensureVisible(zoneItem, 24, 24);
    }
}

void ParkingMapPage::applyEditorFields()
{
    if (m_updatingEditor || !m_editMode) return;
    const QString oldZoneId = selectedZoneId();
    const int index = zoneIndexById(oldZoneId);
    if (index < 0) return;

    const ParkingZoneLayout previousZone = m_zones.at(index);
    ParkingZoneLayout zone = previousZone;
    const QString previousChannel = zone.cameraChannel;
    const QString newZoneId = normalizedZoneId(m_zoneIdEdit->text());
    if (newZoneId.isEmpty()) return;
    const int duplicateIndex = zoneIndexById(newZoneId);
    if (duplicateIndex >= 0 && duplicateIndex != index) {
        m_layoutStatusLabel->setText(QStringLiteral("Duplicate zone ID"));
        return;
    }

    zone.zoneId = newZoneId;
    zone.zoneType = m_zoneTypeCombo->currentText();
    zone.displayName = m_displayNameEdit->text().trimmed().isEmpty()
        ? zone.zoneId
        : m_displayNameEdit->text().trimmed();
    zone.cameraChannel = m_cameraChannelCombo->currentText();
    const QString preferredIva = m_ivaAreaCombo->currentText();
    zone.ivaAreaId = preferredIva;
    zone.hallSensorId = normalizedZoneId(m_hallSensorEdit->text());
    zone.enabled = m_enabledCheck->isChecked();
    zone.rect = QRectF(m_xSpin->value(), m_ySpin->value(),
                       m_widthSpin->value(), m_heightSpin->value());
    zone.rotation = m_rotationSlider->value();
    if (previousChannel != zone.cameraChannel) {
        zone.rect = nextZoneRectForChannel(zone.cameraChannel);
        zone.rotation = 0.0;
    }
    applyZoneTypeRules(&zone, previousChannel != zone.cameraChannel ? QString() : preferredIva);
    if (sameZoneLayout(previousZone, zone)) return;
    if (m_editorSliderGestureActive) {
        if (!m_editorSliderSnapshotRecorded) {
            pushUndoSnapshot(m_editorSliderSnapshot);
            m_editorSliderSnapshotRecorded = true;
        }
    } else {
        pushCurrentLayoutToUndoHistory();
    }
    m_zones[index] = zone;

    if (oldZoneId != zone.zoneId) {
        rebuildScene();
        if (QGraphicsRectItem *item = m_zoneItems.value(zone.zoneId)) item->setSelected(true);
    } else if (QGraphicsRectItem *item = m_zoneItems.value(zone.zoneId)) {
        item->setPos(zone.rect.topLeft());
        item->setRect(QRectF(0, 0, zone.rect.width(), zone.rect.height()));
        item->setTransformOriginPoint(item->rect().center());
        item->setRotation(zone.rotation);
        updateZoneVisual(zone.zoneId);
    }
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty();
}

void ParkingMapPage::setEditMode(bool enabled)
{
    Q_UNUSED(enabled);
    m_editMode = false;
    if (m_editToggleButton) {
        const QSignalBlocker blocker(m_editToggleButton);
        m_editToggleButton->setChecked(false);
        m_editToggleButton->setText(QStringLiteral("Admin layout edit"));
    }
    if (m_editChannelNamesButton) m_editChannelNamesButton->setEnabled(false);
    syncItemsEditable();
    updateEditorFromSelection();
    if (!m_layoutDirty && m_layoutStatusLabel) {
        m_layoutStatusLabel->setText(QStringLiteral("Fixed operator layout"));
    }
}

void ParkingMapPage::loadLayout()
{
    QString error;
    QList<ParkingZoneLayout> loadedZones;
    ParkingChannelDisplayNames loadedChannelDisplayNames;
    ParkingOverviewLayouts loadedOverviewLayouts;
    if (QFile::exists(m_layoutPath)
        && loadParkingZoneLayout(m_layoutPath, &loadedZones,
                                 &loadedChannelDisplayNames,
                                 &loadedOverviewLayouts, &error)) {
        int ignoredCorridorSlots = 0;
        m_zones.clear();
        for (const ParkingZoneLayout &zone : std::as_const(loadedZones)) {
            if (isParkingControlChannel(zone.cameraChannel)) m_zones.append(zone);
            else ++ignoredCorridorSlots;
        }
        if (m_zones.isEmpty()) m_zones = defaultParkingZoneLayout();
        m_channelDisplayNames = loadedChannelDisplayNames;
        m_overviewLayouts = loadedOverviewLayouts;
        clearUndoHistory();
        setLayoutDirty(false, ignoredCorridorSlots > 0
                           ? QStringLiteral("Loaded local layout; ignored %1 non-parking channel slot(s)")
                                 .arg(ignoredCorridorSlots)
                           : QStringLiteral("Loaded local layout"));
        return;
    }

    m_zones = defaultParkingZoneLayout();
    m_channelDisplayNames.clear();
    m_overviewLayouts = defaultParkingOverviewLayouts();
    clearUndoHistory();
    setLayoutDirty(false, QStringLiteral("Loaded default layout"));
}

QString ParkingMapPage::channelDisplayName(const QString &channel) const
{
    const QString normalizedChannel = channel.trimmed().toUpper();
    if (!isCameraChannelId(normalizedChannel)) return QString();
    return m_channelDisplayNames.value(normalizedChannel, normalizedChannel);
}

bool ParkingMapPage::setChannelDisplayName(const QString &channel, const QString &displayName)
{
    const QString normalizedChannel = channel.trimmed().toUpper();
    if (!isCameraChannelId(normalizedChannel)) return false;

    QString normalizedDisplayName = displayName.trimmed();
    if (normalizedDisplayName.compare(normalizedChannel, Qt::CaseInsensitive) == 0) {
        normalizedDisplayName.clear();
    }
    const QString previousDisplayName = m_channelDisplayNames.value(normalizedChannel);
    if (previousDisplayName == normalizedDisplayName) return false;

    pushCurrentLayoutToUndoHistory();
    if (normalizedDisplayName.isEmpty()) {
        m_channelDisplayNames.remove(normalizedChannel);
    } else {
        m_channelDisplayNames.insert(normalizedChannel, normalizedDisplayName);
    }
    rebuildScene();
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty(QStringLiteral("%1 display name updated").arg(normalizedChannel));
    return true;
}

void ParkingMapPage::editChannelDisplayNames()
{
    if (!m_editMode) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit channel display names"));
    dialog.setMinimumWidth(430);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        QStringLiteral("Display names are local UI labels. Internal CH1-CH4 and IVA1-IVA4 IDs remain unchanged."),
        &dialog);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: #54636d;"));
    layout->addWidget(description);

    auto *form = new QFormLayout;
    QHash<QString, QLineEdit *> edits;
    for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
        const QString channel = QStringLiteral("CH%1").arg(channelNumber);
        auto *edit = new QLineEdit(m_channelDisplayNames.value(channel), &dialog);
        edit->setObjectName(QStringLiteral("channelDisplayName_%1").arg(channel));
        edit->setPlaceholderText(QStringLiteral("Optional local display name"));
        edit->setMaxLength(32);
        form->addRow(channel, edit);
        edits.insert(channel, edit);
    }
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    ParkingChannelDisplayNames updatedNames;
    for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
        const QString displayName = it.value()->text().trimmed();
        if (!displayName.isEmpty()
            && displayName.compare(it.key(), Qt::CaseInsensitive) != 0) {
            updatedNames.insert(it.key(), displayName);
        }
    }
    if (updatedNames == m_channelDisplayNames) return;

    pushCurrentLayoutToUndoHistory();
    m_channelDisplayNames = updatedNames;
    rebuildScene();
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty(QStringLiteral("Channel display names updated"));
}

void ParkingMapPage::editOverviewLayout()
{
    ParkingOverviewLayouts workingLayouts = m_overviewLayouts;
    if (workingLayouts.isEmpty()) workingLayouts = defaultParkingOverviewLayouts();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("parkingOverviewLayoutDialog"));
    dialog.setWindowTitle(QStringLiteral("Edit overview layout"));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        QStringLiteral("Camera 2–12 preview channels can be shown as parking, entrance, exit, or hidden. "
                       "Camera 1 uses its live slot mapping and is edited from its detail view."),
        &dialog);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color:#54636d;"));
    layout->addWidget(description);

    auto *cameraCombo = new QComboBox(&dialog);
    cameraCombo->setObjectName(QStringLiteral("parkingOverviewCameraCombo"));
    for (const ParkingOverviewCameraLayout &camera : std::as_const(workingLayouts)) {
        if (camera.cameraNumber != 1) {
            cameraCombo->addItem(QStringLiteral("Camera %1").arg(camera.cameraNumber),
                                 camera.cameraNumber);
        }
    }
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Camera"), cameraCombo);

    QHash<QString, QComboBox *> roleCombos;
    QHash<QString, QSpinBox *> slotSpins;
    for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
        const QString channel = QStringLiteral("CH%1").arg(channelNumber);
        auto *row = new QWidget(&dialog);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *roleCombo = new QComboBox(row);
        roleCombo->addItem(QStringLiteral("Hidden"), QStringLiteral("HIDDEN"));
        roleCombo->addItem(QStringLiteral("Parking"), QStringLiteral("PARKING"));
        roleCombo->addItem(QStringLiteral("Entrance"), QStringLiteral("ENTRANCE"));
        roleCombo->addItem(QStringLiteral("Exit"), QStringLiteral("EXIT"));
        auto *slotSpin = new QSpinBox(row);
        slotSpin->setRange(1, 8);
        slotSpin->setSuffix(QStringLiteral(" slots"));
        rowLayout->addWidget(roleCombo, 1);
        rowLayout->addWidget(slotSpin);
        form->addRow(channel, row);
        roleCombos.insert(channel, roleCombo);
        slotSpins.insert(channel, slotSpin);
    }
    layout->addLayout(form);

    bool loadingCamera = false;
    const auto findCamera = [&workingLayouts](int cameraNumber) {
        return std::find_if(workingLayouts.begin(), workingLayouts.end(),
                            [cameraNumber](const ParkingOverviewCameraLayout &camera) {
                                return camera.cameraNumber == cameraNumber;
                            });
    };
    const auto loadCamera = [&]() {
        const auto camera = findCamera(cameraCombo->currentData().toInt());
        if (camera == workingLayouts.end()) return;
        loadingCamera = true;
        for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
            const QString channel = QStringLiteral("CH%1").arg(channelNumber);
            const ParkingOverviewChannelLayout channelLayout = camera->channels.value(channel);
            QComboBox *roleCombo = roleCombos.value(channel);
            QSpinBox *slotSpin = slotSpins.value(channel);
            const int roleIndex = roleCombo->findData(channelLayout.role);
            roleCombo->setCurrentIndex(roleIndex < 0 ? 0 : roleIndex);
            slotSpin->setValue(qBound(1, channelLayout.slotCount, 8));
            slotSpin->setEnabled(channelLayout.role == QStringLiteral("PARKING"));
        }
        loadingCamera = false;
    };
    const auto storeChannel = [&](const QString &channel) {
        if (loadingCamera) return;
        const auto camera = findCamera(cameraCombo->currentData().toInt());
        if (camera == workingLayouts.end()) return;
        const QString role = roleCombos.value(channel)->currentData().toString();
        ParkingOverviewChannelLayout channelLayout;
        channelLayout.role = role;
        channelLayout.slotCount = role == QStringLiteral("PARKING")
            ? slotSpins.value(channel)->value()
            : 0;
        camera->channels.insert(channel, channelLayout);
        slotSpins.value(channel)->setEnabled(role == QStringLiteral("PARKING"));
    };
    connect(cameraCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [loadCamera](int) { loadCamera(); });
    for (int channelNumber = 1; channelNumber <= 4; ++channelNumber) {
        const QString channel = QStringLiteral("CH%1").arg(channelNumber);
        connect(roleCombos.value(channel), qOverload<int>(&QComboBox::currentIndexChanged),
                &dialog, [storeChannel, channel](int) { storeChannel(channel); });
        connect(slotSpins.value(channel), qOverload<int>(&QSpinBox::valueChanged),
                &dialog, [storeChannel, channel](int) { storeChannel(channel); });
    }
    loadCamera();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    m_overviewLayouts = workingLayouts;
    rebuildOverviewScene();
    markLayoutDirty(QStringLiteral("Overview layout updated"));
    QString error;
    if (!saveLayoutNow(&error)) {
        QMessageBox::warning(this, QStringLiteral("Parking overview layout"), error);
    }
}

QString ParkingMapPage::channelPanelTitle(const QString &channel) const
{
    return channel.trimmed().toUpper();
}

void ParkingMapPage::showOverview()
{
    if (m_operationViewStack) m_operationViewStack->setCurrentIndex(0);
}

void ParkingMapPage::showCurrentZoneDetail()
{
    if (!m_operationViewStack) return;
    m_operationViewStack->setCurrentIndex(1);
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
}

QString ParkingMapPage::exampleLayoutPath() const
{
    return QFileInfo(m_layoutPath).dir().absoluteFilePath(QStringLiteral("parking_map_layout.example.json"));
}

QString ParkingMapPage::nextZoneId(const QString &prefix) const
{
    for (int number = 1; number < 1000; ++number) {
        const QString candidate = QStringLiteral("%1-%2").arg(prefix).arg(number, 2, 10, QLatin1Char('0'));
        const bool hasRuntimeState = m_lastState.evSlots.contains(candidate)
            || m_lastState.parkingSlots.contains(candidate);
        if (zoneIndexById(candidate) < 0 && !hasRuntimeState) {
            return candidate;
        }
    }
    return QStringLiteral("%1-X").arg(prefix);
}

QRectF ParkingMapPage::nextZoneRectForChannel(const QString &channel) const
{
    const QRectF panel = channelPanelRect(channel);
    auto isFree = [this](const QRectF &candidate) {
        const QRectF paddedCandidate = candidate.adjusted(-5, -5, 5, 5);
        for (const ParkingZoneLayout &zone : m_zones) {
            if (paddedCandidate.intersects(zone.rect.adjusted(-5, -5, 5, 5))) {
                return false;
            }
        }
        return true;
    };

    for (int row = 0; row < 1; ++row) {
        for (int column = 0; column < 4; ++column) {
            const QRectF candidate = parkingSlotRectForChannel(
                panel, channel, row, column);
            if (isFree(candidate)) return candidate;
        }
    }

    for (int stagingIndex = 0; stagingIndex < 1000; ++stagingIndex) {
        const int column = stagingIndex % 9;
        const int row = stagingIndex / 9;
        const QRectF candidate(40 + (column * 96),
                               kParkingMapBaseHeight + 30.0 + (row * 72),
                               84, 58);
        if (isFree(candidate)) return candidate;
    }

    return QRectF(40, kParkingMapBaseHeight + 30.0, 84, 58);
}

int ParkingMapPage::zoneIndexById(const QString &zoneId) const
{
    const QString normalized = normalizedZoneId(zoneId);
    for (int i = 0; i < m_zones.size(); ++i) {
        if (m_zones.at(i).zoneId == normalized) return i;
    }
    return -1;
}

QString ParkingMapPage::selectedZoneId() const
{
    if (!m_scene) return QString();
    const QList<QGraphicsItem *> selectedItems = m_scene->selectedItems();
    if (selectedItems.isEmpty()) return QString();
    return selectedItems.first()->data(0).toString();
}

ParkingZoneLayout *ParkingMapPage::selectedZone()
{
    const int index = zoneIndexById(selectedZoneId());
    return index < 0 ? nullptr : &m_zones[index];
}

const ParkingZoneLayout *ParkingMapPage::selectedZone() const
{
    const int index = zoneIndexById(selectedZoneId());
    return index < 0 ? nullptr : &m_zones[index];
}

bool ParkingMapPage::stateForZone(const QString &zoneId, SlotState *state) const
{
    if (m_lastState.evSlots.contains(zoneId)) {
        if (state) *state = m_lastState.evSlots.value(zoneId).state;
        return true;
    }
    if (m_lastState.parkingSlots.contains(zoneId)) {
        if (state) *state = m_lastState.parkingSlots.value(zoneId).state;
        return true;
    }
    return false;
}

SlotVisualState ParkingMapPage::visualStateForZone(const QString &zoneId, bool *known) const
{
    SlotVisualState visual;
    if (known) *known = false;

    if (m_lastState.evSlots.contains(zoneId)) {
        const EvSlotInfo slot = m_lastState.evSlots.value(zoneId);
        visual = slot.visual;
        if (known) *known = true;

        if (visual.occupancy == SlotOccupancy::Unknown) {
            if (slot.state == SlotState::Vacant) {
                visual.occupancy = SlotOccupancy::Vacant;
            } else if (slot.state != SlotState::SensorError) {
                visual.occupancy = SlotOccupancy::Occupied;
                visual.vehicleClass = slot.isEv ? VehicleClass::Electric : VehicleClass::General;
            }
        }
        if (visual.alarm == SlotAlarmKind::None) {
            visual.alarm = slotAlarmKindFromText(slot.alarmText, slot.state);
        }
        if (slot.state == SlotState::Acked) visual.alarmAcknowledged = true;
        return visual;
    }

    if (m_lastState.parkingSlots.contains(zoneId)) {
        const ParkingSlotInfo slot = m_lastState.parkingSlots.value(zoneId);
        visual = slot.visual;
        if (known) *known = true;

        if (visual.occupancy == SlotOccupancy::Unknown) {
            if (slot.state == SlotState::Vacant) {
                visual.occupancy = SlotOccupancy::Vacant;
            } else if (slot.state == SlotState::Occupied) {
                visual.occupancy = SlotOccupancy::Occupied;
                visual.vehicleClass = VehicleClass::General;
            }
        }
        if (visual.alarm == SlotAlarmKind::None) {
            visual.alarm = slotAlarmKindFromText(QString(), slot.state);
        }
        if (slot.state == SlotState::Acked) visual.alarmAcknowledged = true;
        return visual;
    }

    return visual;
}

void ParkingMapPage::handleZoneItemDragStarted(const QString &zoneId)
{
    if (m_rebuildingScene) return;
    if (!m_editMode && m_editToggleButton) {
        m_editToggleButton->setChecked(true);
    }
    if (zoneIndexById(zoneId) < 0) return;
    syncZonesFromItems();
    m_pendingDragSnapshot = captureLayoutSnapshot();
    m_hasPendingDragSnapshot = true;
}

void ParkingMapPage::handleZoneItemMoved(const QString &zoneId)
{
    if (m_rebuildingScene) return;
    const int index = zoneIndexById(zoneId);
    if (index < 0) return;
    QGraphicsRectItem *item = m_zoneItems.value(zoneId);
    if (!item) return;

    const ParkingZoneLayout previousZone = m_zones.at(index);
    ParkingZoneLayout zone = previousZone;
    zone.rect = QRectF(item->pos().x(), item->pos().y(), item->rect().width(), item->rect().height());
    zone.rotation = item->rotation();
    const QString detectedChannel = channelForScenePoint(item->sceneBoundingRect().center());
    if (!detectedChannel.isEmpty() && isParkingControlChannel(detectedChannel)) {
        zone.cameraChannel = detectedChannel;
    } else if (!detectedChannel.isEmpty()) {
        item->setPos(previousZone.rect.topLeft());
        item->setRect(QRectF(0, 0, previousZone.rect.width(), previousZone.rect.height()));
        item->setRotation(previousZone.rotation);
        m_hasPendingDragSnapshot = false;
        if (m_layoutStatusLabel) {
            m_layoutStatusLabel->setText(QStringLiteral("Only CH1/CH3 accept parking slots"));
        }
        updateEditorFromSelection();
        return;
    }
    applyZoneTypeRules(&zone);
    if (sameZoneLayout(previousZone, zone)) {
        m_hasPendingDragSnapshot = false;
        return;
    }
    if (m_hasPendingDragSnapshot) {
        pushUndoSnapshot(m_pendingDragSnapshot);
    } else {
        pushCurrentLayoutToUndoHistory();
    }
    m_hasPendingDragSnapshot = false;
    m_zones[index] = zone;

    updateZoneVisual(zone.zoneId);
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty(QStringLiteral("%1 geometry changed").arg(zone.zoneId));
}

void ParkingMapPage::showZoneContextMenu(const QString &zoneId, const QPoint &screenPos)
{
    selectZoneById(zoneId);
    if (!m_editMode) return;

    QMenu menu(this);
    QAction *resetShapeAction = menu.addAction(QStringLiteral("Reset to default shape"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete zone"));
    QAction *selectedAction = menu.exec(screenPos);
    if (!selectedAction) return;

    if (selectedAction == resetShapeAction) {
        if (m_editToggleButton && !m_editToggleButton->isChecked()) {
            m_editToggleButton->setChecked(true);
        }
        resetSelectedZoneShape();
        return;
    }

    if (selectedAction == deleteAction) {
        deleteSelectedZone();
    }
}

void ParkingMapPage::resetSelectedZoneShape()
{
    syncZonesFromItems();
    const QString selectedId = selectedZoneId();
    const int index = zoneIndexById(selectedId);
    if (index < 0) return;

    const ParkingZoneLayout previousZone = m_zones.at(index);
    QGraphicsRectItem *item = m_zoneItems.value(previousZone.zoneId);
    const bool itemAlreadyDefault = !item
        || (item->rect().width() == 124.0
            && item->rect().height() == 72.0
            && item->rotation() == 0.0);
    if (previousZone.rect.width() == 124.0
        && previousZone.rect.height() == 72.0
        && previousZone.rotation == 0.0
        && itemAlreadyDefault) {
        return;
    }

    pushCurrentLayoutToUndoHistory();
    ParkingZoneLayout zone = previousZone;
    const QPointF position = item ? item->pos() : zone.rect.topLeft();
    zone.rect = QRectF(position.x(), position.y(), 124, 72);
    zone.rotation = 0.0;
    m_zones[index] = zone;
    if (item) {
        item->setPos(position);
        item->setRect(QRectF(0, 0, zone.rect.width(), zone.rect.height()));
        item->setTransformOriginPoint(item->rect().center());
        item->setRotation(zone.rotation);
        updateZoneVisual(zone.zoneId);
    }
    updateZoneTable();
    updateEditorFromSelection();
    markLayoutDirty(QStringLiteral("%1 shape reset to default").arg(zone.zoneId));
}

void ParkingMapPage::selectZoneById(const QString &zoneId)
{
    QGraphicsRectItem *item = m_zoneItems.value(normalizedZoneId(zoneId));
    if (!item || !m_scene) return;
    const QSignalBlocker blocker(m_scene);
    m_scene->clearSelection();
    item->setSelected(true);
    m_mapView->ensureVisible(item, 24, 24);
    updateEditorFromSelection();
    const int row = zoneIndexById(zoneId);
    if (row >= 0) {
        QSignalBlocker tableBlocker(m_zoneTable);
        m_zoneTable->selectRow(row);
    }
}

QString ParkingMapPage::channelForScenePoint(const QPointF &point) const
{
    for (const QString &channel : {QStringLiteral("CH1"), QStringLiteral("CH2"),
                                   QStringLiteral("CH3"), QStringLiteral("CH4")}) {
        if (channelPanelRect(channel).contains(point)) {
            return channel;
        }
    }
    return QString();
}

QString ParkingMapPage::nextIvaAreaForChannel(const QString &channel, const QString &excludeZoneId) const
{
    const QStringList ivaAreas = {
        QStringLiteral("IVA1"),
        QStringLiteral("IVA2"),
        QStringLiteral("IVA3"),
        QStringLiteral("IVA4")
    };
    for (const QString &ivaAreaId : ivaAreas) {
        if (!ivaUsedInChannel(channel, ivaAreaId, excludeZoneId)) {
            return ivaAreaId;
        }
    }
    return QStringLiteral("IVA1");
}

bool ParkingMapPage::ivaUsedInChannel(const QString &channel, const QString &ivaAreaId, const QString &excludeZoneId) const
{
    const QString normalizedExclude = normalizedZoneId(excludeZoneId);
    for (const ParkingZoneLayout &zone : m_zones) {
        if (zone.zoneId == normalizedExclude) continue;
        if (zone.cameraChannel == channel
            && zone.ivaAreaId == ivaAreaId) {
            return true;
        }
    }
    return false;
}

void ParkingMapPage::applyZoneTypeRules(ParkingZoneLayout *zone, const QString &preferredIva) const
{
    if (!zone) return;
    zone->zoneType = zone->zoneType.trimmed().toUpper();
    zone->cameraChannel = zone->cameraChannel.trimmed().toUpper();
    if (!isParkingControlChannel(zone->cameraChannel)) {
        zone->cameraChannel = QStringLiteral("CH1");
    }
    if (zone->zoneType != QStringLiteral("EV")
        && zone->zoneType != QStringLiteral("GENERAL")) {
        zone->zoneType = QStringLiteral("GENERAL");
    }

    QString ivaAreaId = preferredIva.trimmed().toUpper();
    if (!isIvaAreaId(ivaAreaId)) {
        ivaAreaId = zone->ivaAreaId.trimmed().toUpper();
    }
    if (!isIvaAreaId(ivaAreaId)
        || ivaUsedInChannel(zone->cameraChannel, ivaAreaId, zone->zoneId)) {
        ivaAreaId = nextIvaAreaForChannel(zone->cameraChannel, zone->zoneId);
    }
    zone->ivaAreaId = ivaAreaId;
}

void ParkingMapPage::rebuildScene()
{
    m_rebuildingScene = true;
    const QSignalBlocker sceneBlocker(m_scene);
    m_zoneItems.clear();
    m_zoneAccentBars.clear();
    m_zoneTypeIcons.clear();
    m_zoneLabels.clear();
    m_zonePlateLabels.clear();
    m_zoneStateLabels.clear();
    m_zoneMetaLabels.clear();
    m_channelSummaryLabels.clear();
    m_corridorEventLabels.clear();
    m_zoneAlarmHalos.clear();
    m_zoneAlarmBeacons.clear();
    m_zoneAlarmLabels.clear();
    m_scene->clear();
    qreal sceneHeight = kParkingMapBaseHeight;
    for (const ParkingZoneLayout &zone : m_zones) {
        sceneHeight = qMax(sceneHeight, zone.rect.bottom() + 28.0);
    }
    m_scene->setSceneRect(0, 0, kParkingMapCanvasWidth, sceneHeight);
    m_scene->addRect(m_scene->sceneRect(), QPen(QColor(QStringLiteral("#101418"))), QBrush(QColor(QStringLiteral("#1b1f23"))));

    if (sceneHeight > kParkingMapBaseHeight) {
        const QRectF stagingArea(30, kParkingMapBaseHeight - 8.0,
                                 kParkingMapCanvasWidth - 60.0,
                                 sceneHeight - kParkingMapBaseHeight - 4.0);
        m_scene->addRect(stagingArea,
                         QPen(QColor(QStringLiteral("#83919b")), 1, Qt::DashLine),
                         QBrush(QColor(QStringLiteral("#20262b"))));
        auto *stagingTitle = m_scene->addSimpleText(
            QStringLiteral("NEW SLOT STAGING | Drag into an available channel position"));
        QFont stagingFont;
        stagingFont.setPointSize(8);
        stagingFont.setBold(true);
        stagingTitle->setFont(stagingFont);
        stagingTitle->setBrush(QColor(QStringLiteral("#dce5eb")));
        stagingTitle->setPos(42, kParkingMapBaseHeight - 2.0);
    }

    for (const QString &channel : {QStringLiteral("CH1"), QStringLiteral("CH2"),
                                   QStringLiteral("CH3"), QStringLiteral("CH4")}) {
        const QRectF panel = channelPanelRect(channel);
        const bool parkingControl = isParkingControlChannel(channel);
        if (!parkingControl) continue;
        m_scene->addRect(panel,
                         QPen(QColor(QStringLiteral("#5f6c75")), 2),
                         QBrush(QColor(QStringLiteral("#242a2f"))));
        QFont titleFont;
        titleFont.setPointSize(10);
        titleFont.setBold(true);
        const QString fullTitle = channelPanelTitle(channel);
        const QString visibleTitle = QFontMetrics(titleFont).elidedText(
            fullTitle, Qt::ElideRight, qRound(panel.width() - 24.0));
        auto *title = m_scene->addSimpleText(visibleTitle);
        title->setFont(titleFont);
        title->setBrush(QColor(QStringLiteral("#f4f8fb")));
        title->setToolTip(fullTitle);
        title->setPos(panel.x() + 12, panel.y() + 10);

        auto *channelSummary = m_scene->addSimpleText(
            QStringLiteral("4 slots · waiting data"));
        QFont summaryFont;
        summaryFont.setPointSize(7);
        summaryFont.setBold(true);
        channelSummary->setFont(summaryFont);
        channelSummary->setBrush(QColor(QStringLiteral("#b0bec5")));
        channelSummary->setPos(panel.x() + 28, panel.y() + 30.0);
        m_channelSummaryLabels.insert(channel, channelSummary);
    }

    for (const ParkingZoneLayout &zone : m_zones) {
        auto *item = new ParkingZoneGraphicsItem(zone.zoneId, QRectF(0, 0, zone.rect.width(), zone.rect.height()));
        m_scene->addItem(item);
        item->setData(0, zone.zoneId);
        item->setPos(zone.rect.topLeft());
        item->setTransformOriginPoint(item->rect().center());
        item->setRotation(zone.rotation);
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        item->setAcceptHoverEvents(false);
        item->setCursor(Qt::ArrowCursor);
        auto *accent = new QGraphicsRectItem(item);
        accent->setRect(QRectF(1, 1, qMax(1.0, zone.rect.width() - 2.0), 4));
        accent->setPen(Qt::NoPen);
        accent->setZValue(1);
        auto *label = new QGraphicsSimpleTextItem(item);
        QFont labelFont;
        labelFont.setPointSize(9);
        labelFont.setBold(true);
        label->setFont(labelFont);
        label->setPos(6, 10);
        label->setZValue(2);

        auto *typeIcon = new QGraphicsPathItem(item);
        QPainterPath boltPath;
        boltPath.moveTo(10, 0);
        boltPath.lineTo(2, 13);
        boltPath.lineTo(8, 13);
        boltPath.lineTo(5, 25);
        boltPath.lineTo(18, 8);
        boltPath.lineTo(11, 8);
        boltPath.closeSubpath();
        typeIcon->setPath(boltPath);
        typeIcon->setPen(QPen(QColor(QStringLiteral("#b9e7ff")), 1.2,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        typeIcon->setBrush(QColor(QStringLiteral("#2d9cff")));
        typeIcon->setAcceptedMouseButtons(Qt::NoButton);
        typeIcon->setPos(7, 28);
        typeIcon->setZValue(3);

        auto *plateLabel = new QGraphicsSimpleTextItem(item);
        QFont plateFont;
        plateFont.setPointSize(8);
        plateFont.setBold(true);
        plateLabel->setFont(plateFont);
        plateLabel->setBrush(QColor(QStringLiteral("#ffffff")));
        plateLabel->setZValue(3);
        plateLabel->setVisible(false);

        auto *stateLabel = new QGraphicsSimpleTextItem(item);
        QFont stateFont;
        stateFont.setPointSize(7);
        stateFont.setBold(true);
        stateLabel->setFont(stateFont);
        stateLabel->setPos(6, 38);
        stateLabel->setZValue(2);
        auto *metaLabel = new QGraphicsSimpleTextItem(item);
        QFont metaFont;
        metaFont.setPointSize(6);
        metaFont.setBold(true);
        metaLabel->setFont(metaFont);
        metaLabel->setPos(6, 10);
        metaLabel->setZValue(2);
        auto *alarmHalo = new QGraphicsEllipseItem(item);
        alarmHalo->setRect(QRectF(-14, -14, 28, 28));
        alarmHalo->setPen(Qt::NoPen);
        alarmHalo->setBrush(QColor(255, 23, 68, 120));
        alarmHalo->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        alarmHalo->setAcceptedMouseButtons(Qt::NoButton);
        alarmHalo->setZValue(5);
        alarmHalo->setVisible(false);
        auto *alarmBeacon = new QGraphicsPathItem(item);
        QPainterPath beaconPath;
        beaconPath.addRoundedRect(QRectF(-6, -5, 12, 9), 4, 4);
        beaconPath.addRect(QRectF(-8, 4, 16, 3));
        beaconPath.moveTo(-9, -2);
        beaconPath.lineTo(-13, -5);
        beaconPath.moveTo(9, -2);
        beaconPath.lineTo(13, -5);
        beaconPath.moveTo(0, -8);
        beaconPath.lineTo(0, -12);
        alarmBeacon->setPath(beaconPath);
        alarmBeacon->setPen(QPen(QColor(QStringLiteral("#ff8aa1")), 1.8,
                                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        alarmBeacon->setBrush(QColor(QStringLiteral("#ff1744")));
        alarmBeacon->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        alarmBeacon->setAcceptedMouseButtons(Qt::NoButton);
        alarmBeacon->setZValue(7);
        alarmBeacon->setVisible(false);
        auto *alarmLabel = new QGraphicsSimpleTextItem(item);
        QFont alarmFont;
        alarmFont.setPointSize(5);
        alarmFont.setBold(true);
        alarmLabel->setFont(alarmFont);
        alarmLabel->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        alarmLabel->setAcceptedMouseButtons(Qt::NoButton);
        alarmLabel->setZValue(6);
        alarmLabel->setVisible(false);
        item->onDragStarted = [this](const QString &movedZoneId) {
            handleZoneItemDragStarted(movedZoneId);
        };
        item->onGeometryPreviewChanged = [this](const QString &movedZoneId) {
            updateZoneVisual(movedZoneId);
        };
        item->onDragFinished = [this](const QString &movedZoneId) {
            handleZoneItemMoved(movedZoneId);
        };
        item->onContextMenuRequested = [this](const QString &targetZoneId, const QPoint &screenPos) {
            showZoneContextMenu(targetZoneId, screenPos);
        };
        m_zoneItems.insert(zone.zoneId, item);
        m_zoneAccentBars.insert(zone.zoneId, accent);
        m_zoneTypeIcons.insert(zone.zoneId, typeIcon);
        m_zoneLabels.insert(zone.zoneId, label);
        m_zonePlateLabels.insert(zone.zoneId, plateLabel);
        m_zoneStateLabels.insert(zone.zoneId, stateLabel);
        m_zoneMetaLabels.insert(zone.zoneId, metaLabel);
        m_zoneAlarmHalos.insert(zone.zoneId, alarmHalo);
        m_zoneAlarmBeacons.insert(zone.zoneId, alarmBeacon);
        m_zoneAlarmLabels.insert(zone.zoneId, alarmLabel);
        item->setEditingEnabled(m_editMode);
        updateZoneVisual(zone.zoneId);
    }
    m_rebuildingScene = false;
    updateAlarmAnimationState();
    updateOperationalSummary();
}

void ParkingMapPage::updateZoneVisual(const QString &zoneId)
{
    const int index = zoneIndexById(zoneId);
    if (index < 0) return;
    const ParkingZoneLayout zone = m_zones.at(index);
    QGraphicsRectItem *item = m_zoneItems.value(zoneId);
    QGraphicsRectItem *accent = m_zoneAccentBars.value(zoneId);
    QGraphicsPathItem *typeIcon = m_zoneTypeIcons.value(zoneId);
    QGraphicsSimpleTextItem *label = m_zoneLabels.value(zoneId);
    QGraphicsSimpleTextItem *plateLabel = m_zonePlateLabels.value(zoneId);
    QGraphicsSimpleTextItem *stateLabel = m_zoneStateLabels.value(zoneId);
    QGraphicsSimpleTextItem *metaLabel = m_zoneMetaLabels.value(zoneId);
    QGraphicsEllipseItem *alarmHalo = m_zoneAlarmHalos.value(zoneId);
    QGraphicsPathItem *alarmBeacon = m_zoneAlarmBeacons.value(zoneId);
    QGraphicsSimpleTextItem *alarmLabel = m_zoneAlarmLabels.value(zoneId);
    if (!item || !accent || !label || !stateLabel || !metaLabel) return;

    bool visualKnown = false;
    const SlotVisualState visual = visualStateForZone(zone.zoneId, &visualKnown);
    item->setBrush(slotFillColor(visualKnown, visual, zone.enabled));
    const QColor borderColor = slotBorderColor(zone);
    QPen slotPen(borderColor, item->isSelected() ? 3.5 : (zone.enabled ? 2.5 : 1.0));
    slotPen.setJoinStyle(Qt::MiterJoin);
    item->setPen(slotPen);
    accent->setRect(QRectF(1, 1, qMax(1.0, item->rect().width() - 2.0), 4));
    accent->setPen(Qt::NoPen);
    accent->setBrush(zone.enabled ? borderColor : QColor(QStringLiteral("#68727a")));
    QString toolTip = QStringLiteral("%1 | %2 | %3 | %4 | %5")
                          .arg(zone.zoneId,
                               zone.cameraChannel,
                               displayIvaText(zone.ivaAreaId),
                               zone.hallSensorId.isEmpty() ? QStringLiteral("HALL-") : zone.hallSensorId,
                               compactVehicleText(visualKnown, visual));
    if (visual.alarm != SlotAlarmKind::None) {
        toolTip += QStringLiteral(" | WARNING: %1%2")
                       .arg(slotAlarmText(visual.alarm),
                            visual.alarmAcknowledged ? QStringLiteral(" (ACK)") : QString());
    }
    item->setToolTip(toolTip);
    label->setText(zone.displayName.isEmpty() ? zone.zoneId : zone.displayName);
    label->setBrush(slotTextColor(zone.enabled));
    const QString parkingStateText = !visualKnown
        ? QStringLiteral("DATA WAIT")
        : (visual.occupancy == SlotOccupancy::Vacant
            ? QStringLiteral("VACANT")
            : (visual.occupancy == SlotOccupancy::Occupied
                ? QStringLiteral("OCCUPIED") : QStringLiteral("UNKNOWN")));
    stateLabel->setText(parkingStateText);
    stateLabel->setBrush(slotTextColor(zone.enabled));
    stateLabel->setPos(6, qMax(22.0, item->rect().height() - 18.0));
    metaLabel->setText(slotMetaText(zone));
    metaLabel->setVisible(false);

    if (typeIcon) {
        typeIcon->setVisible(zone.enabled && zone.zoneType == QStringLiteral("EV"));
    }
    if (plateLabel) {
        const bool occupied = visualKnown && visual.occupancy == SlotOccupancy::Occupied;
        const QString plateNumber = occupied ? plateNumberForZone(m_lastState, zone.zoneId) : QString();
        plateLabel->setText(plateNumber);
        const QRectF plateBounds = plateLabel->boundingRect();
        plateLabel->setPos(qMax(30.0, (item->rect().width() - plateBounds.width()) / 2.0), 30.0);
        plateLabel->setVisible(zone.enabled && !plateNumber.isEmpty());
    }

    if (alarmHalo && alarmBeacon && alarmLabel) {
        const bool hasAlarm = visual.alarm != SlotAlarmKind::None;
        const QColor color = visual.alarmAcknowledged
            ? QColor(QStringLiteral("#78909c")) : alarmColor(visual.alarm);
        const QPointF beaconPosition(item->rect().width() - 7.0, -3.0);
        alarmHalo->setPos(beaconPosition);
        alarmBeacon->setPos(beaconPosition);
        alarmHalo->setBrush(QColor(color.red(), color.green(), color.blue(), 120));
        alarmBeacon->setBrush(color);
        alarmBeacon->setPen(QPen(color.lighter(155), 1.8,
                                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        alarmLabel->setText(compactAlarmText(visual.alarm, visual.alarmAcknowledged));
        alarmLabel->setBrush(visual.alarmAcknowledged
                                 ? QColor(QStringLiteral("#aeb8c0"))
                                 : color.lighter(135));
        const QRectF alarmBounds = alarmLabel->boundingRect();
        alarmLabel->setPos(qMax(4.0, item->rect().width() - alarmBounds.width() - 5.0), 7.0);
        alarmHalo->setVisible(hasAlarm && !visual.alarmAcknowledged);
        alarmBeacon->setVisible(hasAlarm);
        alarmLabel->setVisible(hasAlarm);
    }
}

void ParkingMapPage::updateAllZoneVisuals()
{
    for (const ParkingZoneLayout &zone : m_zones) {
        updateZoneVisual(zone.zoneId);
    }
}

void ParkingMapPage::updateAlarmAnimationState()
{
    bool hasUnacknowledgedAlarm = false;
    for (const ParkingZoneLayout &zone : m_zones) {
        bool known = false;
        const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
        if (known && visual.alarm != SlotAlarmKind::None && !visual.alarmAcknowledged) {
            hasUnacknowledgedAlarm = true;
            break;
        }
    }

    if (hasUnacknowledgedAlarm) {
        if (m_alarmPulseTimer && !m_alarmPulseTimer->isActive()) m_alarmPulseTimer->start();
    } else {
        if (m_alarmPulseTimer) m_alarmPulseTimer->stop();
        m_alarmPulsePhase = 0.0;
    }
    updateAlarmPulse();
}

void ParkingMapPage::updateAlarmPulse()
{
    const qreal pulse = (qSin(m_alarmPulsePhase) + 1.0) * 0.5;
    for (const ParkingZoneLayout &zone : m_zones) {
        QGraphicsEllipseItem *halo = m_zoneAlarmHalos.value(zone.zoneId);
        QGraphicsPathItem *beacon = m_zoneAlarmBeacons.value(zone.zoneId);
        QGraphicsSimpleTextItem *alarmLabel = m_zoneAlarmLabels.value(zone.zoneId);
        if (!halo || !beacon || !alarmLabel) continue;

        bool known = false;
        const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
        const bool hasAlarm = known && visual.alarm != SlotAlarmKind::None;
        if (!hasAlarm) {
            halo->setVisible(false);
            beacon->setVisible(false);
            alarmLabel->setVisible(false);
            continue;
        }

        beacon->setVisible(true);
        alarmLabel->setVisible(true);
        if (visual.alarmAcknowledged) {
            halo->setVisible(false);
            beacon->setOpacity(0.58);
            alarmLabel->setOpacity(0.78);
            continue;
        }

        halo->setVisible(true);
        halo->setOpacity(0.12 + (0.58 * pulse));
        beacon->setOpacity(0.62 + (0.38 * pulse));
        alarmLabel->setOpacity(0.76 + (0.24 * pulse));
    }
}

void ParkingMapPage::updateRuntimeStatusFromSelection()
{
    const ParkingZoneLayout *zone = selectedZone();
    const auto clearRuntime = [this]() {
        if (m_runtimeVehicleLabel) m_runtimeVehicleLabel->setText(QStringLiteral("-"));
        if (m_runtimeOccupiedSinceLabel) m_runtimeOccupiedSinceLabel->setText(QStringLiteral("-"));
        if (m_runtimeOccupiedTimeLabel) m_runtimeOccupiedTimeLabel->setText(QStringLiteral("-"));
        if (m_runtimeAlarmLabel) m_runtimeAlarmLabel->setText(QStringLiteral("-"));
        if (m_eventsButton) m_eventsButton->setEnabled(false);
        if (m_evidenceButton) m_evidenceButton->setEnabled(false);
        if (m_cameraButton) m_cameraButton->setEnabled(false);
    };
    if (!zone) {
        if (m_selectedTitleLabel) m_selectedTitleLabel->setText(QStringLiteral("No slot selected"));
        if (m_selectedStateLabel) {
            m_selectedStateLabel->setText(QStringLiteral("WAITING"));
            m_selectedStateLabel->setStyleSheet(statePillStyle(false, SlotState::Vacant));
        }
        if (m_runtimeDataStatusLabel) {
            m_runtimeDataStatusLabel->setText(QStringLiteral("WAITING DATA"));
            m_runtimeDataStatusLabel->setStyleSheet(runtimeDataStatusStyle(false));
        }
        clearRuntime();
        return;
    }

    SlotState selectedState = SlotState::Vacant;
    const bool selectedKnown = stateForZone(zone->zoneId, &selectedState);
    bool selectedVisualKnown = false;
    const SlotVisualState selectedVisual = visualStateForZone(zone->zoneId, &selectedVisualKnown);
    const bool runtimeKnown = selectedKnown && selectedVisualKnown;

    if (m_selectedTitleLabel) {
        m_selectedTitleLabel->setText(zone->displayName.isEmpty() ? zone->zoneId : zone->displayName);
    }
    if (m_selectedStateLabel) {
        m_selectedStateLabel->setText(displayStateText(selectedKnown, selectedState));
        m_selectedStateLabel->setStyleSheet(statePillStyle(selectedKnown, selectedState));
    }
    QDateTime occupiedSince;
    QDateTime lastUpdatedAt;
    QString occupiedTime;
    QString eventId;
    if (m_lastState.evSlots.contains(zone->zoneId)) {
        const EvSlotInfo slot = m_lastState.evSlots.value(zone->zoneId);
        occupiedSince = slot.occupiedSince;
        lastUpdatedAt = slot.lastUpdatedAt;
        occupiedTime = slot.occupiedTime;
        eventId = slot.eventId;
    } else if (m_lastState.parkingSlots.contains(zone->zoneId)) {
        const ParkingSlotInfo slot = m_lastState.parkingSlots.value(zone->zoneId);
        occupiedSince = slot.occupiedSince;
        lastUpdatedAt = slot.lastUpdatedAt;
        occupiedTime = slot.occupiedTime;
        eventId = slot.eventId;
    }
    if (!lastUpdatedAt.isValid()) lastUpdatedAt = m_lastState.generatedAt;
    const bool delayed = runtimeKnown && lastUpdatedAt.isValid()
        && lastUpdatedAt.secsTo(QDateTime::currentDateTime()) > 30;
    if (m_runtimeDataStatusLabel) {
        m_runtimeDataStatusLabel->setText(!runtimeKnown
            ? QStringLiteral("WAITING DATA")
            : (delayed ? QStringLiteral("DELAYED") : QStringLiteral("AVAILABLE")));
        m_runtimeDataStatusLabel->setStyleSheet(runtimeDataStatusStyle(runtimeKnown));
    }

    if (!runtimeKnown) {
        clearRuntime();
        return;
    }

    if (m_runtimeVehicleLabel) {
        const QString plateNumber = plateNumberForZone(m_lastState, zone->zoneId);
        const QString vehicleText = compactVehicleText(true, selectedVisual);
        m_runtimeVehicleLabel->setText(plateNumber.isEmpty()
            ? vehicleText
            : QStringLiteral("%1 · %2").arg(vehicleText, plateNumber));
    }

    const bool occupied = selectedVisual.occupancy == SlotOccupancy::Occupied;
    if (m_runtimeOccupiedSinceLabel) {
        m_runtimeOccupiedSinceLabel->setText(
            occupied && occupiedSince.isValid()
                ? occupiedSince.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("-"));
    }
    if (m_runtimeOccupiedTimeLabel) {
        const QString trimmedDuration = occupiedTime.trimmed();
        const QString liveDuration = occupied && occupiedSince.isValid()
            ? durationTextFromSeconds(qMax<qint64>(
                  0, occupiedSince.secsTo(QDateTime::currentDateTime())))
            : QString();
        m_runtimeOccupiedTimeLabel->setText(
            !liveDuration.isEmpty()
                ? liveDuration
                : (occupied && !trimmedDuration.isEmpty()
                   && trimmedDuration != QStringLiteral("-")
                       ? trimmedDuration : QStringLiteral("-")));
    }
    if (m_runtimeAlarmLabel) {
        m_runtimeAlarmLabel->setText(slotAlarmText(selectedVisual.alarm));
    }
    if (m_eventsButton) m_eventsButton->setEnabled(!eventId.isEmpty()
        || selectedVisual.alarm != SlotAlarmKind::None);
    if (m_evidenceButton) {
        m_evidenceButton->setEnabled(!m_lastState.slotImages.value(zone->zoneId).isEmpty());
    }
    if (m_cameraButton) m_cameraButton->setEnabled(isParkingControlChannel(zone->cameraChannel));
}

void ParkingMapPage::updateEditorFromSelection()
{
    updateRuntimeStatusFromSelection();
    updateVehicleImage();
    updateRecentEvents();
    m_updatingEditor = true;
    const ParkingZoneLayout *zone = selectedZone();
    const bool hasSelection = zone != nullptr;
    const QList<QWidget *> editorWidgets = {
        m_zoneIdEdit, m_displayNameEdit, m_zoneTypeCombo,
        m_enabledCheck, m_xSpin, m_ySpin,
        m_widthSpin, m_heightSpin, m_widthSlider, m_heightSlider,
        m_rotationSlider, m_deleteButton
    };
    for (QWidget *widget : editorWidgets) {
        if (widget) widget->setEnabled(m_editMode && hasSelection);
    }

    if (!zone) {
        m_zoneIdEdit->clear();
        m_displayNameEdit->clear();
        if (m_ivaAreaCombo) m_ivaAreaCombo->setCurrentText(QStringLiteral("N/A"));
        m_hallSensorEdit->clear();
        if (m_widthValueLabel) m_widthValueLabel->setText(QStringLiteral("-"));
        if (m_heightValueLabel) m_heightValueLabel->setText(QStringLiteral("-"));
        if (m_rotationValueLabel) m_rotationValueLabel->setText(QStringLiteral("-"));
        m_updatingEditor = false;
        return;
    }

    const QSignalBlocker blockZoneId(m_zoneIdEdit);
    const QSignalBlocker blockDisplay(m_displayNameEdit);
    const QSignalBlocker blockType(m_zoneTypeCombo);
    const QSignalBlocker blockChannel(m_cameraChannelCombo);
    const QSignalBlocker blockIva(m_ivaAreaCombo);
    const QSignalBlocker blockHall(m_hallSensorEdit);
    const QSignalBlocker blockEnabled(m_enabledCheck);
    const QSignalBlocker blockX(m_xSpin);
    const QSignalBlocker blockY(m_ySpin);
    const QSignalBlocker blockW(m_widthSlider);
    const QSignalBlocker blockH(m_heightSlider);
    const QSignalBlocker blockWSpin(m_widthSpin);
    const QSignalBlocker blockHSpin(m_heightSpin);
    const QSignalBlocker blockRotation(m_rotationSlider);
    m_zoneIdEdit->setText(zone->zoneId);
    m_displayNameEdit->setText(zone->displayName);
    m_zoneTypeCombo->setCurrentText(zone->zoneType);
    m_cameraChannelCombo->setCurrentText(zone->cameraChannel.isEmpty() ? QStringLiteral("CH1") : zone->cameraChannel);
    m_ivaAreaCombo->setCurrentText(displayIvaText(zone->ivaAreaId));
    m_hallSensorEdit->setText(zone->hallSensorId);
    m_enabledCheck->setChecked(zone->enabled);
    m_xSpin->setValue(zone->rect.x());
    m_ySpin->setValue(zone->rect.y());
    m_widthSlider->setValue(qRound(zone->rect.width()));
    m_heightSlider->setValue(qRound(zone->rect.height()));
    m_widthSpin->setValue(qRound(zone->rect.width()));
    m_heightSpin->setValue(qRound(zone->rect.height()));
    m_rotationSlider->setValue(qRound(zone->rotation));
    m_cameraChannelCombo->setEnabled(false);
    m_ivaAreaCombo->setEnabled(false);
    m_hallSensorEdit->setEnabled(false);
    updateGeometrySliderLabels();
    m_updatingEditor = false;
}

bool ParkingMapPage::isParkingControlChannel(const QString &channel) const
{
    const QString normalized = channel.trimmed().toUpper();
    return normalized == QStringLiteral("CH1") || normalized == QStringLiteral("CH3");
}

bool ParkingMapPage::validateLayout(QString *errorMessage) const
{
    QSet<QString> zoneIds;
    QSet<QString> ivaMappings;
    QSet<QString> channelSlotOrders;
    for (const ParkingZoneLayout &zone : m_zones) {
        if (zone.zoneId.trimmed().isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Zone ID is required");
            return false;
        }
        if (zoneIds.contains(zone.zoneId)) {
            if (errorMessage) *errorMessage = QStringLiteral("Duplicate Zone ID: %1").arg(zone.zoneId);
            return false;
        }
        zoneIds.insert(zone.zoneId);
        if (!isParkingControlChannel(zone.cameraChannel)) {
            if (errorMessage) *errorMessage = QStringLiteral("%1 cannot use corridor channel %2")
                .arg(zone.zoneId, zone.cameraChannel);
            return false;
        }
        if (zone.slotOrder < 1 || zone.slotOrder > 4) {
            if (errorMessage) *errorMessage = QStringLiteral("%1 has an invalid slot order")
                .arg(zone.zoneId);
            return false;
        }
        const QString channelSlotKey = zone.cameraChannel
            + QLatin1Char(':') + QString::number(zone.slotOrder);
        if (channelSlotOrders.contains(channelSlotKey)) {
            if (errorMessage) *errorMessage = QStringLiteral(
                "Duplicate channel slot order: %1").arg(channelSlotKey);
            return false;
        }
        channelSlotOrders.insert(channelSlotKey);
        const QRectF panel = channelPanelRect(zone.cameraChannel);
        const QGraphicsRectItem *graphicsItem = m_zoneItems.value(zone.zoneId);
        const QRectF visualBounds = graphicsItem
            ? graphicsItem->sceneBoundingRect() : zone.rect;
        if (zone.rect.width() < 16.0 || zone.rect.height() < 16.0
            || !panel.contains(visualBounds)) {
            if (errorMessage) *errorMessage = QStringLiteral("%1 is outside its parking-control area")
                .arg(zone.zoneId);
            return false;
        }
        const QString mappingKey = zone.cameraChannel + QLatin1Char(':') + zone.ivaAreaId;
        if (!isIvaAreaId(zone.ivaAreaId) || ivaMappings.contains(mappingKey)) {
            if (errorMessage) *errorMessage = QStringLiteral("Invalid or duplicate IVA mapping: %1")
                .arg(zone.zoneId);
            return false;
        }
        ivaMappings.insert(mappingKey);
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

QString ParkingMapPage::zoneSearchText(const ParkingZoneLayout &zone) const
{
    bool known = false;
    const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
    SlotState state = SlotState::Vacant;
    stateForZone(zone.zoneId, &state);
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8")
        .arg(zone.zoneId, zone.displayName, zone.cameraChannel, zone.zoneType,
             plateNumberForZone(m_lastState, zone.zoneId),
             displayStateText(known, state), compactVehicleText(known, visual),
             slotAlarmText(visual.alarm));
}

void ParkingMapPage::positionOverviewSearchPopup()
{
    if (!m_overviewSearchEdit || !m_overviewSearchPopup
        || !m_overviewSearchPopup->isVisible()) {
        return;
    }

    QWidget *overviewPage = m_overviewSearchEdit->parentWidget();
    if (!overviewPage) return;

    const QRect editRect(
        m_overviewSearchEdit->mapTo(overviewPage, QPoint(0, 0)),
        m_overviewSearchEdit->size());
    const int popupWidth = qMax(260, editRect.width());
    m_overviewSearchPopup->setFixedWidth(popupWidth);
    m_overviewSearchPopup->adjustSize();

    const int horizontalMargin = 8;
    const int gap = 4;
    int x = editRect.right() - popupWidth + 1;
    x = qBound(horizontalMargin, x,
               qMax(horizontalMargin, overviewPage->width() - popupWidth - horizontalMargin));
    int y = editRect.bottom() + gap;
    if (y + m_overviewSearchPopup->height() > overviewPage->height() - horizontalMargin) {
        y = editRect.top() - m_overviewSearchPopup->height() - gap;
    }
    y = qMax(horizontalMargin, y);
    m_overviewSearchPopup->move(x, y);
    m_overviewSearchPopup->raise();
}

void ParkingMapPage::updateOverviewSearchResults()
{
    if (!m_overviewSearchEdit || !m_overviewSearchPopup || !m_overviewSearchResults) return;

    const QString query = m_overviewSearchEdit->text().trimmed();
    m_overviewSearchResults->clear();
    if (m_overviewSearchStatusLabel) {
        m_overviewSearchStatusLabel->clear();
        m_overviewSearchStatusLabel->setVisible(false);
    }
    m_overviewSearchResults->setVisible(false);
    m_overviewSearchPopup->setVisible(false);
    if (query.isEmpty()) return;

    int matchCount = 0;
    for (const ParkingZoneLayout &zone : m_zones) {
        if (!zone.enabled || !isParkingControlChannel(zone.cameraChannel)
            || !zoneSearchText(zone).contains(query, Qt::CaseInsensitive)) {
            continue;
        }

        bool known = false;
        const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
        SlotState state = SlotState::Vacant;
        stateForZone(zone.zoneId, &state);
        const QString plate = plateNumberForZone(m_lastState, zone.zoneId);
        const QString primary = plate.isEmpty() ? zone.zoneId : plate;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2  ·  %3  ·  %4")
                .arg(primary, zone.zoneId, zone.cameraChannel,
                     displayStateText(known, state)),
            m_overviewSearchResults);
        item->setData(Qt::UserRole, zone.zoneId);
        item->setToolTip(QStringLiteral("Camera 1 · %1 · %2")
                             .arg(zone.cameraChannel, compactVehicleText(known, visual)));
        ++matchCount;
    }

    if (m_overviewSearchStatusLabel) {
        m_overviewSearchStatusLabel->setText(
            matchCount == 0
                ? QStringLiteral("No matching parking zone or plate")
                : QStringLiteral("%1 matching zone%2")
                      .arg(matchCount)
                      .arg(matchCount == 1 ? QString() : QStringLiteral("s")));
        m_overviewSearchStatusLabel->setVisible(true);
    }
    m_overviewSearchResults->setVisible(matchCount > 0);
    m_overviewSearchResults->setFixedHeight(
        matchCount > 0 ? qBound(34, (matchCount * 30) + 8, 126) : 0);
    m_overviewSearchPopup->setVisible(true);
    positionOverviewSearchPopup();
}

void ParkingMapPage::openOverviewSearchResult(QListWidgetItem *item)
{
    if (!item) return;
    const QString zoneId = item->data(Qt::UserRole).toString();
    if (zoneId.isEmpty()) return;
    selectZoneById(zoneId);
    showCurrentZoneDetail();
    if (m_overviewSearchEdit) m_overviewSearchEdit->clear();
}

bool ParkingMapPage::zoneMatchesFilters(const ParkingZoneLayout &zone) const
{
    if (!isParkingControlChannel(zone.cameraChannel)) return false;
    const QString query = m_zoneSearchEdit ? m_zoneSearchEdit->text().trimmed() : QString();
    bool known = false;
    const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
    SlotState state = SlotState::Vacant;
    stateForZone(zone.zoneId, &state);
    const QString searchable = zoneSearchText(zone);
    if (!query.isEmpty() && !searchable.contains(query, Qt::CaseInsensitive)) return false;

    const QString filter = m_stateFilterCombo
        ? m_stateFilterCombo->currentData().toString() : QStringLiteral("ALL");
    if (filter == QStringLiteral("VACANT")) {
        return known && visual.occupancy == SlotOccupancy::Vacant;
    }
    if (filter == QStringLiteral("OCCUPIED")) {
        return known && visual.occupancy == SlotOccupancy::Occupied;
    }
    if (filter == QStringLiteral("WAITING")) return !known;
    if (filter == QStringLiteral("ALERT")) {
        return known && visual.alarm != SlotAlarmKind::None
            && !visual.alarmAcknowledged;
    }
    return true;
}

void ParkingMapPage::applyZoneFilters()
{
    int visibleCount = 0;
    QString soleVisibleZoneId;
    for (int row = 0; row < m_zoneTable->rowCount(); ++row) {
        const QTableWidgetItem *idItem = m_zoneTable->item(row, 0);
        const int zoneIndex = idItem ? zoneIndexById(idItem->text()) : -1;
        const bool visible = zoneIndex >= 0 && zoneMatchesFilters(m_zones.at(zoneIndex));
        m_zoneTable->setRowHidden(row, !visible);
        if (QGraphicsRectItem *item = idItem ? m_zoneItems.value(idItem->text()) : nullptr) {
            item->setVisible(visible);
        }
        if (visible) {
            ++visibleCount;
            soleVisibleZoneId = idItem ? idItem->text() : QString();
        }
    }
    if (m_filterResultLabel) {
        m_filterResultLabel->setText(QStringLiteral("%1 of %2 zones")
            .arg(visibleCount)
            .arg(m_zoneTable->rowCount()));
    }
    if (visibleCount == 1 && m_zoneSearchEdit
        && !m_zoneSearchEdit->text().trimmed().isEmpty()
        && selectedZoneId() != soleVisibleZoneId) {
        selectZoneById(soleVisibleZoneId);
    }
}

void ParkingMapPage::updateOperationalSummary()
{
    struct ChannelCounts {
        int total = 0;
        int vacant = 0;
        int occupied = 0;
        int waiting = 0;
        int alerts = 0;
    };

    int total = 0;
    int vacant = 0;
    int occupied = 0;
    int waiting = 0;
    int alerts = 0;
    QHash<QString, ChannelCounts> channelCounts;
    for (const ParkingZoneLayout &zone : m_zones) {
        if (!zone.enabled || !isParkingControlChannel(zone.cameraChannel)) continue;
        ++total;
        ChannelCounts &channel = channelCounts[zone.cameraChannel.trimmed().toUpper()];
        ++channel.total;
        bool known = false;
        const SlotVisualState visual = visualStateForZone(zone.zoneId, &known);
        if (!known || visual.occupancy == SlotOccupancy::Unknown) {
            ++waiting;
            ++channel.waiting;
        } else if (visual.occupancy == SlotOccupancy::Vacant) {
            ++vacant;
            ++channel.vacant;
        } else if (visual.occupancy == SlotOccupancy::Occupied) {
            ++occupied;
            ++channel.occupied;
        }
        if (known && visual.alarm != SlotAlarmKind::None
            && !visual.alarmAcknowledged) {
            ++alerts;
            ++channel.alerts;
        }
    }
    if (m_totalSummaryLabel) m_totalSummaryLabel->setText(QString::number(total));
    if (m_vacantSummaryLabel) m_vacantSummaryLabel->setText(QString::number(vacant));
    if (m_occupiedSummaryLabel) m_occupiedSummaryLabel->setText(QString::number(occupied));
    if (m_waitingSummaryLabel) m_waitingSummaryLabel->setText(QString::number(waiting));
    if (m_alertSummaryLabel) m_alertSummaryLabel->setText(QString::number(alerts));
    if (m_overviewTotalSummaryLabel) m_overviewTotalSummaryLabel->setText(QString::number(total));
    if (m_overviewVacantSummaryLabel) m_overviewVacantSummaryLabel->setText(QString::number(vacant));
    if (m_overviewOccupiedSummaryLabel) m_overviewOccupiedSummaryLabel->setText(QString::number(occupied));
    if (m_overviewAlertSummaryLabel) m_overviewAlertSummaryLabel->setText(QString::number(alerts));
    updateOverviewSummary();

    for (const QString &channelId : {QStringLiteral("CH1"), QStringLiteral("CH3")}) {
        const ChannelCounts channel = channelCounts.value(channelId);
        if (QGraphicsSimpleTextItem *summary = m_channelSummaryLabels.value(channelId)) {
            summary->setText(QStringLiteral("%1 slots · vacant %2 · occupied %3 · waiting %4 · alert %5")
                                 .arg(channel.total)
                                 .arg(channel.vacant)
                                 .arg(channel.occupied)
                                 .arg(channel.waiting)
                                 .arg(channel.alerts));
            summary->setBrush(channel.alerts > 0
                                  ? QColor(QStringLiteral("#ff8a80"))
                                  : QColor(QStringLiteral("#b0bec5")));
        }
    }

    for (const QString &channelId : {QStringLiteral("CH2"), QStringLiteral("CH4")}) {
        QGraphicsSimpleTextItem *statusLabel = m_corridorEventLabels.value(channelId);
        if (!statusLabel) continue;
        QString latestEvent;
        QString latestStatus;
        for (const MonitoringEvent &event : std::as_const(m_recentEvents)) {
            if (event.sourceId.trimmed().compare(channelId, Qt::CaseInsensitive) != 0) continue;
            latestEvent = event.eventType.trimmed();
            latestStatus = monitoringEventStatusText(event).trimmed();
            break;
        }
        if (latestEvent.isEmpty()) {
            statusLabel->setText(QStringLiteral("No recent events"));
            statusLabel->setBrush(QColor(QStringLiteral("#8a969f")));
        } else {
            statusLabel->setText(QStringLiteral("Latest: %1 · %2")
                                     .arg(latestEvent, latestStatus.isEmpty()
                                              ? QStringLiteral("RECORDED")
                                              : latestStatus));
            statusLabel->setBrush(latestStatus.compare(QStringLiteral("OPEN"),
                                                        Qt::CaseInsensitive) == 0
                                      ? QColor(QStringLiteral("#ffcc80"))
                                      : QColor(QStringLiteral("#b0bec5")));
        }
    }
}

void ParkingMapPage::updateOverviewSummary()
{
    updateOverviewScene();
}

void ParkingMapPage::rebuildOverviewScene()
{
    if (!m_overviewScene) return;
    m_overviewSlotItems.clear();
    m_overviewScene->clear();
    const QStringList channels = {
        QStringLiteral("CH1"), QStringLiteral("CH2"),
        QStringLiteral("CH3"), QStringLiteral("CH4")
    };
    QHash<QString, QList<const ParkingZoneLayout *>> zonesByChannel;
    for (const ParkingZoneLayout &zone : m_zones) {
        const QString channel = zone.cameraChannel.trimmed().isEmpty()
            ? QStringLiteral("UNASSIGNED")
            : zone.cameraChannel.trimmed().toUpper();
        zonesByChannel[channel].append(&zone);
    }
    for (auto it = zonesByChannel.begin(); it != zonesByChannel.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const ParkingZoneLayout *left, const ParkingZoneLayout *right) {
                      if (left->slotOrder != right->slotOrder) {
                          return left->slotOrder < right->slotOrder;
                      }
                      return left->zoneId < right->zoneId;
                  });
    }

    const qreal sceneWidth = kOverviewLayoutMetrics.sceneWidth();
    const qreal sceneHeight = kOverviewLayoutMetrics.sceneHeight();
    m_overviewScene->setSceneRect(0, 0, sceneWidth, sceneHeight);
    m_overviewScene->addRect(m_overviewScene->sceneRect(), Qt::NoPen,
                             QBrush(QColor(QStringLiteral("#20272c"))));

    auto addText = [this](const QString &text, const QPointF &position,
                          int pointSize, const QColor &color, bool bold = false) {
        auto *item = m_overviewScene->addSimpleText(text);
        QFont font;
        font.setPointSize(pointSize);
        font.setBold(bold);
        item->setFont(font);
        item->setBrush(color);
        item->setPos(position);
        return item;
    };
    const QColor liveAccent(QStringLiteral("#2d9cff"));
    const QColor livePanel(QStringLiteral("#2d353b"));
    const QColor previewPanel(QStringLiteral("#272d32"));
    const QColor liveParkingChannelColor(QStringLiteral("#81c784"));
    const QColor previewParkingChannelColor(QStringLiteral("#59636a"));
    const QColor entranceAccent(QStringLiteral("#4dd0e1"));
    const QColor entranceFill(QStringLiteral("#19353a"));
    const QColor exitAccent(QStringLiteral("#ffb74d"));
    const QColor exitFill(QStringLiteral("#3a2b18"));

    const ParkingOverviewLayouts overviewLayouts = m_overviewLayouts.isEmpty()
        ? defaultParkingOverviewLayouts()
        : m_overviewLayouts;
    for (const ParkingOverviewCameraLayout &cameraSpec : overviewLayouts) {
        const int cameraNumber = cameraSpec.cameraNumber;
        const bool live = cameraNumber == 1;
        const QRectF cameraRect = overviewCameraRect(cameraSpec);
        auto *cameraItem = m_overviewScene->addRect(
            cameraRect, Qt::NoPen,
            QBrush(live ? livePanel : previewPanel));
        cameraItem->setToolTip(
            live ? QStringLiteral("Camera 1 | Live | Double-click to open detail")
                 : QStringLiteral("Camera %1 | UI preview").arg(cameraNumber));
        addText(live ? QStringLiteral("C%1 · LIVE").arg(cameraNumber)
                     : QStringLiteral("C%1").arg(cameraNumber),
                cameraRect.topLeft() + QPointF(10, 8), 9,
                live ? QColor(QStringLiteral("#e1f5fe"))
                     : QColor(QStringLiteral("#b0bac0")), true);
        if (live) {
            m_overviewScene->addLine(
                cameraRect.left() + 1.0, cameraRect.top() + 1.0,
                cameraRect.right() - 1.0, cameraRect.top() + 1.0,
                QPen(liveAccent, 3.0));
        }
        for (const QString &channel : channels) {
            const OverviewChannelRole channelRole = overviewChannelRole(
                cameraSpec, channel);
            if (channelRole == OverviewChannelRole::Hidden) continue;
            const QRectF channelRect = overviewChannelRect(cameraRect, channel);

            if (channelRole == OverviewChannelRole::Entrance
                || channelRole == OverviewChannelRole::Exit) {
                const bool entrance = channelRole
                    == OverviewChannelRole::Entrance;
                const QColor accessAccent = entrance
                    ? entranceAccent : exitAccent;
                const QColor accessFill = entrance ? entranceFill : exitFill;
                const QString accessType = entrance
                    ? QStringLiteral("Entrance") : QStringLiteral("Exit");
                const QRectF laneRect = overviewAccessLaneRect(
                    cameraRect, channelRect, channel);
                const QString normalizedChannel = channel.trimmed().toUpper();
                const bool northSouth = normalizedChannel == QStringLiteral("CH1")
                    || normalizedChannel == QStringLiteral("CH3");
                const bool northSide = normalizedChannel == QStringLiteral("CH1");
                const bool westSide = normalizedChannel == QStringLiteral("CH2");
                auto *lane = m_overviewScene->addRect(
                    laneRect, QPen(accessAccent.darker(115), 1.2),
                    QBrush(accessFill));
                lane->setToolTip(
                    QStringLiteral("Camera %1 | %2 | %3")
                        .arg(cameraNumber)
                        .arg(channel)
                        .arg(accessType));

                // The facility border remains continuous; draw the access
                // gate immediately inside that external boundary.
                QPen arrowPen(accessAccent, 2.2);
                arrowPen.setCapStyle(Qt::RoundCap);
                arrowPen.setJoinStyle(Qt::RoundJoin);
                const QString arrowToolTip = lane->toolTip()
                    + QStringLiteral(" | Direction arrow");
                if (northSouth) {
                    const qreal gateY = northSide
                        ? laneRect.bottom() - kOverviewLayoutMetrics.accessGateInset
                        : laneRect.top() + kOverviewLayoutMetrics.accessGateInset;
                    m_overviewScene->addLine(
                        laneRect.left() + 8.0, gateY,
                        laneRect.right() - 8.0, gateY,
                        QPen(accessAccent, 3.0));
                    const qreal arrowX = laneRect.center().x();
                    const qreal arrowTipY = entrance
                        ? (northSide ? laneRect.bottom() - 18.0 : laneRect.top() + 18.0)
                        : (northSide ? laneRect.top() + 4.0 : laneRect.bottom() - 4.0);
                    const qreal arrowTailY = entrance
                        ? (northSide ? laneRect.top() + 8.0 : gateY + 8.0)
                        : (northSide ? gateY - 8.0 : laneRect.top() + 18.0);
                    const qreal travelDirection = entrance
                        ? (northSide ? 1.0 : -1.0)
                        : (northSide ? -1.0 : 1.0);
                    const qreal arrowBaseY = arrowTipY
                        - (travelDirection * 10.0);
                    m_overviewScene->addLine(
                        arrowX, arrowTailY, arrowX, arrowBaseY, arrowPen);
                    const QPolygonF arrowHead{
                        QPointF(arrowX, arrowTipY),
                        QPointF(arrowX - 6.0, arrowBaseY),
                        QPointF(arrowX + 6.0, arrowBaseY)};
                    auto *arrowHeadItem = m_overviewScene->addPolygon(
                        arrowHead, Qt::NoPen, QBrush(accessAccent));
                    arrowHeadItem->setToolTip(arrowToolTip);
                } else {
                    const qreal arrowY = laneRect.center().y();
                    const qreal gateX = westSide
                        ? laneRect.right() - kOverviewLayoutMetrics.accessGateInset
                        : laneRect.left() + kOverviewLayoutMetrics.accessGateInset;
                    m_overviewScene->addLine(
                        gateX, laneRect.top() + 8.0,
                        gateX, laneRect.bottom() - 8.0,
                        QPen(accessAccent, 3.0));
                    const qreal arrowTipX = entrance
                        ? (westSide ? laneRect.right() - 18.0 : laneRect.left() + 18.0)
                        : (westSide ? laneRect.left() + 4.0 : laneRect.right() - 4.0);
                    const qreal arrowTailX = entrance
                        ? (westSide ? laneRect.left() + 8.0 : gateX - 8.0)
                        : (westSide ? gateX + 8.0 : laneRect.left() + 18.0);
                    const qreal travelDirection = entrance
                        ? (westSide ? 1.0 : -1.0)
                        : (westSide ? -1.0 : 1.0);
                    const qreal arrowBaseX = arrowTipX
                        - (travelDirection * 10.0);
                    m_overviewScene->addLine(
                        arrowTailX, arrowY, arrowBaseX, arrowY, arrowPen);
                    const QPolygonF arrowHead{
                        QPointF(arrowTipX, arrowY),
                        QPointF(arrowBaseX, arrowY - 6.0),
                        QPointF(arrowBaseX, arrowY + 6.0)};
                    auto *arrowHeadItem = m_overviewScene->addPolygon(
                        arrowHead, Qt::NoPen, QBrush(accessAccent));
                    arrowHeadItem->setToolTip(arrowToolTip);
                }
                continue;
            }

            const bool sideChannel = channel == QStringLiteral("CH2")
                || channel == QStringLiteral("CH4");
            const QColor visibleChannelAccent = live
                ? liveParkingChannelColor
                : previewParkingChannelColor;
            m_overviewScene->addRect(
                channelRect,
                QPen(visibleChannelAccent, 1.4),
                QBrush(live
                           ? QColor(QStringLiteral("#20272c"))
                           : QColor(QStringLiteral("#292f34"))));

            addText(channel, channelRect.topLeft() + QPointF(6, 4), 7,
                    live ? QColor(QStringLiteral("#f7fbff"))
                         : QColor(QStringLiteral("#929da3")), true);

            const QList<const ParkingZoneLayout *> zones = zonesByChannel.value(channel);
            const int visibleSlots = live
                ? qMin(4, static_cast<int>(zones.size()))
                : cameraSpec.channels.value(channel).slotCount;
            const qreal slotGap = 3.0;
            const qreal slotWidth = sideChannel
                ? channelRect.width() - 12.0
                : (visibleSlots > 0
                       ? (channelRect.width() - 12.0
                          - ((visibleSlots - 1) * slotGap)) / visibleSlots
                       : 0.0);
            const qreal slotStart = 19.0;
            const qreal slotHeight = sideChannel && visibleSlots > 0
                ? (channelRect.height() - slotStart - 8.0
                   - ((visibleSlots - 1) * slotGap)) / visibleSlots
                : channelRect.height() - slotStart - 8.0;
            for (int slotIndex = 0; slotIndex < visibleSlots; ++slotIndex) {
                const QRectF slotRect(
                    channelRect.x() + 6.0
                        + (sideChannel ? 0.0
                                       : slotIndex * (slotWidth + slotGap)),
                    channelRect.y() + slotStart
                        + (sideChannel ? slotIndex * (slotHeight + slotGap) : 0.0),
                    slotWidth, slotHeight);
                if (!live) {
                    m_overviewScene->addRect(
                        slotRect, QPen(QColor(QStringLiteral("#485158")), 1.0),
                        QBrush(QColor(QStringLiteral("#2b3237"))));
                    continue;
                }
                const ParkingZoneLayout *zone = zones.at(slotIndex);
                auto *slot = m_overviewScene->addRect(
                    slotRect, QPen(slotBorderColor(*zone), 1.4),
                    QBrush(QColor(QStringLiteral("#45515a"))));
                slot->setToolTip(QStringLiteral("Camera 1 | %1 | %2")
                                     .arg(channel, zone->zoneId));
                m_overviewSlotItems.insert(zone->zoneId, slot);
            }
        }
    }

    const OverviewLayoutMetrics &metrics = kOverviewLayoutMetrics;
    const QRectF facilityRect(
        metrics.outerMargin,
        metrics.outerMargin + metrics.sceneHeaderHeight,
        (metrics.cardSize * metrics.gridColumns)
            + (metrics.cardGap * (metrics.gridColumns - 1)),
        (metrics.cardSize * metrics.gridRows)
            + (metrics.cardGap * (metrics.gridRows - 1)));
    const QPen separatorPen(QColor(QStringLiteral("#46535d")), 1.0, Qt::DashLine);
    for (int column = 1; column < metrics.gridColumns; ++column) {
        const qreal x = facilityRect.left() + (column * metrics.cardSize)
            + ((column - 0.5) * metrics.cardGap);
        m_overviewScene->addLine(x, facilityRect.top(), x, facilityRect.bottom(), separatorPen);
    }
    for (int row = 1; row < metrics.gridRows; ++row) {
        const qreal y = facilityRect.top() + (row * metrics.cardSize)
            + ((row - 0.5) * metrics.cardGap);
        m_overviewScene->addLine(facilityRect.left(), y, facilityRect.right(), y, separatorPen);
    }
    m_overviewScene->addRect(facilityRect,
                             QPen(QColor(QStringLiteral("#6d7b85")), 1.0),
                             QBrush(Qt::NoBrush));

    updateOverviewScene();
}

void ParkingMapPage::updateOverviewScene()
{
    for (auto it = m_overviewSlotItems.begin(); it != m_overviewSlotItems.end(); ++it) {
        const QString zoneId = it.key();
        QGraphicsRectItem *slot = it.value();
        const int zoneIndex = zoneIndexById(zoneId);
        if (!slot || zoneIndex < 0) continue;
        const ParkingZoneLayout &slotZone = m_zones.at(zoneIndex);
        bool known = false;
        const SlotVisualState visual = visualStateForZone(zoneId, &known);
        slot->setBrush(overviewSlotFillColor(known, visual, slotZone.enabled));
        slot->setPen(QPen(slotBorderColor(slotZone), 2.0));
    }
}

void ParkingMapPage::updateRecentEvents()
{
    if (!m_recentEventsTable) return;
    const QString zoneId = selectedZoneId();
    m_recentEventsTable->setRowCount(0);
    if (zoneId.isEmpty()) return;
    for (const MonitoringEvent &event : std::as_const(m_recentEvents)) {
        if (normalizeParkingSlotId(event.sourceId) != zoneId
            && normalizeParkingSlotId(event.evidenceSlotId) != zoneId) continue;
        const int row = m_recentEventsTable->rowCount();
        m_recentEventsTable->insertRow(row);
        const QString timeText = event.occurredAt.isValid()
            ? event.occurredAt.toLocalTime().toString(QStringLiteral("MM-dd HH:mm:ss"))
            : QStringLiteral("-");
        m_recentEventsTable->setItem(row, 0, new QTableWidgetItem(timeText));
        m_recentEventsTable->setItem(row, 1, new QTableWidgetItem(event.eventType));
        m_recentEventsTable->setItem(row, 2, new QTableWidgetItem(monitoringEventStatusText(event)));
        if (row >= 4) break;
    }
}

void ParkingMapPage::updateVehicleImage()
{
    if (!m_runtimeVehicleImageLabel) return;
    m_vehicleImageRequestId.clear();
    m_runtimeVehicleImageLabel->setPixmap(QPixmap());
    const ParkingZoneLayout *zone = selectedZone();
    if (!zone) {
        m_runtimeVehicleImageLabel->setText(QStringLiteral("Select a slot to view vehicle image"));
        return;
    }
    const QList<ParkingImageResource> images = m_lastState.slotImages.value(zone->zoneId);
    if (images.isEmpty()) {
        m_runtimeVehicleImageLabel->setText(QStringLiteral("No vehicle image"));
        return;
    }
    const ParkingImageResource *latest = &images.constFirst();
    for (const ParkingImageResource &image : images) {
        if (image.timestamp.isValid()
            && (!latest->timestamp.isValid() || image.timestamp > latest->timestamp)) {
            latest = &image;
        }
    }
    if (!m_imageLoader) {
        m_runtimeVehicleImageLabel->setText(QStringLiteral("Vehicle image available"));
        return;
    }
    m_runtimeVehicleImageLabel->setText(QStringLiteral("Loading vehicle image..."));
    m_vehicleImageRequestId = QStringLiteral("parking-map:%1:%2")
        .arg(zone->zoneId, QString::number(qHash(latest->url.toString())));
    m_imageLoader->load(m_vehicleImageRequestId, latest->url);
}

void ParkingMapPage::updateZoneTable()
{
    const QString selectedId = selectedZoneId();
    QSignalBlocker blocker(m_zoneTable);
    m_zoneTable->setRowCount(0);
    for (const ParkingZoneLayout &zone : m_zones) {
        const int row = m_zoneTable->rowCount();
        m_zoneTable->insertRow(row);
        SlotState state = SlotState::Vacant;
        const bool known = stateForZone(zone.zoneId, &state);
        const QStringList values = {
            zone.zoneId,
            zone.zoneType,
            zone.cameraChannel,
            displayIvaText(zone.ivaAreaId),
            zone.hallSensorId,
            displayStateText(known, state),
            zone.enabled ? QStringLiteral("Y") : QStringLiteral("N")
        };
        for (int column = 0; column < values.size(); ++column) {
            m_zoneTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        if (zone.zoneId == selectedId) {
            m_zoneTable->selectRow(row);
        }
    }
    updateOperationalSummary();
    applyZoneFilters();
    updateOverviewSearchResults();
}

void ParkingMapPage::scrollMapToOrigin()
{
    if (!m_mapView) return;
    if (QScrollBar *horizontal = m_mapView->horizontalScrollBar()) {
        horizontal->setValue(horizontal->minimum());
    }
    if (QScrollBar *vertical = m_mapView->verticalScrollBar()) {
        vertical->setValue(vertical->minimum());
    }
}

void ParkingMapPage::updateGeometrySliderLabels()
{
    if (m_widthSpin && m_widthSlider
        && m_widthSpin->value() != m_widthSlider->value()) {
        QSignalBlocker blocker(m_widthSpin);
        m_widthSpin->setValue(m_widthSlider->value());
    }
    if (m_heightSpin && m_heightSlider
        && m_heightSpin->value() != m_heightSlider->value()) {
        QSignalBlocker blocker(m_heightSpin);
        m_heightSpin->setValue(m_heightSlider->value());
    }
    if (m_widthValueLabel && m_widthSlider) {
        m_widthValueLabel->setText(QStringLiteral("%1 px").arg(m_widthSlider->value()));
    }
    if (m_heightValueLabel && m_heightSlider) {
        m_heightValueLabel->setText(QStringLiteral("%1 px").arg(m_heightSlider->value()));
    }
    if (m_rotationValueLabel && m_rotationSlider) {
        m_rotationValueLabel->setText(QStringLiteral("%1 deg").arg(m_rotationSlider->value()));
    }
}

void ParkingMapPage::syncZonesFromItems()
{
    for (ParkingZoneLayout &zone : m_zones) {
        if (QGraphicsRectItem *item = m_zoneItems.value(zone.zoneId)) {
            zone.rect = QRectF(item->pos().x(), item->pos().y(), item->rect().width(), item->rect().height());
            zone.rotation = item->rotation();
            const QString detectedChannel = channelForScenePoint(item->sceneBoundingRect().center());
            if (!detectedChannel.isEmpty()) {
                zone.cameraChannel = detectedChannel;
            }
            applyZoneTypeRules(&zone);
        }
    }
}

void ParkingMapPage::syncItemsEditable()
{
    for (QGraphicsRectItem *item : m_zoneItems) {
        if (auto *zoneItem = dynamic_cast<ParkingZoneGraphicsItem *>(item)) {
            zoneItem->setEditingEnabled(m_editMode);
        } else {
            item->setFlag(QGraphicsItem::ItemIsMovable, m_editMode);
            item->setCursor(m_editMode ? Qt::SizeAllCursor : Qt::ArrowCursor);
        }
    }
}

void ParkingMapPage::markLayoutDirty(const QString &detail)
{
    setLayoutDirty(true, detail);
}

void ParkingMapPage::setLayoutDirty(bool dirty, const QString &status)
{
    const bool changed = m_layoutDirty != dirty;
    m_layoutDirty = dirty;

    if (m_layoutStatusLabel) {
        if (dirty) {
            m_layoutStatusLabel->setText(
                status.isEmpty()
                    ? QStringLiteral("Unsaved changes")
                    : QStringLiteral("Unsaved changes | %1").arg(status));
        } else if (!status.isEmpty()) {
            m_layoutStatusLabel->setText(status);
        }
    }

    if (changed) emit layoutDirtyChanged(dirty);
}
