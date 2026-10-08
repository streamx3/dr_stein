// SPDX-License-Identifier: MIT
#include "app_icon.hpp"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace drstein::ui {

namespace {

void addRendered(QIcon& icon, const QString& svg, std::initializer_list<int> sizes) {
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return;
    for (int n : sizes) {
        QPixmap pm(n, n);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pm);
    }
}

} // namespace

QIcon appIcon() {
    QIcon icon;
    addRendered(icon, QStringLiteral(":/icons/dr-stein-small.svg"), {16, 24, 32});
    addRendered(icon, QStringLiteral(":/icons/dr-stein.svg"), {48, 64, 128, 256, 512});
    return icon;
}

} // namespace drstein::ui
