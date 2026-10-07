// SPDX-License-Identifier: MIT
#include "drstein/core/browse.hpp"

#include "drstein/core/format.hpp"
#include "stein/core/hash.hpp"
#include "stein/core/strings.hpp"

#include <algorithm>

namespace drstein::core {

using namespace stein;

Expected<Browser> Browser::open(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options) {
    const probe::Node* node = nodeAt(root, path);
    if (!node || !node->content) return fail(ErrorCategory::NotFound, "no filesystem at this node");
    const auto& info = node->content->info();
    if (!fs::has(node->content->capabilities(), fs::Capability::Read))
        return fail(ErrorCategory::Unsupported, std::string(fs::displayName(info.type)) + " cannot be read in-process yet");
    auto reader = node->content->openReader(options);
    if (!reader) return fail(reader.error());
    const std::string fsName(fs::displayName(info.type));
    std::string title = info.label.empty() ? fsName : info.label;
    if (!options.volume.empty()) title = options.volume;
    if (!options.snapshot.empty()) title += " @ " + options.snapshot;
    return fromReader(std::move(*reader), std::move(title), fsName);
}

Expected<Browser> Browser::fromReader(std::unique_ptr<fs::Reader> reader, std::string title, std::string fsName) {
    if (!reader) return fail(ErrorCategory::InvalidArgument, "no reader");
    auto rootInode = reader->root();
    if (!rootInode) return fail(rootInode.error());
    Browser b;
    b.m_reader = std::move(reader);
    b.m_crumbs.push_back({"/", *rootInode});
    b.m_title = std::move(title);
    b.m_fsName = std::move(fsName);
    return b;
}

Expected<Listing> Browser::list(const fs::Inode& dir) {
    auto entries = m_reader->readdir(dir);
    if (!entries) return fail(entries.error());
    Listing out;
    out.dir = dir;
    out.entries.reserve(entries->size());
    for (auto& e : *entries) {
        Entry x;
        x.entry = e;
        if (auto st = m_reader->stat(e.inode)) x.stat = *st;
        else {
            x.stat.type = e.type;
        }
        if (x.stat.type == fs::FileType::Symlink)
            if (auto t = m_reader->readlink(e.inode)) x.linkTarget = *t;
        x.sizeText = x.stat.type == fs::FileType::Directory || x.stat.type == fs::FileType::Symlink ? "—" : sizeBinary(x.stat.size);
        x.mtimeText = timeText(x.stat.mtime);
        x.modeText = core::modeText(x.stat);
        out.entries.push_back(std::move(x));
    }
    std::stable_sort(out.entries.begin(), out.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        return toLower(a.entry.name) < toLower(b.entry.name);
    });
    return out;
}

Expected<void> Browser::enter(std::string_view name) {
    auto inode = m_reader->lookup(current(), name);
    if (!inode) return fail(inode.error());
    auto st = m_reader->stat(*inode);
    if (!st) return fail(st.error());
    if (st->type != fs::FileType::Directory) return fail(ErrorCategory::InvalidArgument, std::string(name) + " is not a directory");
    m_crumbs.push_back({std::string(name), *inode});
    return {};
}

Expected<void> Browser::goTo(std::string_view absolutePath) {
    auto parts = split(absolutePath, '/');
    std::vector<Crumb> crumbs{m_crumbs.front()};
    fs::Inode dir = crumbs.front().inode;
    for (const auto& p : parts) {
        if (p.empty() || p == ".") continue;
        if (p == "..") {
            if (crumbs.size() > 1) crumbs.pop_back();
            dir = crumbs.back().inode;
            continue;
        }
        auto inode = m_reader->lookup(dir, p);
        if (!inode) return fail(inode.error());
        auto st = m_reader->stat(*inode);
        if (!st) return fail(st.error());
        if (st->type != fs::FileType::Directory) return fail(ErrorCategory::InvalidArgument, p + " is not a directory");
        crumbs.push_back({p, *inode});
        dir = *inode;
    }
    m_crumbs = std::move(crumbs);
    return {};
}

void Browser::up() {
    if (m_crumbs.size() > 1) m_crumbs.pop_back();
}

void Browser::jumpTo(std::size_t crumbIndex) {
    if (crumbIndex + 1 < m_crumbs.size()) m_crumbs.resize(crumbIndex + 1);
}

std::string Browser::pathText() const {
    if (m_crumbs.size() == 1) return "/";
    std::string s;
    for (std::size_t i = 1; i < m_crumbs.size(); ++i) s += "/" + m_crumbs[i].name;
    return s;
}

