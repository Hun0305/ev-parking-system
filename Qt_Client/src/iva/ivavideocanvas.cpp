#include "ivavideocanvas.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QCursor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <cmath>

IvaVideoCanvas::IvaVideoCanvas(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setObjectName(QStringLiteral("ivaVideoCanvas"));
    setScene(m_scene);
    setBackgroundBrush(Qt::black);
    setFrameShape(QFrame::StyledPanel);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setMinimumSize(480, 300);
    m_frameItem = m_scene->addPixmap(QPixmap());
    m_frameItem->setZValue(0.0);
}

void IvaVideoCanvas::setChannel(int channel,
                                const QSize &coordinateResolution)
{
    m_channel = channel;
    m_coordinateResolution = coordinateResolution;
    m_selectedAreaIndex = -1;
    m_dragging = false;
    m_interactionMode = InteractionMode::None;
    m_editRectangle = {};
    m_frameCompatible = false;
    m_frameCompatibilityMessage = QStringLiteral("Waiting for the shared RTSP frame");
    m_frameItem->setPixmap(QPixmap());
    m_frameItem->setTransform(QTransform());
    if (m_draftItem) {
        m_scene->removeItem(m_draftItem);
        delete m_draftItem;
        m_draftItem = nullptr;
    }
    const QRectF sceneRectangle(QPointF(0, 0), QSizeF(coordinateResolution));
    m_scene->setSceneRect(sceneRectangle);
    rebuildOverlays();
    fitScene();
}

void IvaVideoCanvas::setFrame(const QImage &frame)
{
    bool compatible = !frame.isNull() && m_coordinateResolution.isValid();
    QString message;
    if (frame.isNull()) {
        message = QStringLiteral("Waiting for the shared RTSP frame");
    } else if (!m_coordinateResolution.isValid()) {
        compatible = false;
        message = QStringLiteral("Camera IVA coordinate resolution is unavailable");
    } else {
        const double frameRatio = static_cast<double>(frame.width()) / frame.height();
        const double coordinateRatio = static_cast<double>(m_coordinateResolution.width())
            / m_coordinateResolution.height();
        const double relativeDifference = std::abs(frameRatio - coordinateRatio)
            / coordinateRatio;
        if (relativeDifference > 0.02) {
            compatible = false;
            message = QStringLiteral(
                "RTSP aspect ratio %1:%2 does not match IVA coordinates %3:%4")
                          .arg(frame.width()).arg(frame.height())
                          .arg(m_coordinateResolution.width())
                          .arg(m_coordinateResolution.height());
        }
    }

    if (compatible) {
        m_frameItem->setPixmap(QPixmap::fromImage(frame));
        const qreal xScale = static_cast<qreal>(m_coordinateResolution.width())
            / frame.width();
        const qreal yScale = static_cast<qreal>(m_coordinateResolution.height())
            / frame.height();
        m_frameItem->setTransform(QTransform::fromScale(xScale, yScale));
        message = QStringLiteral("Shared RTSP frame %1x%2")
                      .arg(frame.width()).arg(frame.height());
    }
    if (compatible != m_frameCompatible || message != m_frameCompatibilityMessage) {
        m_frameCompatible = compatible;
        m_frameCompatibilityMessage = message;
        emit frameCompatibilityChanged(compatible, message);
    }
}

void IvaVideoCanvas::setAreas(const QList<IvaAreaDefinition> &areas)
{
    m_areas.clear();
    for (const IvaAreaDefinition &area : areas) {
        if (area.channel == m_channel) {
            m_areas.append(area);
        }
    }
    rebuildOverlays();
}

void IvaVideoCanvas::setSelectedAreaIndex(int areaIndex)
{
    m_selectedAreaIndex = areaIndex;
    updateOverlayStyles();
    m_editRectangle = {};
    m_editRectangle = selectedAreaRectangle();
    rebuildEditHandles();
}

