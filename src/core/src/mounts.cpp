// SPDX-License-Identifier: MIT
#include "drstein/core/mounts.hpp"

#include <chrono>

namespace drstein::core {

using namespace stein;

MountManager::~MountManager() { unmountAll(); }

Expected<MountRecord> MountManager::mount(std::unique_ptr<fs::Reader> reader, std::string what, std::optional<std::filesystem::path> mountpoint) {
    if (!available()) return fail(ErrorCategory::Unsupported, "no mount backend on this system");
    auto entry = std::make_unique<Entry>();
    std::error_code ec;
    if (!mountpoint) {
        const auto base = std::filesystem::temp_directory_path(ec) / "drstein-mounts";
        std::filesystem::create_directories(base, ec);
        std::filesystem::path dir;
        for (int n = 1;; ++n) {
            dir = base / ("mount-" + std::to_string(n));
            if (!std::filesystem::exists(dir, ec)) break;
        }
#if defined(_WIN32)
        // WinFsp wants an absent directory or a free drive letter; the directory must not exist.
        mountpoint = dir;
#else
        std::filesystem::create_directories(dir, ec);
        mountpoint = dir;
#endif
        entry->ownsDirectory = true;
    }
    mount::MountOptions mo;
    mo.fsName = "stein";
    auto m = mount::Mount::create(std::move(reader), *mountpoint, mo);
    if (!m) {
        if (entry->ownsDirectory) std::filesystem::remove(*mountpoint, ec);
        return fail(m.error());
    }
    entry->mount = std::move(*m);
    entry->record.mountpoint = *mountpoint;
    entry->record.what = std::move(what);
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

} // namespace drstein::core