namespace {

std::string sniffImage(std::span<const std::byte> b) {
    auto at = [&](std::size_t i) { return i < b.size() ? std::to_integer<unsigned>(b[i]) : 0u; };
    if (b.size() >= 8 && at(0) == 0x89 && at(1) == 'P' && at(2) == 'N' && at(3) == 'G') return "png";
    if (b.size() >= 3 && at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF) return "jpeg";
    if (b.size() >= 6 && at(0) == 'G' && at(1) == 'I' && at(2) == 'F' && at(3) == '8') return "gif";
    if (b.size() >= 2 && at(0) == 'B' && at(1) == 'M') return "bmp";
    return "";
}

bool looksLikeText(std::span<const std::byte> b) {
    if (b.empty()) return false;
    std::size_t control = 0;
    for (std::byte x : b) {
        const auto c = std::to_integer<unsigned>(x);
        if (c == 0) return false;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != 0x1b) ++control;
    }
    if (control * 100 > b.size()) return false;
    return isValidUtf8(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())) || control == 0;
}

} // namespace

Expected<Preview> preview(fs::Reader& reader, const fs::Inode& file, const fs::Stat& st, ByteCount limit) {
    Preview p;
    if (st.type != fs::FileType::File) {
        p.kind = PreviewKind::Empty;
        p.note = st.type == fs::FileType::Directory ? "a directory" : st.type == fs::FileType::Symlink ? "a symbolic link" : "not a regular file";
        return p;
    }
    if (st.size == 0) {
        p.kind = PreviewKind::Empty;
        p.note = "empty file";
        return p;
    }
    const ByteCount want = std::min<ByteCount>(st.size, limit);
    p.bytes.assign(static_cast<std::size_t>(want), std::byte{0});
    auto n = reader.read(file, 0, p.bytes);
    if (!n) {
        p.kind = PreviewKind::Unreadable;
        p.note = n.error().message();
        return p;
    }
    p.bytes.resize(*n);
    p.truncated = st.size > want;
    if (p.truncated) p.note = "first " + sizeBinary(want) + " of " + sizeBinary(st.size);
    p.imageFormat = sniffImage(p.bytes);
    if (!p.imageFormat.empty() && !p.truncated) {
        p.kind = PreviewKind::Image;
        return p;
    }
    if (looksLikeText(p.bytes)) {
        p.kind = PreviewKind::Text;
        // A UTF-8 sequence cut by the limit would show as garbage: drop a dangling tail.
        std::string_view sv(reinterpret_cast<const char*>(p.bytes.data()), p.bytes.size());
        while (!sv.empty() && !isValidUtf8(sv)) sv.remove_suffix(1);
        p.text.assign(sv);
        return p;
    }
    p.kind = PreviewKind::Hex;
    return p;
}

Expected<std::string> sha256Hex(fs::Reader& reader, const fs::Inode& file, const fs::Stat& st, Progress& progress) {
    Sha256 hasher;
    std::vector<std::byte> buf(4 * MiB);
    progress.setPhase("Hashing", st.size, "bytes");
    std::uint64_t off = 0;
    while (off < st.size) {
        if (progress.isCancelled()) return fail(ErrorCategory::Cancelled, "hash cancelled");
        auto n = reader.read(file, off, buf);
        if (!n) return fail(n.error());
        if (*n == 0) break;
        hasher.update(std::span<const std::byte>(buf.data(), *n));
        off += *n;
        progress.advance(*n);
    }
    progress.finishPhase();
    return Hasher::hex(hasher.finish());
}

Expected<fs::CopyTreeStats> copyOut(fs::Reader& reader, const fs::Inode& source, const std::string& name, const std::filesystem::path& destinationDir, Progress& progress) {
    std::error_code ec;
    if (!std::filesystem::is_directory(destinationDir, ec)) return fail(ErrorCategory::NotFound, destinationDir.string() + " is not a directory");
    fs::CopyTreeOptions o;
    return fs::copyTree(reader, source, destinationDir / name, o, &progress);
}

std::string copyStatsText(const fs::CopyTreeStats& s) {
    std::string t = std::to_string(s.files) + (s.files == 1 ? " file" : " files");
    if (s.directories) t += ", " + std::to_string(s.directories) + (s.directories == 1 ? " directory" : " directories");
    if (s.symlinks) t += ", " + std::to_string(s.symlinks) + (s.symlinks == 1 ? " symlink" : " symlinks");
    t += " (" + sizeText(s.bytes) + ")";
    if (s.skipped) t += ", " + std::to_string(s.skipped) + " skipped";
    return t;
}

} // namespace drstein::core
