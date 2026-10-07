// SPDX-License-Identifier: MIT
// macOS: the window keeps AppKit's traffic lights, resizing, zoom, Stage Manager
// and full-screen; we only move the lights into our bar and drop the title text.
#include "window_chrome.hpp"

#import <AppKit/AppKit.h>

#include <cmath>

namespace drstein::ui {

int WindowChrome::leftInset() const { return 80; }
bool WindowChrome::nativeButtons() const { return true; }

void WindowChrome::attach(QWindow* window) {
    if (!window) return;
    auto* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* ns = [view window];
    if (!ns) return;
    ns.titleVisibility = NSWindowTitleHidden;
    ns.titlebarAppearsTransparent = YES;
    // An empty unified toolbar is what gives the taller title bar area (52 px) with the
    // traffic lights vertically centred in it, the same trick editors use.
    if (@available(macOS 11.0, *)) {
        NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"drstein.chrome"];
        toolbar.showsBaselineSeparator = NO;
        ns.toolbar = toolbar;
        ns.toolbarStyle = NSWindowToolbarStyleUnified;
    }
}

QVariantMap WindowChrome::metrics(QWindow* window) const {
    QVariantMap m;
    if (!window) return m;
    auto* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* ns = [view window];
    if (!ns) return m;
    NSButton* close = [ns standardWindowButton:NSWindowCloseButton];
    NSButton* zoom = [ns standardWindowButton:NSWindowZoomButton];
    if (!close || !zoom) return m;
    // Window coordinates have their origin at the bottom-left of the whole frame.
    const NSRect c = [close convertRect:close.bounds toView:nil];
    const NSRect z = [zoom convertRect:zoom.bounds toView:nil];
    const double height = ns.frame.size.height;
    m["lightsCenterY"] = static_cast<int>(std::lround(height - NSMidY(c)));
    m["leftInset"] = static_cast<int>(std::lround(NSMaxX(z) + 14 - 0));
    return m;
}

} // namespace drstein::ui
