// SPDX-License-Identifier: MIT
// The application icon (icons/app/README.md) for the taskbar, alt-tab and dock.
#pragma once

#include <QIcon>

namespace drstein::ui {

// The main drawing rendered at 48 px and up, the bolder small variant at 32 px and below.
// Rendered to pixmaps rather than added as SVG files: Qt's SVG icon engine keeps one file per
// mode and state, so a second addFile() would replace the first instead of covering other sizes.
QIcon appIcon();

} // namespace drstein::ui
