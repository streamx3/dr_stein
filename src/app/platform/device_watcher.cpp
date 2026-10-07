// SPDX-License-Identifier: MIT
#include "device_watcher.hpp"

#include <QDateTime>

namespace drstein::ui {

DeviceWatcher::DeviceWatcher(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, [this] { Q_EMIT devicesChanged(m_last); });
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    start();
}

DeviceWatcher::~DeviceWatcher() { stop(); }

void DeviceWatcher::notify(const QString& what) {
    if (QDateTime::currentMSecsSinceEpoch() - m_startedAt < 1500) return;   // the registration replay
    m_last = what;
    m_debounce.start();
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_LINUX) && !defined(Q_OS_WIN)
void DeviceWatcher::start() {}
void DeviceWatcher::stop() {}
#endif

} // namespace drstein::ui
