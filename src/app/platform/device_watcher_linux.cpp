// SPDX-License-Identifier: MIT
// Linux: device nodes come and go in /dev (devtmpfs supports inotify), and the
// kernel marks /proc/self/mounts readable-with-priority-data when the mount
// table changes, which poll() reports as POLLPRI. No libudev needed.
#include "device_watcher.hpp"

#include <QFileSystemWatcher>
#include <QSocketNotifier>

#include <fcntl.h>
#include <unistd.h>

namespace drstein::ui {

namespace {
struct Native {
    QFileSystemWatcher* dev = nullptr;
    QSocketNotifier* mounts = nullptr;
    int mountsFd = -1;
};
} // namespace

void DeviceWatcher::start() {
    auto* n = new Native;
    n->dev = new QFileSystemWatcher(this);
    n->dev->addPath("/dev");
    connect(n->dev, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) { notify("device nodes changed"); });
    n->mountsFd = ::open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);
    if (n->mountsFd >= 0) {
        n->mounts = new QSocketNotifier(n->mountsFd, QSocketNotifier::Exception, this);
        connect(n->mounts, &QSocketNotifier::activated, this, [this](QSocketDescriptor, QSocketNotifier::Type) { notify("mount table changed"); });
    }
    m_native = n;
    m_available = true;
}

void DeviceWatcher::stop() {
    auto* n = static_cast<Native*>(m_native);
    if (!n) return;
    if (n->mountsFd >= 0) ::close(n->mountsFd);
    delete n;
    m_native = nullptr;
}

} // namespace drstein::ui
