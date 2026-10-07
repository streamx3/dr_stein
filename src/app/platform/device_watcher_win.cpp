// SPDX-License-Identifier: MIT
// Windows: WM_DEVICECHANGE reaches every top-level window; a native event filter
// sees it without a window of its own. DBT_DEVNODES_CHANGED covers arrivals,
// removals and volume mounts in one message.
#include "device_watcher.hpp"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>

#include <windows.h>
#include <dbt.h>

namespace drstein::ui {

namespace {
class Filter : public QAbstractNativeEventFilter {
public:
    explicit Filter(DeviceWatcher* w) : m_watcher(w) {}
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (type != "windows_generic_MSG") return false;
        auto* msg = static_cast<MSG*>(message);
        if (msg->message != WM_DEVICECHANGE) return false;
        switch (msg->wParam) {
        case DBT_DEVICEARRIVAL: m_watcher->notify("arrival"); break;
        case DBT_DEVICEREMOVECOMPLETE: m_watcher->notify("removal"); break;
        case DBT_DEVNODES_CHANGED: m_watcher->notify("device nodes changed"); break;
        default: break;
        }
        return false;
    }
private:
    DeviceWatcher* m_watcher;
};
} // namespace

void DeviceWatcher::start() {
    auto* f = new Filter(this);
    QCoreApplication::instance()->installNativeEventFilter(f);
    m_native = f;
    m_available = true;
}

void DeviceWatcher::stop() {
    auto* f = static_cast<Filter*>(m_native);
    if (!f) return;
    QCoreApplication::instance()->removeNativeEventFilter(f);
    delete f;
    m_native = nullptr;
}

} // namespace drstein::ui