void IvaVideoCanvas::setDrawMode(bool enabled)
{
    m_drawMode = enabled;
    m_dragging = false;
    m_interactionMode = InteractionMode::None;
    if (!enabled && m_draftItem) {
        m_scene->removeItem(m_draftItem);
        delete m_draftItem;
        m_draftItem = nullptr;
    }
    updateCursor();
}

void IvaVideoCanvas::setEditMode(bool enabled)
{
    m_editMode = enabled;
    if (!enabled && (m_interactionMode == InteractionMode::MoveRectangle
                     || m_interactionMode == InteractionMode::MoveVertex)) {
        m_dragging = false;
        m_interactionMode = InteractionMode::None;
    }
    rebuildEditHandles();
    updateCursor();
}

QList<QPointF> IvaVideoCanvas::rectangleCoordinates(const QRectF &rectangle)
{
    const QRectF normalized = rectangle.normalized();
    const double left = std::round(normalized.left());
    const double top = std::round(normalized.top());
    const double right = std::round(normalized.right());
    const double bottom = std::round(normalized.bottom());
    return {QPointF(left, top), QPointF(right, top),
            QPointF(right, bottom), QPointF(left, bottom)};
}

QRectF IvaVideoCanvas::normalizedFromSource(const QRectF &sourceRectangle,
                                            const QSize &sourceResolution)
{
    if (!sourceResolution.isValid()) return {};
    const QRectF bounds(QPointF(0.0, 0.0), QSizeF(sourceResolution));
    const QRectF bounded = sourceRectangle.normalized().intersected(bounds);
    if (bounded.isEmpty()) return {};
    return QRectF(bounded.x() / sourceResolution.width(),
                  bounded.y() / sourceResolution.height(),
                  bounded.width() / sourceResolution.width(),
                  bounded.height() / sourceResolution.height());
}

QRectF IvaVideoCanvas::sourceFromNormalized(
    const QRectF &normalizedRectangle, const QSize &sourceResolution)
{
    if (!sourceResolution.isValid()) return {};
    const QRectF bounded = normalizedRectangle.normalized().intersected(
        QRectF(0.0, 0.0, 1.0, 1.0));
    if (bounded.isEmpty()) return {};
    return QRectF(bounded.x() * sourceResolution.width(),
                  bounded.y() * sourceResolution.height(),
                  bounded.width() * sourceResolution.width(),
                  bounded.height() * sourceResolution.height());
}

void IvaVideoCanvas::setParkingRoiOverlays(
    const QRectF &savedNormalizedRectangle,
    const QRectF &selectedNormalizedRectangle)
{
    updateParkingRoiItem(m_savedParkingRoiItem, savedNormalizedRectangle,
                         QColor(QStringLiteral("#76ff03")), Qt::SolidLine, 2.5);
    updateParkingRoiItem(m_selectedParkingRoiItem, selectedNormalizedRectangle,
                         QColor(QStringLiteral("#ffca28")), Qt::DashLine, 3.0);
}

void IvaVideoCanvas::clearParkingRoiOverlays()
{
    QGraphicsRectItem **items[] = {
        &m_savedParkingRoiItem, &m_selectedParkingRoiItem};
    for (QGraphicsRectItem **item : items) {
        if (!*item) continue;
        m_scene->removeItem(*item);
        delete *item;
        *item = nullptr;
    }
}

void IvaVideoCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const QPointF scenePoint = boundedScenePoint(event->position().toPoint());
    if (m_editMode && m_selectedAreaIndex >= 0) {
        const int vertex = selectedVertexAt(scenePoint);
        if (vertex >= 0) {
            m_dragStart = scenePoint;
            m_dragStartRectangle = selectedAreaRectangle();
            m_dragVertex = vertex;
            m_interactionMode = InteractionMode::MoveVertex;
            m_dragging = true;
            updateCursorAt(scenePoint);
            event->accept();
            return;
        }
        const QRectF selectedRectangle = selectedAreaRectangle();
        if (!selectedRectangle.isEmpty() && selectedRectangle.contains(scenePoint)) {
            m_dragStart = scenePoint;
            m_dragStartRectangle = selectedRectangle;
            m_dragVertex = -1;
            m_interactionMode = InteractionMode::MoveRectangle;
            m_dragging = true;
            updateCursorAt(scenePoint);
            event->accept();
            return;
        }
    }

    QGraphicsItem *clicked = itemAt(event->position().toPoint());
    while (clicked && !clicked->data(0).isValid()) {
        clicked = clicked->parentItem();
    }
    if (clicked && clicked->data(0).isValid()) {
        emit areaSelected(clicked->data(0).toInt());
        updateCursorAt(scenePoint);
        event->accept();
        return;
    }
    if (!m_drawMode) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (!m_frameCompatible || !m_coordinateResolution.isValid()) {
        emit rectangleRejected(
            QStringLiteral("The selected area is outside the image."));
        event->accept();
        return;
    }

    m_dragStart = scenePoint;
    m_dragging = true;
    m_interactionMode = InteractionMode::DrawRectangle;
    updateCursorAt(scenePoint);
    if (!m_draftItem) {
        m_draftItem = m_scene->addRect(
            QRectF(m_dragStart, m_dragStart),
            QPen(QColor(QStringLiteral("#ffca28")), 4, Qt::DashLine),
            QBrush(QColor(255, 202, 40, 50)));
        m_draftItem->setZValue(4.0);
    }
    event->accept();
}

void IvaVideoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        updateCursorAt(boundedScenePoint(event->position().toPoint()));
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const QPointF current = boundedScenePoint(event->position().toPoint());
    if (m_interactionMode == InteractionMode::DrawRectangle && m_draftItem) {
        m_draftItem->setRect(QRectF(m_dragStart, current).normalized());
    } else if (m_interactionMode == InteractionMode::MoveRectangle) {
        QRectF rectangle = m_dragStartRectangle.translated(current - m_dragStart);
        const QRectF bounds = m_scene->sceneRect();
        rectangle.moveLeft(qBound(bounds.left(), rectangle.left(),
                                  bounds.right() - rectangle.width()));
        rectangle.moveTop(qBound(bounds.top(), rectangle.top(),
                                 bounds.bottom() - rectangle.height()));
        setSelectedRectangle(rectangle);
    } else if (m_interactionMode == InteractionMode::MoveVertex) {
        QRectF rectangle = m_dragStartRectangle;
        const qreal minimumSize = 8.0;
        switch (m_dragVertex) {
        case 0:
            rectangle.setLeft(qMin(current.x(), rectangle.right() - minimumSize));
            rectangle.setTop(qMin(current.y(), rectangle.bottom() - minimumSize));
            break;
        case 1:
            rectangle.setRight(qMax(current.x(), rectangle.left() + minimumSize));
            rectangle.setTop(qMin(current.y(), rectangle.bottom() - minimumSize));
            break;
        case 2:
            rectangle.setRight(qMax(current.x(), rectangle.left() + minimumSize));
            rectangle.setBottom(qMax(current.y(), rectangle.top() + minimumSize));
            break;
        case 3:
            rectangle.setLeft(qMin(current.x(), rectangle.right() - minimumSize));
            rectangle.setBottom(qMax(current.y(), rectangle.top() + minimumSize));
            break;
        default:
            break;
        }
        const QRectF bounds = m_scene->sceneRect();
        rectangle.setLeft(qBound(bounds.left(), rectangle.left(),
                                 bounds.right() - minimumSize));
        rectangle.setRight(qBound(bounds.left() + minimumSize, rectangle.right(),
                                  bounds.right()));
        rectangle.setTop(qBound(bounds.top(), rectangle.top(),
                                bounds.bottom() - minimumSize));
        rectangle.setBottom(qBound(bounds.top() + minimumSize, rectangle.bottom(),
                                   bounds.bottom()));
        setSelectedRectangle(rectangle.normalized());
    }
    updateCursorAt(current);
    event->accept();
}

void IvaVideoCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const bool drawing = m_interactionMode == InteractionMode::DrawRectangle;
    const bool editing = m_interactionMode == InteractionMode::MoveRectangle
        || m_interactionMode == InteractionMode::MoveVertex;
    const QRectF rectangle = drawing && m_draftItem
        ? m_draftItem->rect().normalized()
        : m_editRectangle;
    m_dragging = false;
    m_interactionMode = InteractionMode::None;
    if (rectangle.width() >= 8.0 && rectangle.height() >= 8.0) {
        if (drawing) emit rectangleDrafted(rectangle);
        else if (editing) emit rectangleEdited(rectangle);
    } else {
        if (drawing && m_draftItem) {
            m_scene->removeItem(m_draftItem);
            delete m_draftItem;
            m_draftItem = nullptr;
        } else if (editing) {
            setSelectedRectangle(m_dragStartRectangle);
        }
        emit rectangleRejected(
            QStringLiteral("The selected area is too small."));
    }
    updateCursor();
    event->accept();
}

void IvaVideoCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    fitScene();
}

QPointF IvaVideoCanvas::boundedScenePoint(const QPoint &viewportPoint) const
{
    QPointF point = mapToScene(viewportPoint);
    const QRectF bounds = m_scene->sceneRect();
    if (!bounds.isValid() || bounds.isEmpty()) return point;
    point.setX(qBound(bounds.left(), point.x(), bounds.right()));
    point.setY(qBound(bounds.top(), point.y(), bounds.bottom()));
    return point;
}

QRectF IvaVideoCanvas::selectedAreaRectangle() const
{
    if (!m_editRectangle.isEmpty() && m_editRectangle.isValid()) {
        return m_editRectangle.normalized();
    }
    for (const IvaAreaDefinition &area : m_areas) {
        if (area.areaIndex != m_selectedAreaIndex || area.areaCoordinates.size() < 3) {
            continue;
        }
        QPolygonF polygon;
        for (const QPointF &point : area.areaCoordinates) polygon.append(point);
        return polygon.boundingRect().normalized();
    }
    return {};
}

int IvaVideoCanvas::selectedVertexAt(const QPointF &scenePoint) const
{
    const QRectF rectangle = selectedAreaRectangle();
    if (rectangle.isEmpty()) return -1;
    const QList<QPointF> vertices = rectangleCoordinates(rectangle);
    const qreal tolerance = sceneInteractionTolerance();
    for (int index = 0; index < vertices.size(); ++index) {
        if (QLineF(scenePoint, vertices.at(index)).length() <= tolerance) return index;
    }
    return -1;
}

qreal IvaVideoCanvas::sceneInteractionTolerance() const
{
    const QPointF origin = mapToScene(QPoint(0, 0));
    const QPointF offset = mapToScene(QPoint(12, 0));
    return qMax<qreal>(12.0, QLineF(origin, offset).length());
}

void IvaVideoCanvas::setSelectedRectangle(const QRectF &rectangle)
{
    m_editRectangle = rectangle.normalized();
    const QPolygonF polygon(rectangleCoordinates(m_editRectangle));
    for (QGraphicsPolygonItem *item : m_areaItems) {
        if (item->data(0).toInt() == m_selectedAreaIndex) {
            item->setPolygon(polygon);
            break;
        }
    }
    rebuildEditHandles();
}

