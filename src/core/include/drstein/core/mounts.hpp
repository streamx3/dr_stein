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
    // `mountpoint` with its symlinks resolved, once, before the mount went live ($TMPDIR on macOS sits
    // under /var, which is /private/var): the OS table may list either spelling. Never re-resolved,
    // since a stat of a live mountpoint is a round trip to the server behind it.
    std::filesystem::path realMountpoint;
    std::string what;             // "ext4 "fedora" in ws-2026-10-04.stein"
    std::string sourceId;         // the open source the node belongs to ("image:/…", "disk:…"); "" when unknown
    std::vector<int> nodePath;    // the node inside it (core::NodePath), so views can find their own mounts
    bool running = false;
    std::string error;            // why the loop ended, if it failed
};

// What to mount: the description for the Mounts list, the name the directory (and, on
// macOS, the Finder volume) takes, and which node of which source it is.
struct MountRequest {
    std::string what;
    std::string name;             // the filesystem label when it has one, else the node's name; sanitised by mountDirectoryName()
    std::string sourceId;
    std::vector<int> nodePath;
    std::optional<std::filesystem::path> mountpoint;   // explicit; otherwise <tmp>/drstein-mounts/<name>
};

// The directory name a mount gets: the label, sanitised for a path component and for Finder
// (no separators, no control characters, not hidden, at most 64 bytes), else `fallback`
// treated the same way, else "volume".
std::string mountDirectoryName(std::string_view label, std::string_view fallback);

class MountManager {
public:
    MountManager() = default;
    ~MountManager();
    MountManager(const MountManager&) = delete;
    MountManager& operator=(const MountManager&) = delete;

    static bool available() { return stein::mount::Mount::available(); }
    // Mounts at request.mountpoint or, when empty, under the temp directory as request.name
    // (suffixed "-2", "-3"… while that name is taken). Returns once the OS accepted it.
    stein::Expected<MountRecord> mount(std::unique_ptr<stein::fs::Reader> reader, MountRequest request);
    // The short form: description only, directory "volume".
    stein::Expected<MountRecord> mount(std::unique_ptr<stein::fs::Reader> reader, std::string what, std::optional<std::filesystem::path> mountpoint = {});
    std::vector<MountRecord> list() const;
    std::optional<MountRecord> find(const std::string& sourceId, const std::vector<int>& nodePath) const;
    std::optional<MountRecord> findByMountpoint(const std::filesystem::path& mountpoint) const;
    stein::Expected<void> unmount(int id);
    void unmountAll();
    // Drops every mount whose serving loop has ended or whose mountpoint the OS no longer lists
    // (umount by hand, Finder eject): the thread is joined and the directory removed. Returns
    // what was dropped. Cheap: reads the mount table, never touches the mounts themselves.
    std::vector<MountRecord> reapEnded();
    // True when the OS mount table lists `mountpoint`, or `alias` (a second spelling of the same
    // directory, symlinks resolved), as a mountpoint. Reads the table only (getfsstat with MNT_NOWAIT /
    // /proc/self/mounts) and never looks at the path itself, so a stuck server cannot stall the
    // caller. Windows: always true.
    static bool isMounted(const std::filesystem::path& mountpoint, const std::filesystem::path& alias = {});
    // The directory a mount called `name` (already sanitised) gets under `base`: `name`, else
    // "name-2", "name-3"… while the candidate is taken. Taken is asked of the mount table first, then
    // of the disk: a stale directory of a crashed run may still be a mountpoint, and a stat of one
    // whose server is gone does not return.
    static std::filesystem::path pickMountDirectory(const std::filesystem::path& base, const std::string& name);

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
