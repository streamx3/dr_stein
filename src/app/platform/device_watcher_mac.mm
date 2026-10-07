// SPDX-License-Identifier: MIT
#include "device_watcher.hpp"

#import <DiskArbitration/DiskArbitration.h>
#import <Foundation/Foundation.h>

namespace drstein::ui {

namespace {

QString bsdName(DADiskRef disk) {
    const char* name = DADiskGetBSDName(disk);
    return name ? QString::fromUtf8(name) : QString();
}

void onAppeared(DADiskRef disk, void* ctx) { static_cast<DeviceWatcher*>(ctx)->notify("appeared " + bsdName(disk)); }
void onDisappeared(DADiskRef disk, void* ctx) { static_cast<DeviceWatcher*>(ctx)->notify("disappeared " + bsdName(disk)); }
void onDescriptionChanged(DADiskRef disk, CFArrayRef, void* ctx) { static_cast<DeviceWatcher*>(ctx)->notify("changed " + bsdName(disk)); }

} // namespace

void DeviceWatcher::start() {
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session) return;
    DARegisterDiskAppearedCallback(session, nullptr, onAppeared, this);
    DARegisterDiskDisappearedCallback(session, nullptr, onDisappeared, this);
    // Mount and unmount show up as a change of the volume path; watching every key is cheap enough.
    DARegisterDiskDescriptionChangedCallback(session, nullptr, nullptr, onDescriptionChanged, this);
    // Qt's event loop drives the main CFRunLoop, so callbacks arrive on the GUI thread.
    DASessionScheduleWithRunLoop(session, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    m_native = session;
    m_available = true;
}

void DeviceWatcher::stop() {
    if (!m_native) return;
    auto session = static_cast<DASessionRef>(m_native);
    DAUnregisterCallback(session, reinterpret_cast<void*>(onAppeared), this);
    DAUnregisterCallback(session, reinterpret_cast<void*>(onDisappeared), this);
    DAUnregisterCallback(session, reinterpret_cast<void*>(onDescriptionChanged), this);
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    CFRelease(session);
    m_native = nullptr;
}

} // namespace drstein::ui
