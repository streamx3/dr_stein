// SPDX-License-Identifier: MIT
// The file viewer: a Reader over any readable content node, a current
// directory with breadcrumbs, previews, hashes and copy-out. Read-only by
// construction (no Write capability exists in the library yet).
#pragma once

#include "drstein/core/topology.hpp"
#include "stein/core/progress.hpp"
#include "stein/fs/reader.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace drstein::core {

struct Entry {
    stein::fs::DirEntry entry;
    stein::fs::Stat stat;
    std::string linkTarget;        // symlinks
    std::string sizeText, mtimeText, modeText;
    bool isDir() const { return stat.type == stein::fs::FileType::Directory; }
};

struct Listing {
    stein::fs::Inode dir;
    std::vector<Entry> entries;    // directories first, then by name (case-insensitive)
};

struct Crumb {
    std::string name;              // "/" for the root
    stein::fs::Inode inode;
};

class Browser {
public:
    // The content node's reader (ReaderOptions pick an APFS volume / snapshot).
    static stein::Expected<Browser> open(const stein::probe::Node& root, const NodePath& path, const stein::fs::ReaderOptions& options = {});
    static stein::Expected<Browser> fromReader(std::unique_ptr<stein::fs::Reader> reader, std::string title, std::string fsName);

    Browser(Browser&&) noexcept = default;
    Browser& operator=(Browser&&) noexcept = default;

    stein::Expected<Listing> list(const stein::fs::Inode& dir);
    stein::Expected<Listing> listCurrent() { return list(current()); }
    stein::Expected<void> enter(std::string_view name);      // a directory inside the current one
    stein::Expected<void> goTo(std::string_view absolutePath); // "/etc/ssh"
    void up();
    void jumpTo(std::size_t crumbIndex);                      // breadcrumb click

    stein::fs::Inode current() const { return m_crumbs.back().inode; }
    const std::vector<Crumb>& breadcrumbs() const { return m_crumbs; }
    std::string pathText() const;                            // "/etc/ssh"
    const std::string& title() const { return m_title; }     // label or "ext4"
    const std::string& fsName() const { return m_fsName; }   // "ext4"
    bool caseSensitive() const { return m_reader->caseSensitive(); }
    stein::fs::Reader& reader() { return *m_reader; }

private:
    Browser() = default;
    std::unique_ptr<stein::fs::Reader> m_reader;
    std::vector<Crumb> m_crumbs;
    std::string m_title, m_fsName;
};

enum class PreviewKind { Empty, Text, Hex, Image, Unreadable };
struct Preview {
    PreviewKind kind = PreviewKind::Empty;
    std::vector<std::byte> bytes;  // what was read (up to the limit)
    std::string text;              // for Text
    std::string note;              // "first 256 KiB of 1.2 GB"
    bool truncated = false;
    std::string imageFormat;       // "png", "jpeg", "gif", "bmp"
};
stein::Expected<Preview> preview(stein::fs::Reader& reader, const stein::fs::Inode& file, const stein::fs::Stat& st, stein::ByteCount limit = 256 * stein::KiB);
stein::Expected<std::string> sha256Hex(stein::fs::Reader& reader, const stein::fs::Inode& file, const stein::fs::Stat& st, stein::Progress& progress);
// Copies `source` (a file, a directory tree or a symlink) to `destinationDir / name`.
stein::Expected<stein::fs::CopyTreeStats> copyOut(stein::fs::Reader& reader, const stein::fs::Inode& source, const std::string& name,
                                                  const std::filesystem::path& destinationDir, stein::Progress& progress);
std::string copyStatsText(const stein::fs::CopyTreeStats& stats);   // "12 files, 3 directories (1.2 MB)"

} // namespace drstein::core
