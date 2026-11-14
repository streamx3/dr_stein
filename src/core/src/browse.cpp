// SPDX-License-Identifier: MIT
#include "drstein/core/browse.hpp"

#include "drstein/core/format.hpp"
#include "stein/core/hash.hpp"
#include "stein/core/strings.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace drstein::core {

using namespace stein;
using namespace std::string_view_literals;

Expected<std::unique_ptr<fs::Reader>> openReader(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options) {
    const probe::Node* node = nodeAt(root, path);
    if (!node || !node->content) return fail(ErrorCategory::NotFound, "no filesystem at this node");
    if (!fs::has(node->content->capabilities(), fs::Capability::Read))
        return fail(ErrorCategory::Unsupported, std::string(fs::displayName(node->content->info().type)) + " cannot be read in-process yet");
    return node->content->openReader(options);
}

Expected<Browser> Browser::open(const probe::Node& root, const NodePath& path, const fs::ReaderOptions& options) {
    auto reader = openReader(root, path, options);
    if (!reader) return fail(reader.error());
    const auto& info = nodeAt(root, path)->content->info();
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

std::string Browser::up() {
    if (m_crumbs.size() < 2) return {};
    std::string left = std::move(m_crumbs.back().name);
    m_crumbs.pop_back();
    return left;
}

std::string Browser::jumpTo(std::size_t crumbIndex) {
    if (crumbIndex >= m_crumbs.size() - 1) return {};
    std::string left = std::move(m_crumbs[crumbIndex + 1].name);
    m_crumbs.resize(crumbIndex + 1);
    return left;
}

std::string Browser::pathText() const {
    if (m_crumbs.size() == 1) return "/";
    std::string s;
    for (std::size_t i = 1; i < m_crumbs.size(); ++i) s += "/" + m_crumbs[i].name;
    return s;
}

int movedIndex(int current, int count, int delta) {
    if (count <= 0) return -1;
    if (current < 0) return delta > 0 ? 0 : delta < 0 ? count - 1 : -1;
    return static_cast<int>(std::clamp<long long>(static_cast<long long>(current) + delta, 0, count - 1));
}

namespace {

bool isSpace(unsigned c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string_view asText(std::span<const std::byte> b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

// The head of a file, read without running past its end: bytes beyond it read as zero.
struct Head {
    std::span<const std::byte> b;

    std::size_t size() const { return b.size(); }
    unsigned at(std::size_t i) const { return i < b.size() ? std::to_integer<unsigned>(b[i]) : 0u; }
    unsigned le16(std::size_t i) const { return at(i) | at(i + 1) << 8; }
    std::uint32_t le32(std::size_t i) const { return le16(i) | static_cast<std::uint32_t>(le16(i + 2)) << 16; }
    std::uint32_t be32(std::size_t i) const { return static_cast<std::uint32_t>(at(i)) << 24 | at(i + 1) << 16 | at(i + 2) << 8 | at(i + 3); }
    bool has(std::size_t offset, std::string_view magic) const {
        return offset + magic.size() <= b.size() && std::memcmp(b.data() + offset, magic.data(), magic.size()) == 0;
    }
    std::string_view text(std::size_t offset, std::size_t length) const { return asText(b).substr(offset, length); }
};

// "BM" opens plenty of files that are no bitmap: the reserved words, the DIB header size and
// the plane and depth fields have to agree too.
bool plausibleBmp(const Head& h) {
    if (!h.has(0, "BM") || h.size() < 26 || h.le16(6) != 0 || h.le16(8) != 0) return false;
    const std::uint32_t dib = h.le32(14);
    if (h.le32(10) < 14 + std::uint64_t{dib}) return false;
    std::uint32_t width = 0, height = 0;
    unsigned planes = 0, depth = 0;
    if (dib == 12) {
        width = h.le16(18);
        height = h.le16(20);
        planes = h.le16(22);
        depth = h.le16(24);
    } else if (dib == 16 || dib == 40 || dib == 52 || dib == 56 || dib == 64 || dib == 108 || dib == 124) {
        if (h.size() < 30) return false;
        width = h.le32(18);
        height = h.le32(22);
        planes = h.le16(26);
        depth = h.le16(28);
    } else {
        return false;
    }
    return width != 0 && height != 0 && planes == 1 && (depth == 1 || depth == 4 || depth == 8 || depth == 16 || depth == 24 || depth == 32);
}

// 00 00 01 00 is also how an MPEG stream starts: the first directory entry has to make sense.
std::string sniffIcon(const Head& h) {
    if (h.size() < 22 || h.le16(0) != 0) return "";
    const unsigned type = h.le16(2), count = h.le16(4);
    if ((type != 1 && type != 2) || count == 0 || count > 256) return "";
    if (h.at(9) != 0 || h.le32(14) == 0 || h.le32(18) < 6 + 16 * count) return "";
    if (type == 1 && (h.le16(10) > 1 || h.le16(12) > 32)) return "";
    return type == 1 ? "ico" : "cur";
}

// ISO base media files (HEIF, AVIF, but MP4 and QuickTime too) say what they are in the brands
// of their first box.
std::string sniffIsoBrand(const Head& h) {
    if (!h.has(4, "ftyp") || h.size() < 16 || h.be32(0) < 16) return "";
    auto codec = [](std::string_view brand) -> std::string_view {
        if (brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx") return "heic";
        if (brand == "avif" || brand == "avis") return "avif";
        return "";
    };
    const std::string_view major = h.text(8, 4);
    if (auto id = codec(major); !id.empty()) return std::string(id);
    if (major != "mif1" && major != "msf1") return "";
    // The generic HEIF brands leave the codec to the compatible brands.
    const std::size_t end = std::min<std::size_t>(h.be32(0), h.size());
    for (std::size_t i = 16; i + 4 <= end; i += 4)
        if (auto id = codec(h.text(i, 4)); !id.empty()) return std::string(id);
    return "heic";
}

// "P5 is ..." opens a text file as well: a width and a height have to follow the magic.
bool plausiblePnm(const Head& h) {
    if (h.at(0) != 'P' || h.at(1) < '1' || h.at(1) > '6') return false;
    std::size_t i = 2;
    auto gap = [&] {
        const std::size_t start = i;
        while (i < h.size() && (isSpace(h.at(i)) || h.at(i) == '#')) {
            if (h.at(i) == '#')
                while (i < h.size() && h.at(i) != '\n') ++i;
            else ++i;
        }
        return i > start;
    };
    auto number = [&] {
        const std::size_t start = i;
        bool nonZero = false;
        while (i < h.size() && h.at(i) >= '0' && h.at(i) <= '9') nonZero |= h.at(i++) != '0';
        return i > start && i - start <= 9 && nonZero;
    };
    return gap() && number() && gap() && number() && i < h.size() && isSpace(h.at(i));
}

std::size_t doctypeEnd(std::string_view t) {
    char quote = 0;
    int depth = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            if (depth) --depth;
        } else if (c == '>' && depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

// Whether the first element of an XML text is <svg>: a byte order mark, white space, the XML
// declaration, processing instructions, comments and a DOCTYPE may come before it.
bool startsWithSvgElement(std::string_view t) {
    if (t.starts_with("\xEF\xBB\xBF")) t.remove_prefix(3);
    for (;;) {
        while (!t.empty() && isSpace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
        std::size_t end = 0, closer = 0;
        if (t.starts_with("<?")) {
            end = t.find("?>", 2);
            closer = 2;
        } else if (t.starts_with("<!--")) {
            end = t.find("-->", 4);
            closer = 3;
        } else if (iequals(t.substr(0, 9), "<!DOCTYPE")) {
            end = doctypeEnd(t);
            closer = 1;
        } else {
            break;
        }
        if (end == std::string_view::npos) return false;
        t.remove_prefix(end + closer);
    }
    return t.size() > 4 && t.starts_with("<svg") && (isSpace(static_cast<unsigned char>(t[4])) || t[4] == '>' || t[4] == '/');
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
    return isValidUtf8(asText(b)) || control == 0;
}

bool isCancelled(const std::atomic<bool>* cancelled) { return cancelled && cancelled->load(std::memory_order_relaxed); }

Error cancelledError() { return Error(ErrorCategory::Cancelled, "preview cancelled"); }

// Grows `bytes` towards the first `want` bytes of the file, one chunk at a time so that a
// superseded preview of a large picture stops soon. Ends short when the file does.
Expected<void> readOn(fs::Reader& reader, const fs::Inode& file, std::vector<std::byte>& bytes, ByteCount want, ByteCount chunk, const std::atomic<bool>* cancelled) {
    bytes.reserve(static_cast<std::size_t>(want));
    while (bytes.size() < want) {
        if (isCancelled(cancelled)) return fail(cancelledError());
        const std::size_t have = bytes.size();
        const auto piece = static_cast<std::size_t>(std::min<ByteCount>(want - have, chunk ? chunk : want));
        bytes.resize(have + piece);
        auto n = reader.read(file, have, std::span<std::byte>(bytes.data() + have, piece));
        bytes.resize(have + (n ? std::min(*n, piece) : 0));
        if (!n) return fail(n.error());
        if (*n == 0) break;
    }
    return {};
}

std::string endsEarlyNote(ByteCount got, ByteCount size) { return "the file ends early: " + sizeBinary(got) + " of " + sizeBinary(size) + " could be read"; }

void addNote(std::string& note, const std::string& more) { note += (note.empty() ? "" : " · ") + more; }

// The first `limit` bytes of a text. A UTF-8 sequence cut there would show as garbage: the
// dangling tail, three bytes at most, is dropped. Text in another encoding stays as it is.
std::string_view textHead(std::string_view t, ByteCount limit) {
    if (t.size() > limit) t = t.substr(0, static_cast<std::size_t>(limit));
    for (std::size_t cut = 0; cut <= 3 && cut < t.size(); ++cut)
        if (isValidUtf8(t.substr(0, t.size() - cut))) return t.substr(0, t.size() - cut);
    return t;
}

// What a preview shows as text: `available` is what was read of a file of `size` bytes.
void setText(Preview& p, std::string_view available, ByteCount size, ByteCount limit) {
    p.text.assign(textHead(available, limit));
    const ByteCount considered = std::min<ByteCount>(available.size(), limit);
    p.textNote = considered < size ? "first " + sizeBinary(considered) + " of " + sizeBinary(size) : "";
}

} // namespace

std::string sniffImageFormat(std::span<const std::byte> head) {
    const Head h{head};
    if (h.has(0, "\x89PNG\r\n\x1a\n")) return "png";
    if (h.has(0, "\xFF\xD8\xFF")) return "jpeg";
    if (h.has(0, "GIF87a") || h.has(0, "GIF89a")) return "gif";
    if (h.has(0, "RIFF") && h.has(8, "WEBP")) return "webp";
    if ((h.has(0, "II*\0"sv) && h.le32(4) >= 8) || (h.has(0, "MM\0*"sv) && h.be32(4) >= 8)) return "tiff";
    if (h.has(0, "icns") && h.be32(4) >= 8) return "icns";
    if (h.has(0, "8BPS") && (h.has(4, "\0\x01"sv) || h.has(4, "\0\x02"sv))) return "psd";
    if (h.has(0, "\0\0\0\x0CjP  \r\n\x87\n"sv)) return "jp2";
    if (h.has(0, "\xFF\x4F\xFF\x51")) return "j2k";
    if (auto id = sniffIsoBrand(h); !id.empty()) return id;
    if (plausibleBmp(h)) return "bmp";
    if (auto id = sniffIcon(h); !id.empty()) return id;
    if (h.has(0, "/* XPM */")) return "xpm";
    if (plausiblePnm(h)) return "pnm";
    return "";
}

std::string imageFormatLabel(std::string_view id) {
    static constexpr std::pair<std::string_view, std::string_view> labels[] = {
        {"jpeg", "JPEG"}, {"webp", "WebP"}, {"jp2", "JPEG 2000"}, {"j2k", "JPEG 2000"},
    };
    for (const auto& [key, label] : labels)
        if (id == key) return std::string(label);
    return toUpper(id);
}

bool svgSafeToRender(std::string_view svg) {
    const std::string s = toLower(svg);
    for (std::string_view banned : {"<!entity", "@import", "xml:base"})
        if (s.find(banned) != std::string::npos) return false;

    auto at = [&](std::size_t i) { return i < s.size() ? static_cast<unsigned char>(s[i]) : 0u; };
    auto inName = [](unsigned c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':' || c >= 0x80; };
    // Elements by their local name: QtSvg ignores the prefix, so <s:image> bound to the SVG
    // namespace is an <image> too, and draws a file named by a "#..." reference.
    for (auto pos = s.find('<'); pos != std::string::npos; pos = s.find('<', pos + 1)) {
        std::size_t end = pos + 1;
        while (inName(at(end))) ++end;
        std::string_view name(s.data() + pos + 1, end - pos - 1);
        if (const auto colon = name.rfind(':'); colon != std::string_view::npos) name.remove_prefix(colon + 1);
        if (name == "script" || name == "image" || name == "foreignobject") return false;
    }
    // What a reference names, quotes and white space skipped: only "#id" stays inside the file.
    auto pointsInside = [&](std::size_t i) {
        while (isSpace(at(i))) ++i;
        if (at(i) == '"' || at(i) == '\'') ++i;
        while (isSpace(at(i))) ++i;
        return at(i) == '#';
    };
    for (auto pos = s.find("href"); pos != std::string::npos; pos = s.find("href", pos + 4)) {
        std::size_t i = pos + 4;
        while (isSpace(at(i))) ++i;
        if (at(i) == '=' && !pointsInside(i + 1)) return false;
    }
    for (auto pos = s.find("url("); pos != std::string::npos; pos = s.find("url(", pos + 4))
        if (!pointsInside(pos + 4)) return false;

    // Event attributes: a name that starts with "on".
    for (auto pos = s.find("on"); pos != std::string::npos; pos = s.find("on", pos + 2)) {
        if (pos > 0 && inName(at(pos - 1))) continue;
        std::size_t i = pos + 2;
        while (at(i) >= 'a' && at(i) <= 'z') ++i;
        if (i == pos + 2) continue;
        while (isSpace(at(i))) ++i;
        if (at(i) == '=') return false;
    }
    return true;
}

Expected<Preview> preview(fs::Reader& reader, const fs::Inode& file, const fs::Stat& st, const PreviewLimits& limits, const std::atomic<bool>* cancelled) {
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
    if (isCancelled(cancelled)) return fail(cancelledError());
    auto unreadable = [&](const Error& error) {
        p.kind = PreviewKind::Unreadable;
        p.note = error.message();
        p.truncated = p.bytes.size() < st.size;
        return std::move(p);
    };
    const ByteCount want = std::min<ByteCount>(st.size, limits.head);
    p.bytes.assign(static_cast<std::size_t>(want), std::byte{0});
    auto n = reader.read(file, 0, p.bytes);
    p.bytes.resize(n ? std::min(*n, p.bytes.size()) : 0);
    if (!n) return unreadable(n.error());
    p.truncated = st.size > want;
    if (p.truncated) p.note = "first " + sizeBinary(want) + " of " + sizeBinary(st.size);

    p.imageFormat = sniffImageFormat(p.bytes);
    if (!p.imageFormat.empty()) {
        if (st.size > limits.image) {
            p.kind = PreviewKind::Hex;
            p.note = imageFormatLabel(p.imageFormat) + " image of " + sizeBinary(st.size) + ": above the " + sizeBinary(limits.image) + " preview limit";
            return p;
        }
        if (auto rest = readOn(reader, file, p.bytes, st.size, limits.chunk, cancelled); !rest) {
            if (rest.error().category() == ErrorCategory::Cancelled) return fail(rest.error());
            return unreadable(rest.error());
        }
        p.kind = PreviewKind::Image;
        p.truncated = false;
        p.note = p.bytes.size() < st.size ? endsEarlyNote(p.bytes.size(), st.size) : "";
        return p;
    }

    if (!looksLikeText(p.bytes)) {
        p.kind = PreviewKind::Hex;
        return p;
    }
    p.kind = PreviewKind::Text;
    setText(p, asText(p.bytes), st.size, limits.text);
    p.truncated = !p.textNote.empty();
    p.note = p.textNote;
    if (!startsWithSvgElement(textHead(asText(p.bytes), p.bytes.size()))) return p;

    p.imageFormat = "svg";
    if (st.size > limits.svg) {
        addNote(p.note, "SVG image above the " + sizeBinary(limits.svg) + " preview limit: shown as text");
        return p;
    }
    const std::size_t headSize = p.bytes.size();
    if (auto rest = readOn(reader, file, p.bytes, st.size, limits.chunk, cancelled); !rest) {
        if (rest.error().category() == ErrorCategory::Cancelled) return fail(rest.error());
        return unreadable(rest.error());
    }
    const std::string_view whole = asText(p.bytes);
    const bool text = looksLikeText(p.bytes);
    if (text && svgSafeToRender(whole)) {
        p.kind = PreviewKind::Image;
        setText(p, whole, st.size, limits.text);
        p.truncated = false;
        p.note = p.bytes.size() < st.size ? endsEarlyNote(p.bytes.size(), st.size) : "";
        return p;
    }
    p.bytes.resize(headSize);
    addNote(p.note, text ? "SVG image with scripts or references outside the file: shown as text" : "SVG image with binary data in it: shown as text");
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
