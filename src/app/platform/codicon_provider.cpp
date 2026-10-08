// SPDX-License-Identifier: MIT
#include "codicon_provider.hpp"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace drstein::ui {

QImage CodiconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const qsizetype slash = id.lastIndexOf(QLatin1Char('/'));
    const QString name = slash < 0 ? id : id.left(slash);
    const QColor color = slash < 0 ? QColor(Qt::black) : QColor::fromString(QLatin1Char('#') + id.mid(slash + 1));

    QFile file(QStringLiteral(":/codicons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = file.readAll();
    // QtSvg reads #rrggbb only; an #aarrggbb name paints black.
    svg.replace("currentColor", (color.isValid() ? color : QColor(Qt::black)).name(QColor::HexRgb).toLatin1());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};
    const QSize px = requestedSize.isValid() ? requestedSize : renderer.defaultSize();
    QImage image(px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    if (size) *size = px;
    return image;
}

} // namespace drstein::ui
