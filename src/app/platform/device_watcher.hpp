// SPDX-License-Identifier: MIT
// Qt side of libstein's platform::DeviceWatcher: the library's callback arrives
// on its own thread; this marshals it to the GUI thread and debounces, so the
// workspace re-lists once per plug or mount, not once per OS message.
#pragma once

#include "stein/platform/device_watcher.hpp"

#include <QObject>
#include <QTimer>

#include <memory>

namespace drstein::ui {

class DeviceWatcher : public QObject {
    Q_OBJECT
public:
    explicit DeviceWatcher(QObject* parent = nullptr);
    ~DeviceWatcher() override;
    bool available() const { return m_watcher != nullptr; }

Q_SIGNALS:
    void devicesChanged(QString lastEvent);

private:
    void onEvent(const QString& what);   // GUI thread
    std::unique_ptr<stein::platform::DeviceWatcher> m_watcher;
    QTimer m_debounce;
    QString m_last;
};

} // namespace drstein::ui
