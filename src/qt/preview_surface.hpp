// SPDX-License-Identifier: MIT
// Paints a decoded preview: the picture centred and fitted into the item, at
// most one image pixel per screen pixel. It takes the QImage itself, so what
// the preview worker decoded is shown without a URL or an image provider in
// between. A picture with an alpha channel lies on a checkerboard of the two
// checker colours, so a dark drawing on nothing still shows on a dark pane.
#pragma once

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>

namespace drstein::ui {

class PreviewSurface : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
    Q_PROPERTY(QColor checkerColor READ checkerColor WRITE setCheckerColor NOTIFY checkerChanged)
    Q_PROPERTY(QColor checkerAltColor READ checkerAltColor WRITE setCheckerAltColor NOTIFY checkerChanged)

public:
    explicit PreviewSurface(QQuickItem* parent = nullptr);

    QImage image() const { return m_image; }
    void setImage(const QImage& image);
    QColor checkerColor() const { return m_checker[0]; }
    void setCheckerColor(const QColor& color);
    QColor checkerAltColor() const { return m_checker[1]; }
    void setCheckerAltColor(const QColor& color);

    void paint(QPainter* painter) override;

Q_SIGNALS:
    void imageChanged();
    void checkerChanged();

private:
    QImage m_image;
    QImage m_fitted;   // m_image at the size last painted, when that is smaller: a repaint does not scale again
    QColor m_checker[2] = {QColor(0x80, 0x80, 0x80), QColor(0x99, 0x99, 0x99)};
};

} // namespace drstein::ui
