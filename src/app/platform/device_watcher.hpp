// SPDX-License-Identifier: MIT
// Tells the workspace when the OS's view of block devices changes: a disk
// appears or disappears, a volume mounts or unmounts. One debounced signal;
// the workspace re-enumerates. Per platform:
//   macOS   DiskArbitration (appeared / disappeared / description changed)
//   Linux   inotify on /dev (device nodes) and poll() on /proc/self/mounts
//   Windows WM_DEVICECHANGE through a native event filter
// This belongs in libstein's platform layer eventually (it is OS code, not UI);
// it lives here until that session.
#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

namespace drstein::ui {

class DeviceWatcher : public QObject {
    Q_OBJECT
public:
    explicit DeviceWatcher(QObject* parent = nullptr);
    ~DeviceWatcher() override;
    bool available() const { return m_available; }
    // Platform hooks call this; the signal fires once, 400 ms after the last event.
    void notify(const QString& what);

Q_SIGNALS:
    void devicesChanged(QString lastEvent);

private:
    void start();
    void stop();
    QTimer m_debounce;
    QString m_last;
    qint64 m_startedAt = 0;      // DiskArbitration replays every disk at registration; that burst is not a change
    bool m_available = false;
    void* m_native = nullptr;   // platform session / handles
};

} // namespace drstein::ui
