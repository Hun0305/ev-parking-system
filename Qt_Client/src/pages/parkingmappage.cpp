#include "parkingmappage.h"

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
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
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
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QtMath>
#include <QVariant>
#include <QVBoxLayout>

#include <functional>

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
        return QColor(QStringLiteral("#2a3035"));
    }
    if (visual.occupancy == SlotOccupancy::Unknown) {
        return QColor(QStringLiteral("#343b41"));
    }
    switch (visual.vehicleClass) {
    case VehicleClass::Electric: return QColor(QStringLiteral("#174a66"));
    case VehicleClass::General: return QColor(QStringLiteral("#46535f"));
    case VehicleClass::Unknown: return QColor(QStringLiteral("#39434b"));
    }
    return QColor(QStringLiteral("#343b41"));
}

QString compactVehicleText(bool known, const SlotVisualState &visual)
{
    if (!known) return QStringLiteral("WAITING");
    if (visual.occupancy == SlotOccupancy::Vacant) return QStringLiteral("VACANT");
    if (visual.occupancy == SlotOccupancy::Unknown) return QStringLiteral("UNKNOWN");
    return vehicleClassText(visual.vehicleClass);
}

QColor alarmColor(SlotAlarmKind alarm)
{
    return alarm == SlotAlarmKind::SensorError
        ? QColor(QStringLiteral("#b388ff"))
        : QColor(QStringLiteral("#ff1744"));
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
    if (zone.zoneType == QStringLiteral("EV")) {
        return displayIvaText(zone.ivaAreaId);
    }
    return zone.cameraChannel.isEmpty() ? QStringLiteral("CH-") : zone.cameraChannel;
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

QString maskedPlateText(const QString &rawPlate)
{
    const QString plate = rawPlate.trimmed();
    const QString normalized = plate.toUpper();
    if (plate.isEmpty() || plate == QStringLiteral("-")
        || normalized == QStringLiteral("UNKNOWN")
        || normalized == QStringLiteral("N/A")) {
        return QStringLiteral("-");
    }
    if (plate.size() <= 4) {
        return QString(plate.size(), QLatin1Char('*'));
    }
    return plate.left(2)
        + QString(plate.size() - 4, QLatin1Char('*'))
        + plate.right(2);
}

QString runtimeDataStatusStyle(bool available)
{
    return QStringLiteral(
        "QLabel { color: %1; font-weight: 800; }")
        .arg(available ? QStringLiteral("#1b5e20") : QStringLiteral("#607d8b"));
}

QString runtimeAlarmStateStyle(bool known, SlotAlarmKind alarm, bool acknowledged)
{
    QString color = QStringLiteral("#607d8b");
    if (known && alarm != SlotAlarmKind::None) {
        color = acknowledged ? QStringLiteral("#546e7a") : QStringLiteral("#c62828");
    }
    return QStringLiteral("QLabel { color: %1; font-weight: 900; }").arg(color);
}

QRectF channelPanelRect(const QString &channel)
{
    if (channel == QStringLiteral("CH2")) return QRectF(470, 36, 420, 220);
    if (channel == QStringLiteral("CH3")) return QRectF(30, 304, 420, 220);
    if (channel == QStringLiteral("CH4")) return QRectF(470, 304, 420, 220);
    return QRectF(30, 36, 420, 220);
}

bool sameZoneLayout(const ParkingZoneLayout &left, const ParkingZoneLayout &right)
{
    return left.zoneId == right.zoneId
        && left.zoneType == right.zoneType
        && left.displayName == right.displayName
        && left.rect == right.rect
        && left.rotation == right.rotation
        && left.cameraChannel == right.cameraChannel
        && left.ivaAreaId == right.ivaAreaId
        && left.hallSensorId == right.hallSensorId
        && left.enabled == right.enabled;
}

constexpr int kUndoHistoryLimit = 30;
}

