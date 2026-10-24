// SPDX-License-Identifier: MIT
#include "drstein/core/mounts.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/ucred.h>
#elif defined(__linux__)
#include <fstream>
#endif

namespace drstein::core {

using namespace stein;

std::string mountDirectoryName(std::string_view label, std::string_view fallback) {
    auto clean = [](std::string_view in) {
        std::string out;
        for (unsigned char c : in) {
            // Path separators on every OS, ':' (Finder shows it as '/'), and control characters.
            if (c == '/' || c == '\\' || c == ':' || c < 0x20 || c == 0x7f) out += '_';
            else out += static_cast<char>(c);
        }
        // Trim what the shell and Finder treat specially at the edges: blanks and dots (a leading
        // dot hides the directory; "." and ".." are not names).
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
        std::size_t start = 0;
        while (start < out.size() && (out[start] == ' ' || out[start] == '.')) ++start;
        out.erase(0, start);
        if (out.size() > 64) {
            out.resize(64);
            while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();   // do not cut a UTF-8 sequence
            if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80)) out.pop_back();
        }
        return out;
    };
    std::string name = clean(label);
    if (name.empty()) name = clean(fallback);
    if (name.empty()) name = "volume";
    return name;
}

MountManager::~MountManager() { unmountAll(); }

Expected<MountRecord> MountManager::mount(std::unique_ptr<fs::Reader> reader, std::string what, std::optional<std::filesystem::path> mountpoint) {
    MountRequest r;
    r.what = std::move(what);
    r.mountpoint = std::move(mountpoint);
    return mount(std::move(reader), std::move(r));
}

Expected<MountRecord> MountManager::mount(std::unique_ptr<fs::Reader> reader, MountRequest request) {
    if (!available()) return fail(ErrorCategory::Unsupported, "no mount backend on this system");
    auto entry = std::make_unique<Entry>();
    std::error_code ec;
    const std::string name = mountDirectoryName(request.name, {});
    std::optional<std::filesystem::path> mountpoint = request.mountpoint;
    if (!mountpoint) {
        const auto base = std::filesystem::temp_directory_path(ec) / "drstein-mounts";
        std::filesystem::create_directories(base, ec);
        // The label names the directory; a second mount with the same label gets "-2", and so on.
        const std::filesystem::path dir = pickMountDirectory(base, name);
#if defined(_WIN32)
        // WinFsp wants an absent directory or a free drive letter; the directory must not exist.
        mountpoint = dir;
#else
        std::filesystem::create_directories(dir, ec);
        mountpoint = dir;
#endif
        entry->ownsDirectory = true;
    }
    // A caller's own mountpoint that is one already would be shadowed, not served; and the resolution
    // below must not touch a directory whose server may be gone.
    if (request.mountpoint && isMounted(*mountpoint)) return fail(ErrorCategory::Busy, mountpoint->string() + " is already a mountpoint");
    // Resolved once, now, while the directory is a plain one: the table may list this spelling, and the
    // watchdog compares against both rather than stat the live mountpoint to find out.
    std::filesystem::path real = std::filesystem::weakly_canonical(*mountpoint, ec);
    if (ec || real.empty()) real = *mountpoint;
    // Fully qualified: <sys/mount.h> declares a global `struct mount`, which makes the bare name ambiguous.
    stein::mount::MountOptions mo;
    mo.fsName = name;   // what Finder (NFS export path) and FUSE (fsname / volname) show
    auto m = stein::mount::Mount::create(std::move(reader), *mountpoint, mo);
    if (!m) {
        if (entry->ownsDirectory) std::filesystem::remove(*mountpoint, ec);
        return fail(m.error());
    }
    entry->mount = std::move(*m);
    entry->record.mountpoint = *mountpoint;
    entry->record.realMountpoint = std::move(real);
    entry->record.what = std::move(request.what);
    entry->record.sourceId = std::move(request.sourceId);
    entry->record.nodePath = std::move(request.nodePath);
    entry->record.running = true;
    Entry* raw = entry.get();
    entry->thread = std::thread([this, raw] {
        auto r = raw->mount->run();
        std::lock_guard lock(m_mutex);
        raw->record.running = false;
        if (!r) raw->record.error = r.error().message();
    });
    std::lock_guard lock(m_mutex);
    entry->record.id = m_nextId++;
    MountRecord rec = entry->record;
    m_entries.push_back(std::move(entry));
    return rec;
}

std::vector<MountRecord> MountManager::list() const {
    std::lock_guard lock(m_mutex);
    std::vector<MountRecord> out;
    for (const auto& e : m_entries) out.push_back(e->record);
    return out;
}

std::optional<MountRecord> MountManager::find(const std::string& sourceId, const std::vector<int>& nodePath) const {
    std::lock_guard lock(m_mutex);
    for (const auto& e : m_entries)
        if (!e->record.sourceId.empty() && e->record.sourceId == sourceId && e->record.nodePath == nodePath) return e->record;
    return std::nullopt;
}

