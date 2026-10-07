// SPDX-License-Identifier: MIT
// Native touches for the client-side title bar. Everything cross-platform
// lives in QML (Main.qml, TopBar, WindowControls); this is only what Qt's
// window flags cannot express.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QWindow>

namespace drstein::ui {

class WindowChrome : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Width reserved on the left of the top bar for the traffic lights (macOS); 0 elsewhere.
    Q_PROPERTY(int leftInset READ leftInset CONSTANT)
    // Whether the platform draws its own caption buttons when the custom chrome is on.
    Q_PROPERTY(bool nativeButtons READ nativeButtons CONSTANT)

public:
    static WindowChrome* create(QQmlEngine*, QJSEngine*) { return new WindowChrome(); }
    int leftInset() const;
    bool nativeButtons() const;
    // Called once the window exists: on macOS attaches an empty unified toolbar so the
    // traffic lights sit centred in a 52 px bar and hides the title text. No-op elsewhere.
    Q_INVOKABLE void attach(QWindow* window);
    // Where the system's own controls ended up, in window pixels from the top-left:
    // {"lightsCenterY": N, "leftInset": N}. Empty when the platform draws none.
    Q_INVOKABLE QVariantMap metrics(QWindow* window) const;
};

} // namespace drstein::ui