ParkingMapPage::ParkingMapPage(const QString &layoutPath, QWidget *parent)
    : QWidget(parent)
    , m_layoutPath(layoutPath)
{
    auto *pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    auto *mapGroup = new QGroupBox(QStringLiteral("4-Channel Parking Zone Map"), this);
    auto *mapLayout = new QVBoxLayout(mapGroup);
    mapLayout->setSpacing(8);

    auto *toolbarLayout = new QHBoxLayout;
    m_editToggleButton = new QPushButton(QStringLiteral("Edit layout"), mapGroup);
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
    auto *saveButton = new QPushButton(QStringLiteral("Save layout"), mapGroup);
    auto *reloadButton = new QPushButton(QStringLiteral("Reload"), mapGroup);
    auto *resetButton = new QPushButton(QStringLiteral("Reset default"), mapGroup);
    m_layoutStatusLabel = new QLabel(QStringLiteral("-"), mapGroup);
    m_layoutStatusLabel->setStyleSheet(QStringLiteral("color: #607d8b; font-size: 11px;"));
    toolbarLayout->addWidget(m_editToggleButton);
    toolbarLayout->addWidget(m_undoButton);
    toolbarLayout->addWidget(m_editChannelNamesButton);
    toolbarLayout->addWidget(addGeneralButton);
    toolbarLayout->addWidget(addEvButton);
    toolbarLayout->addWidget(saveButton);
    toolbarLayout->addWidget(reloadButton);
    toolbarLayout->addWidget(resetButton);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_layoutStatusLabel);
    mapLayout->addLayout(toolbarLayout);

    m_scene = new QGraphicsScene(0, 0, 920, 560, this);
    m_scene->setBackgroundBrush(QColor(QStringLiteral("#1b1f23")));
    m_mapView = new QGraphicsView(m_scene, mapGroup);
    m_mapView->setMinimumSize(680, 520);
    m_mapView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_mapView->setResizeAnchor(QGraphicsView::NoAnchor);
    m_mapView->setTransformationAnchor(QGraphicsView::NoAnchor);
    m_mapView->setRenderHint(QPainter::Antialiasing, true);
    m_mapView->setDragMode(QGraphicsView::RubberBandDrag);
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

    auto *legendLayout = new QHBoxLayout;
    legendLayout->setSpacing(6);
    legendLayout->addWidget(createLegendItem(QStringLiteral("VACANT"),
                                              QColor(QStringLiteral("#2a3035")),
                                              QColor(QStringLiteral("#68727a"))));
    legendLayout->addWidget(createLegendItem(QStringLiteral("EV CAR"),
                                              QColor(QStringLiteral("#174a66")),
                                              QColor(QStringLiteral("#38bdf8"))));
    legendLayout->addWidget(createLegendItem(QStringLiteral("GENERAL CAR"),
                                              QColor(QStringLiteral("#46535f")),
                                              QColor(QStringLiteral("#aebbc5"))));
    legendLayout->addWidget(createLegendItem(QStringLiteral("EV ZONE"),
                                              QColor(QStringLiteral("#242a2f")),
                                              QColor(QStringLiteral("#2d9cff"))));
    legendLayout->addWidget(createLegendItem(QStringLiteral("GENERAL ZONE"),
                                              QColor(QStringLiteral("#242a2f")),
                                              QColor(QStringLiteral("#ffd447"))));
    legendLayout->addWidget(createLegendItem(QStringLiteral("ACTIVE WARNING"),
                                              QColor(QStringLiteral("#ff1744")),
                                              QColor(QStringLiteral("#ff8aa1")), true));
    legendLayout->addStretch();
    mapLayout->addLayout(legendLayout);
    pageLayout->addWidget(mapGroup, 3);

    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto *mappingGroup = new QGroupBox(QStringLiteral("Channel / IVA / Zone Mapping"), rightPanel);
    auto *mappingLayout = new QVBoxLayout(mappingGroup);
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
    rightLayout->addWidget(mappingGroup, 2);

    auto *runtimeGroup = new QGroupBox(QStringLiteral("Runtime Status"), rightPanel);
    runtimeGroup->setObjectName(QStringLiteral("runtimeStatusGroup"));
    auto *runtimeLayout = new QVBoxLayout(runtimeGroup);
    runtimeLayout->setSpacing(8);
    auto *selectedHeader = new QWidget(runtimeGroup);
    auto *selectedHeaderLayout = new QVBoxLayout(selectedHeader);
    selectedHeaderLayout->setContentsMargins(0, 0, 0, 8);
    selectedHeaderLayout->setSpacing(4);
    auto *selectedTitleRow = new QHBoxLayout;
    selectedTitleRow->setContentsMargins(0, 0, 0, 0);
    m_selectedTitleLabel = new QLabel(QStringLiteral("No slot selected"), selectedHeader);
    m_selectedTitleLabel->setStyleSheet(QStringLiteral("color: #202124; font-size: 15px; font-weight: 900;"));
    m_selectedStateLabel = new QLabel(QStringLiteral("WAITING"), selectedHeader);
    m_selectedStateLabel->setObjectName(QStringLiteral("selectedSlotStateLabel"));
    m_selectedStateLabel->setAlignment(Qt::AlignCenter);
    m_selectedStateLabel->setStyleSheet(statePillStyle(false, SlotState::Vacant));
    selectedTitleRow->addWidget(m_selectedTitleLabel, 1);
    selectedTitleRow->addWidget(m_selectedStateLabel);
    m_selectedMetaLabel = new QLabel(QStringLiteral("No active selection"), selectedHeader);
    m_selectedMetaLabel->setStyleSheet(QStringLiteral("color: #54636d; font-size: 11px;"));
    m_selectedMetaLabel->setWordWrap(true);
    selectedHeaderLayout->addLayout(selectedTitleRow);
    selectedHeaderLayout->addWidget(m_selectedMetaLabel);
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
    m_runtimeVehicleLabel = makeRuntimeValue(QStringLiteral("runtimeVehicleLabel"));
    m_runtimePlateLabel = makeRuntimeValue(QStringLiteral("runtimePlateLabel"));
    m_runtimeOccupiedTimeLabel = makeRuntimeValue(QStringLiteral("runtimeOccupiedTimeLabel"));
    m_runtimeAlarmLabel = makeRuntimeValue(QStringLiteral("runtimeAlarmLabel"));
    m_runtimeAlarmStateLabel = makeRuntimeValue(QStringLiteral("runtimeAlarmStateLabel"));
    auto addRuntimeField = [runtimeGrid, runtimeGroup](int row, int column,
                                                       const QString &title, QLabel *value) {
        auto *titleLabel = new QLabel(title, runtimeGroup);
        titleLabel->setStyleSheet(QStringLiteral("color: #607d8b; font-size: 11px;"));
        runtimeGrid->addWidget(titleLabel, row, column * 2);
        runtimeGrid->addWidget(value, row, (column * 2) + 1);
    };
    addRuntimeField(0, 0, QStringLiteral("Runtime data"), m_runtimeDataStatusLabel);
    addRuntimeField(0, 1, QStringLiteral("Vehicle"), m_runtimeVehicleLabel);
    addRuntimeField(1, 0, QStringLiteral("Plate"), m_runtimePlateLabel);
    addRuntimeField(1, 1, QStringLiteral("Occupied"), m_runtimeOccupiedTimeLabel);
    addRuntimeField(2, 0, QStringLiteral("Alarm"), m_runtimeAlarmLabel);
    addRuntimeField(2, 1, QStringLiteral("Alarm state"), m_runtimeAlarmStateLabel);
    runtimeGrid->setColumnStretch(1, 1);
    runtimeGrid->setColumnStretch(3, 1);
    runtimeLayout->addLayout(runtimeGrid);
    rightLayout->insertWidget(0, runtimeGroup);

    auto *editorGroup = new QGroupBox(QStringLiteral("Layout Editor"), rightPanel);
    auto *editorLayout = new QFormLayout(editorGroup);
    m_zoneIdEdit = new QLineEdit(editorGroup);
    m_displayNameEdit = new QLineEdit(editorGroup);
    m_zoneTypeCombo = new QComboBox(editorGroup);
    m_zoneTypeCombo->addItems({QStringLiteral("GENERAL"), QStringLiteral("EV")});
    m_cameraChannelCombo = new QComboBox(editorGroup);
    m_cameraChannelCombo->addItems({QStringLiteral("CH1"), QStringLiteral("CH2"), QStringLiteral("CH3"), QStringLiteral("CH4")});
    m_ivaAreaCombo = new QComboBox(editorGroup);
    m_ivaAreaCombo->addItems({
        QStringLiteral("N/A"),
        QStringLiteral("IVA1"),
        QStringLiteral("IVA2"),
        QStringLiteral("IVA3"),
        QStringLiteral("IVA4")
    });
    m_hallSensorEdit = new QLineEdit(editorGroup);
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
        auto *createdLabel = new QLabel(container);
        createdLabel->setMinimumWidth(42);
        createdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        createdLabel->setStyleSheet(QStringLiteral("color: #455a64; font-weight: 700;"));
        layout->addWidget(createdSlider, 1);
        layout->addWidget(createdLabel);
        *slider = createdSlider;
        *valueLabel = createdLabel;
        return container;
    };
    QWidget *widthControl = makeSliderControl(&m_widthSlider, &m_widthValueLabel, 16, 420, 4);
    QWidget *heightControl = makeSliderControl(&m_heightSlider, &m_heightValueLabel, 16, 220, 4);
    QWidget *rotationControl = makeSliderControl(&m_rotationSlider, &m_rotationValueLabel, -180, 180, 5);
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
    pageLayout->addWidget(rightPanel, 2);

    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &ParkingMapPage::handleSceneSelectionChanged);
    connect(m_zoneTable, &QTableWidget::cellClicked,
            this, &ParkingMapPage::handleZoneTableClicked);
    connect(m_editToggleButton, &QPushButton::toggled,
            this, &ParkingMapPage::setEditMode);
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

    loadLayout();
    rebuildScene();
    updateZoneTable();
    updateEditorFromSelection();
    scrollMapToOrigin();
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
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
    QTimer::singleShot(0, this, &ParkingMapPage::scrollMapToOrigin);
}

