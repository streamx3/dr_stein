// SPDX-License-Identifier: MIT
// Mounts the app made: a Reader exposed to the OS (FUSE / NFS loopback /
// WinFsp), each with its serving thread, unmounted on request and on exit.
#pragma once

#include "stein/core/error.hpp"
#include "stein/fs/reader.hpp"
#include "stein/mount/mount.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace drstein::core {

struct MountRecord {
    int id = 0;
    std::filesystem::path mountpoint;
    std::string what;             // "ext4 "fedora" in ws-2026-10-04.stein"
    bool running = false;
    std::string error;            // why the loop ended, if it failed
};

class MountManager {
public:
    MountManager() = default;
    ~MountManager();
    MountManager(const MountManager&) = delete;
    MountManager& operator=(const MountManager&) = delete;

    static bool available() { return stein::mount::Mount::available(); }
    // Mounts at `mountpoint` or, when empty, under the temp directory. Returns once the OS accepted it.
    stein::Expected<MountRecord> mount(std::unique_ptr<stein::fs::Reader> reader, std::string what, std::optional<std::filesystem::path> mountpoint = {});
    std::vector<MountRecord> list() const;
    stein::Expected<void> unmount(int id);
    void unmountAll();

private:
    struct Entry {
        MountRecord record;
        std::unique_ptr<stein::mount::Mount> mount;
        std::thread thread;
        bool ownsDirectory = false;
    };
    void stopEntry(Entry& e);

    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<Entry>> m_entries;
    int m_nextId = 1;
};

} // namespace drstein::core
