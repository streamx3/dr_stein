// SPDX-License-Identifier: MIT
#include "window_chrome.hpp"

namespace drstein::ui {

#if !defined(Q_OS_MACOS)
int WindowChrome::leftInset() const { return 0; }
bool WindowChrome::nativeButtons() const { return false; }
void WindowChrome::attach(QWindow*) {}
QVariantMap WindowChrome::metrics(QWindow*) const { return {}; }
#endif

} // namespace drstein::ui