void ParkingMapPage::render(const ParkingViewState &state)
{
    m_lastState = state;
    updateAllZoneVisuals();
    updateAlarmAnimationState();
    updateZoneTable();
    updateEditorFromSelection();
}

ParkingMapPage::LayoutSnapshot ParkingMapPage::captureLayoutSnapshot() const
{
    LayoutSnapshot snapshot;
    snapshot.zones = m_zones;
    snapshot.channelDisplayNames = m_channelDisplayNames;
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
    m_hasPendingDragSnapshot = false;
    m_editorSliderGestureActive = false;
    m_editorSliderSnapshotRecorded = false;

    rebuildScene();
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
    zone.cameraChannel = m_cameraChannelCombo ? m_cameraChannelCombo->currentText() : QStringLiteral("CH1");
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
    if (!saveParkingZoneLayout(m_layoutPath, m_zones, m_channelDisplayNames, &error)) {
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
    updateZoneTable();
    updateEditorFromSelection();
    scrollMapToOrigin();
}

void ParkingMapPage::resetDefaultLayout()
{
    syncZonesFromItems();
    const QList<ParkingZoneLayout> defaultZones = defaultParkingZoneLayout();
    if (m_channelDisplayNames.isEmpty() && m_zones.size() == defaultZones.size()) {
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
    rebuildScene();
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
                       m_widthSlider->value(), m_heightSlider->value());
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
    m_editMode = enabled;
    m_editToggleButton->setText(enabled ? QStringLiteral("Finish edit") : QStringLiteral("Edit layout"));
    m_editChannelNamesButton->setEnabled(enabled);
    syncItemsEditable();
    updateEditorFromSelection();
    if (!m_layoutDirty) {
        m_layoutStatusLabel->setText(enabled ? QStringLiteral("Edit mode") : QStringLiteral("View mode"));
    }
}

void ParkingMapPage::loadLayout()
{
    QString error;
    QList<ParkingZoneLayout> loadedZones;
    ParkingChannelDisplayNames loadedChannelDisplayNames;
    if (QFile::exists(m_layoutPath)
        && loadParkingZoneLayout(m_layoutPath, &loadedZones,
                                 &loadedChannelDisplayNames, &error)) {
        m_zones = loadedZones;
        m_channelDisplayNames = loadedChannelDisplayNames;
        clearUndoHistory();
        setLayoutDirty(false, QStringLiteral("Loaded local layout"));
        return;
    }

    m_zones = defaultParkingZoneLayout();
    m_channelDisplayNames.clear();
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

QString ParkingMapPage::channelPanelTitle(const QString &channel) const
{
    const QString displayName = m_channelDisplayNames.value(channel).trimmed();
    if (displayName.isEmpty()) {
        return QStringLiteral("%1 | IVA1-IVA4").arg(channel);
    }
    return QStringLiteral("%1 | %2 | IVA1-IVA4").arg(channel, displayName);
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

    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 4; ++column) {
            const QRectF candidate(panel.x() + 34 + (column * 96),
                                   panel.y() + 60 + (row * 94),
                                   84, 58);
            if (isFree(candidate)) return candidate;
        }
    }

    for (int stagingIndex = 0; stagingIndex < 1000; ++stagingIndex) {
        const int column = stagingIndex % 9;
        const int row = stagingIndex / 9;
        const QRectF candidate(40 + (column * 96),
                               590 + (row * 72),
                               84, 58);
        if (isFree(candidate)) return candidate;
    }

    return QRectF(40, 590, 84, 58);
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
    if (!detectedChannel.isEmpty()) {
        zone.cameraChannel = detectedChannel;
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
        || (item->rect().width() == 84.0
            && item->rect().height() == 58.0
            && item->rotation() == 0.0);
    if (previousZone.rect.width() == 84.0
        && previousZone.rect.height() == 58.0
        && previousZone.rotation == 0.0
        && itemAlreadyDefault) {
        return;
    }

    pushCurrentLayoutToUndoHistory();
    ParkingZoneLayout zone = previousZone;
    const QPointF position = item ? item->pos() : zone.rect.topLeft();
    zone.rect = QRectF(position.x(), position.y(), 84, 58);
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

QString ParkingMapPage::nextIvaAreaForEv(const QString &channel, const QString &excludeZoneId) const
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
        if (zone.zoneType == QStringLiteral("EV")
            && zone.cameraChannel == channel
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
    if (zone->cameraChannel.isEmpty()) {
        zone->cameraChannel = QStringLiteral("CH1");
    }
    if (zone->zoneType != QStringLiteral("EV")) {
        zone->zoneType = QStringLiteral("GENERAL");
        zone->ivaAreaId.clear();
        return;
    }

    QString ivaAreaId = preferredIva.trimmed().toUpper();
    if (!isIvaAreaId(ivaAreaId)) {
        ivaAreaId = zone->ivaAreaId.trimmed().toUpper();
    }
    if (!isIvaAreaId(ivaAreaId)
        || ivaUsedInChannel(zone->cameraChannel, ivaAreaId, zone->zoneId)) {
        ivaAreaId = nextIvaAreaForEv(zone->cameraChannel, zone->zoneId);
    }
    zone->ivaAreaId = ivaAreaId;
}

void ParkingMapPage::rebuildScene()
{
    m_rebuildingScene = true;
    const QSignalBlocker sceneBlocker(m_scene);
    m_zoneItems.clear();
    m_zoneAccentBars.clear();
    m_zoneLabels.clear();
    m_zoneStateLabels.clear();
    m_zoneMetaLabels.clear();
    m_zoneAlarmHalos.clear();
    m_zoneAlarmBeacons.clear();
    m_zoneAlarmLabels.clear();
    m_scene->clear();
    qreal sceneHeight = 560.0;
    for (const ParkingZoneLayout &zone : m_zones) {
        sceneHeight = qMax(sceneHeight, zone.rect.bottom() + 28.0);
    }
    m_scene->setSceneRect(0, 0, 920, sceneHeight);
    m_scene->addRect(m_scene->sceneRect(), QPen(QColor(QStringLiteral("#101418"))), QBrush(QColor(QStringLiteral("#1b1f23"))));

    if (sceneHeight > 560.0) {
        const QRectF stagingArea(30, 552, 860, sceneHeight - 566.0);
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
        stagingTitle->setPos(42, 558);
    }

    for (const QString &channel : {QStringLiteral("CH1"), QStringLiteral("CH2"),
                                   QStringLiteral("CH3"), QStringLiteral("CH4")}) {
        const QRectF panel = channelPanelRect(channel);
        m_scene->addRect(panel, QPen(QColor(QStringLiteral("#5f6c75")), 2),
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
        m_scene->addRect(QRectF(panel.x() + 18, panel.y() + 124, panel.width() - 36, 22),
                         QPen(QColor(QStringLiteral("#56626b")), 1, Qt::DashLine),
                         QBrush(QColor(QStringLiteral("#20262b"))));
        auto *lane = m_scene->addSimpleText(QStringLiteral("drive aisle"));
        QFont laneFont;
        laneFont.setPointSize(7);
        lane->setFont(laneFont);
        lane->setBrush(QColor(QStringLiteral("#8a969f")));
        lane->setPos(panel.x() + panel.width() - 92, panel.y() + 127);
    }

    for (const ParkingZoneLayout &zone : m_zones) {
        auto *item = new ParkingZoneGraphicsItem(zone.zoneId, QRectF(0, 0, zone.rect.width(), zone.rect.height()));
        m_scene->addItem(item);
        item->setData(0, zone.zoneId);
        item->setPos(zone.rect.topLeft());
        item->setTransformOriginPoint(item->rect().center());
        item->setRotation(zone.rotation);
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setFlag(QGraphicsItem::ItemIsMovable, true);
        item->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        item->setAcceptHoverEvents(true);
        item->setCursor(Qt::SizeAllCursor);
        auto *accent = new QGraphicsRectItem(item);
        accent->setRect(QRectF(1, 1, qMax(1.0, zone.rect.width() - 2.0), 4));
        accent->setPen(Qt::NoPen);
        accent->setZValue(1);
        auto *label = new QGraphicsSimpleTextItem(item);
        QFont labelFont;
        labelFont.setPointSize(7);
        labelFont.setBold(true);
        label->setFont(labelFont);
        label->setPos(6, 10);
        label->setZValue(2);
        auto *stateLabel = new QGraphicsSimpleTextItem(item);
        QFont stateFont;
        stateFont.setPointSize(6);
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
        m_zoneLabels.insert(zone.zoneId, label);
        m_zoneStateLabels.insert(zone.zoneId, stateLabel);
        m_zoneMetaLabels.insert(zone.zoneId, metaLabel);
        m_zoneAlarmHalos.insert(zone.zoneId, alarmHalo);
        m_zoneAlarmBeacons.insert(zone.zoneId, alarmBeacon);
        m_zoneAlarmLabels.insert(zone.zoneId, alarmLabel);
        updateZoneVisual(zone.zoneId);
    }
    m_rebuildingScene = false;
    updateAlarmAnimationState();
}

void ParkingMapPage::updateZoneVisual(const QString &zoneId)
{
    const int index = zoneIndexById(zoneId);
    if (index < 0) return;
    const ParkingZoneLayout zone = m_zones.at(index);
    QGraphicsRectItem *item = m_zoneItems.value(zoneId);
    QGraphicsRectItem *accent = m_zoneAccentBars.value(zoneId);
    QGraphicsSimpleTextItem *label = m_zoneLabels.value(zoneId);
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
    stateLabel->setText(compactVehicleText(visualKnown, visual));
    stateLabel->setBrush(slotTextColor(zone.enabled));
    stateLabel->setPos(6, qMax(22.0, item->rect().height() - 18.0));
    metaLabel->setText(slotMetaText(zone));
    metaLabel->setBrush(zone.enabled ? borderColor.lighter(125) : QColor(QStringLiteral("#9aa5ad")));
    const QRectF metaBounds = metaLabel->boundingRect();
    metaLabel->setPos(qMax(6.0, item->rect().width() - metaBounds.width() - 6.0),
                      visual.alarm == SlotAlarmKind::None ? 10 : 25);

    if (alarmHalo && alarmBeacon && alarmLabel) {
        const bool hasAlarm = visual.alarm != SlotAlarmKind::None;
        const QColor color = alarmColor(visual.alarm);
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
    if (!zone) {
        if (m_selectedTitleLabel) m_selectedTitleLabel->setText(QStringLiteral("No slot selected"));
        if (m_selectedStateLabel) {
            m_selectedStateLabel->setText(QStringLiteral("WAITING"));
            m_selectedStateLabel->setStyleSheet(statePillStyle(false, SlotState::Vacant));
        }
        if (m_selectedMetaLabel) m_selectedMetaLabel->setText(QStringLiteral("No active selection"));
        if (m_runtimeDataStatusLabel) {
            m_runtimeDataStatusLabel->setText(QStringLiteral("WAITING DATA"));
            m_runtimeDataStatusLabel->setStyleSheet(runtimeDataStatusStyle(false));
        }
        if (m_runtimeVehicleLabel) m_runtimeVehicleLabel->setText(QStringLiteral("-"));
        if (m_runtimePlateLabel) m_runtimePlateLabel->setText(QStringLiteral("-"));
        if (m_runtimeOccupiedTimeLabel) m_runtimeOccupiedTimeLabel->setText(QStringLiteral("-"));
        if (m_runtimeAlarmLabel) m_runtimeAlarmLabel->setText(QStringLiteral("-"));
        if (m_runtimeAlarmStateLabel) {
            m_runtimeAlarmStateLabel->setText(QStringLiteral("-"));
            m_runtimeAlarmStateLabel->setStyleSheet(
                runtimeAlarmStateStyle(false, SlotAlarmKind::None, false));
        }
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
    if (m_selectedMetaLabel) {
        m_selectedMetaLabel->setText(
            QStringLiteral("%1 | %2 | %3 | %4")
                .arg(zone->zoneType,
                     zone->cameraChannel,
                     displayIvaText(zone->ivaAreaId),
                     zone->hallSensorId.isEmpty() ? QStringLiteral("HALL-")
                                                  : zone->hallSensorId));
    }
    if (m_runtimeDataStatusLabel) {
        m_runtimeDataStatusLabel->setText(runtimeKnown ? QStringLiteral("AVAILABLE")
                                                       : QStringLiteral("WAITING DATA"));
        m_runtimeDataStatusLabel->setStyleSheet(runtimeDataStatusStyle(runtimeKnown));
    }

    if (!runtimeKnown) {
        if (m_runtimeVehicleLabel) m_runtimeVehicleLabel->setText(QStringLiteral("-"));
        if (m_runtimePlateLabel) m_runtimePlateLabel->setText(QStringLiteral("-"));
        if (m_runtimeOccupiedTimeLabel) m_runtimeOccupiedTimeLabel->setText(QStringLiteral("-"));
        if (m_runtimeAlarmLabel) m_runtimeAlarmLabel->setText(QStringLiteral("-"));
        if (m_runtimeAlarmStateLabel) {
            m_runtimeAlarmStateLabel->setText(QStringLiteral("-"));
            m_runtimeAlarmStateLabel->setStyleSheet(
                runtimeAlarmStateStyle(false, SlotAlarmKind::None, false));
        }
        return;
    }

    if (m_runtimeVehicleLabel) {
        m_runtimeVehicleLabel->setText(compactVehicleText(true, selectedVisual));
    }

    const bool occupied = selectedVisual.occupancy == SlotOccupancy::Occupied;
    const bool evRuntime = zone->zoneType == QStringLiteral("EV")
        && m_lastState.evSlots.contains(zone->zoneId);
    if (evRuntime) {
        const EvSlotInfo slot = m_lastState.evSlots.value(zone->zoneId);
        if (m_runtimePlateLabel) {
            m_runtimePlateLabel->setText(occupied ? maskedPlateText(slot.plateNumber)
                                                  : QStringLiteral("-"));
        }
        if (m_runtimeOccupiedTimeLabel) {
            const QString occupiedTime = slot.occupiedTime.trimmed();
            m_runtimeOccupiedTimeLabel->setText(
                occupied && !occupiedTime.isEmpty() && occupiedTime != QStringLiteral("-")
                    ? occupiedTime
                    : QStringLiteral("-"));
        }
    } else {
        if (m_runtimePlateLabel) m_runtimePlateLabel->setText(QStringLiteral("N/A"));
        if (m_runtimeOccupiedTimeLabel) m_runtimeOccupiedTimeLabel->setText(QStringLiteral("N/A"));
    }

    if (m_runtimeAlarmLabel) {
        m_runtimeAlarmLabel->setText(slotAlarmText(selectedVisual.alarm));
    }
    if (m_runtimeAlarmStateLabel) {
        const QString alarmState = selectedVisual.alarm == SlotAlarmKind::None
            ? QStringLiteral("NONE")
            : (selectedVisual.alarmAcknowledged ? QStringLiteral("ACK")
                                                : QStringLiteral("ACTIVE"));
        m_runtimeAlarmStateLabel->setText(alarmState);
        m_runtimeAlarmStateLabel->setStyleSheet(
            runtimeAlarmStateStyle(true, selectedVisual.alarm,
                                   selectedVisual.alarmAcknowledged));
    }
}

void ParkingMapPage::updateEditorFromSelection()
{
    updateRuntimeStatusFromSelection();
    m_updatingEditor = true;
    const ParkingZoneLayout *zone = selectedZone();
    const bool hasSelection = zone != nullptr;
    const QList<QWidget *> editorWidgets = {
        m_zoneIdEdit, m_displayNameEdit, m_zoneTypeCombo, m_cameraChannelCombo,
        m_ivaAreaCombo, m_hallSensorEdit, m_enabledCheck, m_xSpin, m_ySpin,
        m_widthSlider, m_heightSlider, m_rotationSlider, m_deleteButton
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
    m_rotationSlider->setValue(qRound(zone->rotation));
    m_ivaAreaCombo->setEnabled(m_editMode && zone->zoneType == QStringLiteral("EV"));
    updateGeometrySliderLabels();
    m_updatingEditor = false;
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
        item->setFlag(QGraphicsItem::ItemIsMovable, true);
        item->setCursor(Qt::SizeAllCursor);
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

QWidget *ParkingMapPage::createLegendItem(const QString &label, const QColor &fill,
                                          const QColor &border, bool circular)
{
    auto *container = new QWidget(this);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *swatch = new QLabel(container);
    swatch->setFixedSize(14, 14);
    swatch->setStyleSheet(QStringLiteral("background: %1; border: 2px solid %2; border-radius: %3px;")
                              .arg(fill.name(), border.name())
                              .arg(circular ? 7 : 3));
    layout->addWidget(swatch);
    auto *text = new QLabel(label, container);
    text->setStyleSheet(QStringLiteral("color: #263238;"));
    layout->addWidget(text);
    return container;
}