std::optional<MountRecord> MountManager::findByMountpoint(const std::filesystem::path& mountpoint) const {
    std::lock_guard lock(m_mutex);
    for (const auto& e : m_entries)
        if (e->record.mountpoint == mountpoint || e->record.realMountpoint == mountpoint) return e->record;
    return std::nullopt;
}

void MountManager::stopEntry(Entry& e) {
    if (e.mount) e.mount->stop();
    if (e.thread.joinable()) e.thread.join();
    e.mount.reset();
    if (e.ownsDirectory) {
        std::error_code ec;
        std::filesystem::remove(e.record.mountpoint, ec);
    }
    e.record.running = false;
}

Expected<void> MountManager::unmount(int id) {
    std::unique_ptr<Entry> taken;
    {
        std::lock_guard lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if ((*it)->record.id == id) {
                taken = std::move(*it);
                m_entries.erase(it);
                break;
            }
    }
    if (!taken) return fail(ErrorCategory::NotFound, "no such mount");
    stopEntry(*taken);
    return {};
}

void MountManager::unmountAll() {
    std::vector<std::unique_ptr<Entry>> taken;
    {
        std::lock_guard lock(m_mutex);
        taken.swap(m_entries);
    }
    for (auto& e : taken) stopEntry(*e);
}

std::vector<MountRecord> MountManager::reapEnded() {
    std::vector<std::unique_ptr<Entry>> taken;
    {
        std::lock_guard lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            // The loop ended on its own (the backend noticed the unmount), or the OS has already
            // dropped the mountpoint while the loop still waits: either way the mount is gone.
            if (!(*it)->record.running || !isMounted((*it)->record.mountpoint, (*it)->record.realMountpoint)) {
                taken.push_back(std::move(*it));
                it = m_entries.erase(it);
            } else ++it;
        }
    }
    std::vector<MountRecord> out;
    for (auto& e : taken) {
        stopEntry(*e);
        out.push_back(e->record);
    }
    return out;
}

std::filesystem::path MountManager::pickMountDirectory(const std::filesystem::path& base, const std::string& name) {
    std::error_code ec;
    // The table may list the base with its symlinks resolved. Resolving the base is a stat of a plain
    // directory of ours, never of a mountpoint inside it.
    std::filesystem::path realBase = std::filesystem::weakly_canonical(base, ec);
    if (ec || realBase.empty()) realBase = base;
    std::string leaf = name;
    for (int n = 2;; ++n) {
        const std::filesystem::path dir = base / leaf;
        // The table first: a stale directory of a crashed run may still be a mountpoint, and exists()
        // on one whose server is gone hangs (soft NFS waits out its timeouts, FUSE until the daemon answers).
        if (!isMounted(dir, realBase / leaf) && !std::filesystem::exists(dir, ec)) return dir;
        leaf = name + "-" + std::to_string(n);
    }
}

bool MountManager::isMounted(const std::filesystem::path& mountpoint, const std::filesystem::path& alias) {
#if defined(__APPLE__) || defined(__linux__)
    // The table may hold the path as given or with symlinks resolved ($TMPDIR on macOS sits under
    // /var, which is /private/var); the caller passes both spellings, resolved when the directory
    // was still plain. Nothing here touches the filesystem: this runs from the watchdog, on the GUI
    // thread, against mountpoints whose server may be this very process or gone.
    const std::string want = mountpoint.string();
    const std::string also = alias.string();
    auto matches = [&](std::string_view on) { return on == want || (!also.empty() && on == also); };
#endif
#if defined(__APPLE__)
    // MNT_NOWAIT: the table as the kernel has it, without asking each filesystem for fresh
    // statistics, so an unresponsive server cannot block this call.
    const int n = getfsstat(nullptr, 0, MNT_NOWAIT);
    if (n <= 0) return false;
    std::vector<struct statfs> table(static_cast<std::size_t>(n) + 8);
    const int got = getfsstat(table.data(), static_cast<int>(table.size() * sizeof(struct statfs)), MNT_NOWAIT);
    if (got <= 0) return false;
    for (int i = 0; i < got; ++i) {
        const char* on = table[static_cast<std::size_t>(i)].f_mntonname;
        if (matches(std::string_view(on, strnlen(on, sizeof(table[0].f_mntonname))))) return true;
    }
    return false;
#elif defined(__linux__)
    // /proc/self/mounts escapes blanks and the backslash in octal ("\040"); undo that before comparing.
    std::ifstream in("/proc/self/mounts");
    std::string line;
    while (std::getline(in, line)) {
        const auto a = line.find(' ');
        if (a == std::string::npos) continue;
        const auto b = line.find(' ', a + 1);
        std::string target = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        std::string decoded;
        for (std::size_t i = 0; i < target.size(); ++i) {
            if (target[i] == '\\' && i + 3 < target.size()) {
                decoded += static_cast<char>((target[i + 1] - '0') * 64 + (target[i + 2] - '0') * 8 + (target[i + 3] - '0'));
                i += 3;
            } else decoded += target[i];
        }
        if (matches(decoded)) return true;
    }
    return false;
#else
    (void)mountpoint;
    (void)alias;
    return true;
#endif
}

} // namespace drstein::core
