// SPDX-License-Identifier: MIT
// The file viewer: a Reader over any readable content node, a current
// directory with breadcrumbs, previews, hashes and copy-out. Read-only by
// construction (no Write capability exists in the library yet).
#pragma once

#include "drstein/core/topology.hpp"
#include "stein/core/progress.hpp"
#include "stein/fs/reader.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

// A reader of the content node at `path`: what Browser::open browses with. A Reader is not
// thread-safe, so whoever reads on another thread opens one of its own.
stein::Expected<std::unique_ptr<stein::fs::Reader>> openReader(const stein::probe::Node& root, const NodePath& path, const stein::fs::ReaderOptions& options = {});

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
    // Both return the name of the child directory that was left, for the list to re-select;
    // "" when nothing changed.
    std::string up();
    std::string jumpTo(std::size_t crumbIndex);               // breadcrumb click

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

// The row a keyboard step lands on: -1 for an empty list; from no selection a step down takes
// the first row and a step up the last; otherwise the step is clamped to the list.
int movedIndex(int current, int count, int delta);

enum class PreviewKind { Empty, Text, Hex, Image, Unreadable };
struct Preview {
    PreviewKind kind = PreviewKind::Empty;
    std::vector<std::byte> bytes;  // what was read: a whole image, otherwise the head
    std::string text;              // for Text, and the source of an SVG image; at most PreviewLimits::text
    std::string note;              // "first 256 KiB of 1.2 GiB"
    std::string textNote;          // "first 64 KiB of 300 KiB" when `text` is not the whole file; else empty
    bool truncated = false;
    std::string imageFormat;       // "png", "jpeg", "svg", ...: set whenever the content was recognised
};

struct PreviewLimits {
    stein::ByteCount head = 256 * stein::KiB;   // read first and sniffed; all a text or hex preview shows
    stein::ByteCount image = 64 * stein::MiB;   // a recognised image up to this size is read whole
    stein::ByteCount svg = 1 * stein::MiB;      // an SVG up to this size is read whole and vetted
    stein::ByteCount chunk = 1 * stein::MiB;    // read size past the head; cancellation is checked per chunk
    stein::ByteCount text = 64 * stein::KiB;    // the most of a text that is shown: more stalls a text view on every file
};

// What the file is shown as. A recognised image within limits.image comes back whole (Image);
// above it the head is shown as Hex with a note. An SVG is an Image only when it is within
// limits.svg and svgSafeToRender(); otherwise it stays Text. Text shows at most limits.text
// bytes, and the note says so when that is not the whole file. A read failure is a Preview of
// kind Unreadable with the message as its note; the only error returned is Cancelled, when
// `cancelled` turns true before a read.
stein::Expected<Preview> preview(stein::fs::Reader& reader, const stein::fs::Inode& file, const stein::fs::Stat& st, const PreviewLimits& limits = {},
                                 const std::atomic<bool>* cancelled = nullptr);

// The image format the first bytes of a file announce, as a lowercase id: png jpeg gif bmp webp
// tiff ico cur icns heic avif jp2 j2k psd pnm xpm; "" when none. Formats without a magic number
// (TGA) and text formats (SVG) are not found here.
std::string sniffImageFormat(std::span<const std::byte> head);
// "jpeg" -> "JPEG", "jp2" -> "JPEG 2000"; an unknown id comes back in upper case.
std::string imageFormatLabel(std::string_view id);
// Whether an SVG can be handed to a renderer that resolves references: false when it declares
// entities, carries scripts, event attributes, <image> or <foreignObject> (under any namespace
// prefix: the renderer goes by the local name), imports a style
// sheet, moves its base (xml:base), or points anywhere but at one of its own elements (href,
// xlink:href, url()). A hostile file on the browsed disk must not make the renderer open files
// of the host. Conservative: harmless files are refused too when they merely look like any of that.
bool svgSafeToRender(std::string_view svg);

stein::Expected<std::string> sha256Hex(stein::fs::Reader& reader, const stein::fs::Inode& file, const stein::fs::Stat& st, stein::Progress& progress);
// Copies `source` (a file, a directory tree or a symlink) to `destinationDir / name`.
stein::Expected<stein::fs::CopyTreeStats> copyOut(stein::fs::Reader& reader, const stein::fs::Inode& source, const std::string& name,
                                                  const std::filesystem::path& destinationDir, stein::Progress& progress);
std::string copyStatsText(const stein::fs::CopyTreeStats& stats);   // "12 files, 3 directories (1.2 MB)"

} // namespace drstein::core
