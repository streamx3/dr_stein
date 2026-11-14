// SPDX-License-Identifier: MIT
#include "preview_surface.hpp"

#include <QBrush>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace drstein::ui {

PreviewSurface::PreviewSurface(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void PreviewSurface::setCheckerColor(const QColor& color) {
    if (color == m_checker[0]) return;
    m_checker[0] = color;
    update();
    Q_EMIT checkerChanged();
}

void PreviewSurface::setCheckerAltColor(const QColor& color) {
    if (color == m_checker[1]) return;
    m_checker[1] = color;
    update();
    Q_EMIT checkerChanged();
}

void PreviewSurface::setImage(const QImage& image) {
    if (image.cacheKey() == m_image.cacheKey()) return;
    m_image = image;
    m_fitted = {};
    update();
    Q_EMIT imageChanged();
}

// Called by the scene graph, off the GUI thread as a rule, but with the GUI thread blocked for
// the duration: the members are safe to touch.
void PreviewSurface::paint(QPainter* painter) {
    if (m_image.isNull()) return;
    // The painter arrives scaled by the device pixel ratio. Sizes are worked out in the pixels
    // it really paints, and the picture goes down there one to one: scaled once, by the smooth
    // scaler, not by the painter, whose filtering shimmers on a strong reduction.
    const QTransform toDevice = painter->deviceTransform();
    const QSize room(static_cast<int>(std::floor(width() * toDevice.m11())), static_cast<int>(std::floor(height() * toDevice.m22())));
    if (room.isEmpty()) return;
    QSize shown = m_image.size();
    if (shown.width() > room.width() || shown.height() > room.height()) shown = shown.scaled(room, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    if (shown == m_image.size()) m_fitted = {};
    else if (m_fitted.size() != shown) m_fitted = m_image.scaled(shown, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QPoint origin = toDevice.map(QPointF(0, 0)).toPoint();
    const QPoint corner = origin + QPoint((room.width() - shown.width()) / 2, (room.height() - shown.height()) / 2);
    painter->setTransform(toDevice.inverted(), true);
    const QRect target(corner, shown);
    if (m_image.hasAlphaChannel()) {
        // Squares of 8 logical pixels, counted from the picture's corner.
        const int side = std::max(1, static_cast<int>(std::lround(8 * toDevice.m11())));
        QImage tile(2 * side, 2 * side, QImage::Format_RGB32);
        tile.fill(m_checker[0]);
        QPainter squares(&tile);
        squares.fillRect(side, 0, side, side, m_checker[1]);
        squares.fillRect(0, side, side, side, m_checker[1]);
        squares.end();
        QBrush brush(tile);
        brush.setTransform(QTransform::fromTranslate(target.left(), target.top()));
        painter->fillRect(target, brush);
    }
    painter->drawImage(target, m_fitted.isNull() ? m_image : m_fitted);
}

} // namespace drstein::ui