void IvaVideoCanvas::rebuildEditHandles()
{
    clearEditHandles();
    if (!m_editMode) return;
    const QRectF rectangle = selectedAreaRectangle();
    if (rectangle.isEmpty()) return;
    const QList<QPointF> vertices = rectangleCoordinates(rectangle);
    for (int index = 0; index < vertices.size(); ++index) {
        auto *handle = m_scene->addEllipse(
            QRectF(-12.0, -12.0, 24.0, 24.0),
            QPen(QColor(QStringLiteral("#263238")), 3.0),
            QBrush(QColor(QStringLiteral("#ffffff"))));
        handle->setPos(vertices.at(index));
        handle->setZValue(5.0);
        handle->setToolTip(QStringLiteral("Move corner %1").arg(index + 1));
        m_editHandles.append(handle);
    }
}

void IvaVideoCanvas::clearEditHandles()
{
    for (QGraphicsEllipseItem *handle : m_editHandles) {
        m_scene->removeItem(handle);
        delete handle;
    }
    m_editHandles.clear();
}

void IvaVideoCanvas::updateCursor()
{
    updateCursorAt(boundedScenePoint(mapFromGlobal(QCursor::pos())));
}

void IvaVideoCanvas::updateCursorAt(const QPointF &scenePoint)
{
    if (m_dragging && m_interactionMode == InteractionMode::MoveRectangle) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_dragging && m_interactionMode == InteractionMode::MoveVertex) {
        switch (m_dragVertex) {
        case 0:
        case 2:
            setCursor(Qt::SizeFDiagCursor);
            return;
        case 1:
        case 3:
            setCursor(Qt::SizeBDiagCursor);
            return;
        default:
            break;
        }
    }
    if (m_editMode && !selectedAreaRectangle().isEmpty()) {
        const int vertex = selectedVertexAt(scenePoint);
        if (vertex == 0 || vertex == 2) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (vertex == 1 || vertex == 3) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (selectedAreaRectangle().contains(scenePoint)) {
            setCursor(Qt::OpenHandCursor);
        } else if (m_drawMode) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    } else if (m_drawMode) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void IvaVideoCanvas::rebuildOverlays()
{
    for (QGraphicsPolygonItem *item : m_areaItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_areaItems.clear();
    for (const IvaAreaDefinition &area : m_areas) {
        QPolygonF polygon;
        for (const QPointF &point : area.areaCoordinates) {
            polygon.append(point);
        }
        auto *item = m_scene->addPolygon(polygon);
        item->setData(0, area.areaIndex);
        item->setToolTip(QStringLiteral("%1 / index %2")
                             .arg(area.name).arg(area.areaIndex));
        item->setZValue(2.0);
        m_areaItems.append(item);
    }
    updateOverlayStyles();
    m_editRectangle = {};
    rebuildEditHandles();
}

void IvaVideoCanvas::updateOverlayStyles()
{
    for (QGraphicsPolygonItem *item : m_areaItems) {
        const bool selected = item->data(0).toInt() == m_selectedAreaIndex;
        const QColor color = selected ? QColor(QStringLiteral("#00e5ff"))
                                      : QColor(QStringLiteral("#76ff03"));
        item->setPen(QPen(color, selected ? 5.0 : 3.0));
        item->setBrush(QColor(color.red(), color.green(), color.blue(),
                              selected ? 70 : 35));
    }
}

void IvaVideoCanvas::fitScene()
{
    if (m_coordinateResolution.isValid() && viewport()->size().isValid()) {
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
}

void IvaVideoCanvas::updateParkingRoiItem(
    QGraphicsRectItem *&item, const QRectF &normalizedRectangle,
    const QColor &color, Qt::PenStyle penStyle, qreal zValue)
{
    const QRectF sourceRectangle = sourceFromNormalized(
        normalizedRectangle, m_coordinateResolution);
    if (sourceRectangle.isEmpty()) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
            item = nullptr;
        }
        return;
    }
    if (!item) item = m_scene->addRect(sourceRectangle);
    else item->setRect(sourceRectangle);
    item->setPen(QPen(color, 4.0, penStyle));
    item->setBrush(QColor(color.red(), color.green(), color.blue(), 45));
    item->setZValue(zValue);
}
