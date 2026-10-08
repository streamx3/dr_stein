// SPDX-License-Identifier: MIT
// Serves the Codicon SVGs in icons/codicons/ (Microsoft, CC-BY 4.0) with the
// requested colour in place of currentColor, which QtSvg does not resolve.
// URL: image://codicon/<file name without .svg>/<rrggbb or aarrggbb>
#pragma once

#include <QQuickImageProvider>

namespace drstein::ui {

class CodiconProvider : public QQuickImageProvider {
public:
    CodiconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace drstein::ui
