// SPDX-License-Identifier: MIT
#include "device_watcher.hpp"

#include <QMetaObject>

namespace drstein::ui {

DeviceWatcher::DeviceWatcher(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, [this] { Q_EMIT devicesChanged(m_last); });
    auto w = stein::platform::DeviceWatcher::create([this](const stein::platform::DeviceEvent& e) {
        const QString what = QString::fromUtf8(stein::platform::toString(e.kind).data(), static_cast<qsizetype>(stein::platform::toString(e.kind).size())) +
                             (e.device.empty() ? QString() : " " + QString::fromStdString(e.device));
        QMetaObject::invokeMethod(this, [this, what] { onEvent(what); }, Qt::QueuedConnection);
    });
    if (w) m_watcher = std::move(*w);
}

DeviceWatcher::~DeviceWatcher() {
    if (m_watcher) m_watcher->stop();   // joins the library thread before the QObject goes away
}

void DeviceWatcher::onEvent(const QString& what) {
    m_last = what;
    m_debounce.start();
}

} // namespace drstein::ui
