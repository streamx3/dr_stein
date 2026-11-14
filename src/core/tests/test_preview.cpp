// SPDX-License-Identifier: MIT
#include "drstein/core/browse.hpp"
#include "drstein/core/format.hpp"
#include "drstein/core/preview_loader.hpp"
#include "fixtures.hpp"
#include "stein/mount/nfs_server.hpp"

#include <doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <future>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using namespace drstein::core;
using namespace stein;
using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

std::vector<std::byte> bytesOf(std::string_view s) {
    std::vector<std::byte> out(s.size());
    if (!s.empty()) std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// `magic` followed by filler that is neither text nor a format of its own.
std::vector<std::byte> binary(std::string_view magic, std::size_t size) {
    std::vector<std::byte> out(std::max(size, magic.size()));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::byte((i * 31 + 7) & 0xFF);
    if (!magic.empty()) std::memcpy(out.data(), magic.data(), magic.size());
    return out;
}

std::string le16(unsigned v) { return {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)}; }
std::string le32(std::uint32_t v) { return le16(v & 0xFFFF) + le16(v >> 16); }
std::string be32(std::uint32_t v) { return {static_cast<char>(v >> 24), static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 8) & 0xFF), static_cast<char>(v & 0xFF)}; }

const std::string kPngMagic = "\x89PNG\r\n\x1a\n";

std::string bmpHeader(std::uint32_t dib = 40, unsigned planes = 1, unsigned depth = 24, unsigned reserved = 0) {
    std::string h = "BM" + le32(1000) + le16(reserved) + le16(0) + le32(14 + dib) + le32(dib);
    if (dib == 12) return h + le16(16) + le16(16) + le16(planes) + le16(depth);
    return h + le32(16) + le32(16) + le16(planes) + le16(depth) + std::string(dib > 16 ? dib - 16 : 0, '\0');
}

std::string iconHeader(unsigned type, unsigned count, std::uint32_t firstOffset, unsigned reserved = 0, std::uint32_t firstSize = 4096) {
    return le16(0) + le16(type) + le16(count) + std::string{32, 32, 0, static_cast<char>(reserved)} + le16(1) + le16(32) + le32(firstSize) + le32(firstOffset);
}

std::string ftyp(std::string_view major, std::initializer_list<std::string_view> compatible = {}) {
    std::string brands;
    for (auto c : compatible) brands += c;
    return be32(static_cast<std::uint32_t>(16 + brands.size())) + "ftyp" + std::string(major) + be32(0) + brands;
}

// One file served from memory that counts its reads, and can fail one or call out before one.
class ScriptedReader final : public fs::Reader {
public:
    struct Read {
        std::uint64_t offset = 0;
        std::size_t size = 0;
        bool operator==(const Read&) const = default;
    };

    explicit ScriptedReader(std::vector<std::byte> content) : m_inner(mount::makeSingleFileReader(std::make_shared<MemoryDevice>(std::move(content)), "file")) {}

    Expected<fs::Inode> root() const override { return m_inner->root(); }
    Expected<fs::Inode> lookup(const fs::Inode& dir, std::string_view name) override { return m_inner->lookup(dir, name); }
    Expected<fs::Stat> stat(const fs::Inode& inode) override { return m_inner->stat(inode); }
    Expected<std::vector<fs::DirEntry>> readdir(const fs::Inode& dir) override { return m_inner->readdir(dir); }
    Expected<std::string> readlink(const fs::Inode& link) override { return m_inner->readlink(link); }
    Expected<std::size_t> read(const fs::Inode& inode, std::uint64_t offset, std::span<std::byte> dst) override {
        const int call = static_cast<int>(reads.size());
        reads.push_back({offset, dst.size()});
        if (beforeRead) beforeRead(call);
        if (call == failAt) return fail(ErrorCategory::Io, "sector 7 is unreadable");
        return m_inner->read(inode, offset, dst);
    }

    fs::Inode file() { return *m_inner->lookup(*m_inner->root(), "file"); }
    fs::Stat fileStat() { return *m_inner->stat(file()); }

    std::vector<Read> reads;
    int failAt = -1;
    std::function<void(int)> beforeRead;

private:
    std::unique_ptr<fs::Reader> m_inner;
};

Preview previewOf(std::string_view content, const PreviewLimits& limits = {}) {
    ScriptedReader reader(bytesOf(content));
    auto pv = preview(reader, reader.file(), reader.fileStat(), limits);
    REQUIRE(pv);
    return std::move(*pv);
}

std::string_view textOf(const std::vector<std::byte>& bytes) { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }

const PreviewLimits kSmall{.head = 64, .image = 4096, .svg = 4096, .chunk = 100};

} // namespace

TEST_CASE("image formats are told by their magic numbers, and near misses are not") {
    struct Case {
        const char* what;
        std::string head;
        const char* id;
    };
    const Case cases[] = {
        {"PNG", kPngMagic + "\0\0\0\rIHDR"s, "png"},
        {"JPEG", "\xFF\xD8\xFF\xE0\0\x10JFIF"s, "jpeg"},
        {"GIF 87a", "GIF87a\x10\0\x10\0"s, "gif"},
        {"GIF 89a", "GIF89a\x10\0\x10\0"s, "gif"},
        {"BMP, Windows header", bmpHeader(), "bmp"},
        {"BMP, OS/2 core header", bmpHeader(12, 1, 8), "bmp"},
        {"BMP, V5 header", bmpHeader(124, 1, 32), "bmp"},
        {"WebP", "RIFF" + le32(100) + "WEBPVP8 ", "webp"},
        {"TIFF, little endian", "II*\0"s + le32(8), "tiff"},
        {"TIFF, big endian", "MM\0*"s + be32(8), "tiff"},
        {"ICO with three pictures", iconHeader(1, 3, 6 + 3 * 16), "ico"},
        {"CUR", iconHeader(2, 1, 22), "cur"},
        {"ICNS", "icns" + be32(1024) + "ic07" + be32(1016), "icns"},
        {"ICNS with a table of contents first", "icns" + be32(4096) + "TOC " + be32(16), "icns"},
        {"ICNS with no pictures", "icns" + be32(8), "icns"},
        {"HEIC", ftyp("heic", {"mif1", "heic"}), "heic"},
        {"HEIC, 10 bit", ftyp("heix"), "heic"},
        {"HEIF sequence", ftyp("msf1", {"msf1", "hevc"}), "heic"},
        {"generic HEIF with HEVC pictures", ftyp("mif1", {"mif1", "heic"}), "heic"},
        {"generic HEIF, no codec named", ftyp("mif1"), "heic"},
        {"generic HEIF with AV1 pictures", ftyp("mif1", {"mif1", "avif", "miaf"}), "avif"},
        {"AVIF", ftyp("avif", {"avif", "mif1"}), "avif"},
        {"AVIF sequence", ftyp("avis"), "avif"},
        {"JPEG 2000 file", "\0\0\0\x0CjP  \r\n\x87\n"s + "\0\0\0\x14" "ftypjp2 "s, "jp2"},
        {"JPEG 2000 codestream", "\xFF\x4F\xFF\x51\0\x2F"s, "j2k"},
        {"PSD", "8BPS\0\x01\0\0\0\0\0\0"s, "psd"},
        {"PSB", "8BPS\0\x02\0\0\0\0\0\0"s, "psd"},
        {"PPM", "P6\n640 480\n255\n", "pnm"},
        {"PBM with a comment", "P1\n# a comment\n2 2\n0 1 1 0\n", "pnm"},
        {"PGM on one line", "P5 3 3 255\n", "pnm"},
        {"XPM", "/* XPM */\nstatic char *x[] = {", "xpm"},

        {"nothing", "", ""},
        {"plain text", "hello, world\n", ""},
        {"PNG signature cut short", "\x89PNG\r\n\x1a", ""},
        {"PNG letters, wrong signature", "\x89PNGxxxxIHDR", ""},
        {"two bytes of a JPEG", "\xFF\xD8", ""},
        {"JPEG end marker first", "\xFF\xD9\xFF\xE0", ""},
        {"GIF of no version", "GIF88a\x10\0\x10\0"s, ""},
        {"BM and garbage", std::string(textOf(binary("BM", 64))), ""},
        {"BM and zeros", "BM" + std::string(62, '\0'), ""},
        {"BMP with no planes", bmpHeader(40, 0, 24), ""},
        {"BMP of 7 bits", bmpHeader(40, 1, 7), ""},
        {"BMP with an unknown header size", bmpHeader(41), ""},
        {"BMP with its reserved word in use", bmpHeader(40, 1, 24, 0x4D42), ""},
        {"BMP header cut short", bmpHeader().substr(0, 20), ""},
        {"WAV", "RIFF" + le32(100) + "WAVEfmt ", ""},
        {"AVI", "RIFF" + le32(100) + "AVI LIST", ""},
        {"TIFF with no directory", "II*\0"s + le32(0), ""},
        {"TIFF bytes in the wrong order", "MM*\0"s + be32(8), ""},
        {"BigTIFF", "II+\0"s + le32(8), ""},
        {"ICO with no pictures", iconHeader(1, 0, 22), ""},
        {"ICO whose picture starts inside the directory", iconHeader(1, 3, 22), ""},
        {"ICO with its reserved byte set", iconHeader(1, 1, 22, 1), ""},
        {"ICO whose picture is empty", iconHeader(1, 1, 22, 0, 0), ""},
        {"icon header of a third kind", iconHeader(3, 1, 22), ""},
        {"MPEG program stream", "\0\0\x01\xBA"s + std::string(textOf(binary("", 32))), ""},
        {"TGA, which has no magic", "\0\0\x02\0\0\0\0\0\0\0\0\0"s + le16(64) + le16(64) + "\x18\0"s + std::string(16, '\x7F'), ""},
        {"ICNS cut short", "icns", ""},
        {"a note that starts with icns", "icns notes: how the icons were made\n", ""},
        {"icns, a newline, then words", "icns\nnotes about the icons\n", ""},
        {"ICNS whose first entry runs past its end", "icns" + be32(64) + "ic07" + be32(100), ""},
        {"ICNS whose first entry has no type", "icns" + be32(64) + "\x01\x02\x03\x04"s + be32(16), ""},
        {"MP4", ftyp("isom", {"isom", "mp42"}), ""},
        {"QuickTime", ftyp("qt  "), ""},
        {"MP4 that lists a HEIF brand among others", ftyp("mp42", {"mp42", "heic"}), ""},
        {"ftyp box too small to hold a brand", be32(8) + "ftypheic" + be32(0), ""},
        {"JPEG 2000 signature, damaged", "\0\0\0\x0CjP  \r\n\x87\r"s, ""},
        {"JPEG 2000 codestream, wrong marker", "\xFF\x4F\xFF\x52", ""},
        {"8BPS of a third version", "8BPS\0\x03\0\0\0\0\0\0"s, ""},
        {"a sentence that opens like a PGM", "P5 is the fifth chapter\n", ""},
        {"PAM", "P7\nWIDTH 2\nHEIGHT 2\n", ""},
        {"PPM of no width", "P6\n0 480\n255\n", ""},
        {"PPM that stops after its width", "P6 640", ""},
        {"PPM that stops in its height", "P6\n640 480", ""},
        {"P and a letter", "PK\x03\x04", ""},
        {"XPM comment, unfinished", "/* XPM", ""},
    };
    for (const auto& c : cases) {
        CAPTURE(c.what);
        CHECK(sniffImageFormat(bytesOf(c.head)) == c.id);
    }

    CHECK(imageFormatLabel("jpeg") == "JPEG");
    CHECK(imageFormatLabel("png") == "PNG");
    CHECK(imageFormatLabel("heic") == "HEIC");
    CHECK(imageFormatLabel("webp") == "WebP");
    CHECK(imageFormatLabel("jp2") == "JPEG 2000");
    CHECK(imageFormatLabel("j2k") == "JPEG 2000");
    CHECK(imageFormatLabel("svg") == "SVG");
    CHECK(imageFormatLabel("tga") == "TGA");
    CHECK(imageFormatLabel("") == "");
}

TEST_CASE("a picture larger than the head is read whole, in chunks") {
    CHECK(PreviewLimits{}.head == 256 * KiB);
    CHECK(PreviewLimits{}.image == 64 * MiB);
    CHECK(PreviewLimits{}.svg == 512 * KiB);
    CHECK(PreviewLimits{}.chunk == 1 * MiB);

    const auto content = binary(kPngMagic, 1000);
    ScriptedReader reader(content);
    auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
    REQUIRE(pv);
    CHECK(pv->kind == PreviewKind::Image);
    CHECK(pv->imageFormat == "png");
    CHECK((pv->bytes == content));
    CHECK(!pv->truncated);
    CHECK(pv->note == "");
    std::vector<ScriptedReader::Read> expected{{0, 64}};
    for (std::uint64_t off = 64; off < 1000; off += 100) expected.push_back({off, static_cast<std::size_t>(std::min<std::uint64_t>(100, 1000 - off))});
    CHECK(expected.size() == 11);
    CHECK((reader.reads == expected));

    SUBCASE("with the limits the application uses, a 300 KiB photograph is a picture, not a hex dump") {
        const auto photo = binary("\xFF\xD8\xFF\xE1", 300 * KiB);
        ScriptedReader big(photo);
        auto whole = preview(big, big.file(), big.fileStat());
        REQUIRE(whole);
        CHECK(whole->kind == PreviewKind::Image);
        CHECK(whole->imageFormat == "jpeg");
        CHECK((whole->bytes == photo));
        CHECK(!whole->truncated);
        CHECK((big.reads == std::vector<ScriptedReader::Read>{{0, 256 * KiB}, {256 * KiB, 44 * KiB}}));
    }
    SUBCASE("a picture that fits the head costs one read") {
        ScriptedReader tiny(binary(kPngMagic, 40));
        auto one = preview(tiny, tiny.file(), tiny.fileStat(), kSmall);
        REQUIRE(one);
        CHECK(one->kind == PreviewKind::Image);
        CHECK(one->bytes.size() == 40);
        CHECK(tiny.reads.size() == 1);
    }
    SUBCASE("a chunk size of zero reads the rest in one piece") {
        ScriptedReader atOnce(content);
        auto all = preview(atOnce, atOnce.file(), atOnce.fileStat(), PreviewLimits{.head = 64, .image = 4096, .svg = 4096, .chunk = 0});
        REQUIRE(all);
        CHECK(all->kind == PreviewKind::Image);
        CHECK((all->bytes == content));
        CHECK((atOnce.reads == std::vector<ScriptedReader::Read>{{0, 64}, {64, 936}}));
    }
}

TEST_CASE("a picture above the image limit stays a hex dump of its head, and says why") {
    SUBCASE("exactly at the limit it is still read") {
        ScriptedReader reader(binary(kPngMagic, 4096));
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Image);
        CHECK(pv->bytes.size() == 4096);
    }
    SUBCASE("above it nothing past the head is read") {
        ScriptedReader reader(binary(kPngMagic, 6 * KiB));
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Hex);
        CHECK(pv->truncated);
        CHECK(pv->imageFormat == "png");
        CHECK(pv->bytes.size() == 64);
        CHECK(pv->note == "PNG image of 6 KiB: above the 4 KiB preview limit");
        CHECK(reader.reads.size() == 1);
    }
}

TEST_CASE("a cancelled preview stops before its next read") {
    const auto content = binary(kPngMagic, 1000);
    SUBCASE("in the middle of a picture") {
        ScriptedReader reader(content);
        std::atomic<bool> cancelled{false};
        reader.beforeRead = [&](int call) {
            if (call == 3) cancelled = true;
        };
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall, &cancelled);
        REQUIRE(!pv);
        CHECK(pv.error().category() == ErrorCategory::Cancelled);
        CHECK(reader.reads.size() == 4);   // of the 11 a whole read takes
    }
    SUBCASE("before the first one") {
        ScriptedReader reader(content);
        std::atomic<bool> cancelled{true};
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall, &cancelled);
        REQUIRE(!pv);
        CHECK(pv.error().category() == ErrorCategory::Cancelled);
        CHECK(reader.reads.empty());
    }
    SUBCASE("a flag that stays down changes nothing") {
        ScriptedReader reader(content);
        std::atomic<bool> cancelled{false};
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall, &cancelled);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Image);
        CHECK(reader.reads.size() == 11);
    }
}

TEST_CASE("a picture that ends early or cannot be read is reported as such") {
    SUBCASE("shorter on disk than its size says: what there is, with a note") {
        const auto content = binary(kPngMagic, 500);
        ScriptedReader reader(content);
        fs::Stat st = reader.fileStat();
        st.size = 1000;
        auto pv = preview(reader, reader.file(), st, kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Image);
        CHECK((pv->bytes == content));
        CHECK(pv->note == "the file ends early: 500 B of 1000 B could be read");
        CHECK(reader.reads.back() == ScriptedReader::Read{500, 100});   // the read that found the end
    }
    SUBCASE("a chunk fails: unreadable, with the reason and what came before it") {
        ScriptedReader reader(binary(kPngMagic, 1000));
        reader.failAt = 2;
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Unreadable);
        CHECK(pv->note == "sector 7 is unreadable");
        CHECK(pv->imageFormat == "png");
        CHECK(pv->bytes.size() == 164);
        CHECK(pv->truncated);
        CHECK(reader.reads.size() == 3);
    }
    SUBCASE("the head fails: unreadable and nothing to show") {
        ScriptedReader reader(binary(kPngMagic, 1000));
        reader.failAt = 0;
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Unreadable);
        CHECK(pv->note == "sector 7 is unreadable");
        CHECK(pv->bytes.empty());
    }
}

TEST_CASE("what is not a picture is text or hex, from the head alone") {
    SUBCASE("things that are not files, and empty ones") {
        ScriptedReader reader(bytesOf("x"));
        fs::Stat st;
        st.type = fs::FileType::Directory;
        CHECK(preview(reader, reader.file(), st)->note == "a directory");
        st.type = fs::FileType::Symlink;
        CHECK(preview(reader, reader.file(), st)->note == "a symbolic link");
        st.type = fs::FileType::Fifo;
        CHECK(preview(reader, reader.file(), st)->note == "not a regular file");
        st.type = fs::FileType::File;
        st.size = 0;
        auto empty = preview(reader, reader.file(), st);
        REQUIRE(empty);
        CHECK(empty->kind == PreviewKind::Empty);
        CHECK(empty->note == "empty file");
        CHECK(reader.reads.empty());
    }
    SUBCASE("binary: hex, cut at the head") {
        ScriptedReader reader(binary("", 1000));
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Hex);
        CHECK(pv->truncated);
        CHECK(pv->bytes.size() == 64);
        CHECK(pv->imageFormat == "");
        CHECK(pv->note == "first 64 B of 1000 B");
        CHECK(reader.reads.size() == 1);
    }
    SUBCASE("text: a character cut by the limit is dropped, not shown as garbage") {
        const std::string text = std::string(62, 'a') + "\xC3\xA9" + std::string(100, 'b');
        const auto pv = previewOf(text, PreviewLimits{.head = 63});
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.truncated);
        CHECK(pv.bytes.size() == 63);
        CHECK(pv.text == std::string(62, 'a'));
        CHECK(pv.note == "first 63 B of 164 B");
    }
    SUBCASE("text in an older encoding is shown whole, not cut at its first accent") {
        std::string latin1;
        for (int i = 0; i < 2000; ++i) latin1 += "caf\xE9 au lait\n";
        const auto pv = previewOf(latin1);
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.text == latin1);
    }
    SUBCASE("a long text shows its first 64 KiB, and the note says so") {
        CHECK(PreviewLimits{}.text == 64 * KiB);
        const std::string log = std::string(64 * KiB - 1, 'x') + "\xC3\xA9" + std::string(100 * KiB, 'y');
        const auto pv = previewOf(log);
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.truncated);
        CHECK(pv.bytes.size() == 164 * KiB + 1);   // the head is read and sniffed as before
        CHECK(pv.text == std::string(64 * KiB - 1, 'x'));
        CHECK(pv.note == "first 64 KiB of " + sizeBinary(log.size()));
        CHECK(pv.textNote == pv.note);
    }
    SUBCASE("a text above the head: the note names what is shown, not what was read") {
        const auto pv = previewOf(std::string(300 * KiB, 'z'));
        CHECK(pv.text.size() == 64 * KiB);
        CHECK(pv.note == "first 64 KiB of 300 KiB");
    }
    SUBCASE("a text within the limit is whole, and has no note") {
        const auto pv = previewOf(std::string(64 * KiB, 'z'));
        CHECK(pv.text.size() == 64 * KiB);
        CHECK(!pv.truncated);
        CHECK(pv.note == "");
        CHECK(pv.textNote == "");
    }
    SUBCASE("UTF-8 text with a control character, cut inside a character by the head, stays text") {
        std::string text = "a\v";   // a control character: only valid UTF-8 makes this text
        for (int i = 0; i < 1000; ++i) text += "\xD0\xB6";
        const auto pv = previewOf(text, PreviewLimits{.head = 1001});   // ends on the first byte of a character
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.text == text.substr(0, 1000));
    }
    SUBCASE("a text file that merely starts with BM stays text") {
        const auto pv = previewOf("BMW service log\n2024-01-01 oil change\n");
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.imageFormat == "");
    }
}

TEST_CASE("an SVG is a picture only when it is small and keeps to itself") {
    const std::string body = R"svg(<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient><rect id="a" width="4" height="4"/></defs>)svg"
                             R"svg(<rect width="10" height="10" fill="url(#g)" style="stroke:url( '#g' )"/><use href="#a"/><use xlink:href = '#a' x="5"/>)svg"
                             R"svg(<polygon points="0,0 1,1 0,1"/><text font-family="serif" data-only="1">turn it on now, version 2</text>)svg";
    const std::string open = R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" width="10" height="10">)svg";
    const std::string plain = open + body + "</svg>\n";

    SUBCASE("plain") {
        const auto pv = previewOf(plain);
        CHECK(pv.kind == PreviewKind::Image);
        CHECK(pv.imageFormat == "svg");
        CHECK(pv.text == plain);
        CHECK(textOf(pv.bytes) == plain);
        CHECK(!pv.truncated);
        CHECK(pv.note == "");
    }
    SUBCASE("behind a byte order mark, the XML declaration, a comment and a DOCTYPE") {
        const std::string dressed = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n<!-- drawn by hand, not an <svg> yet -->\n"
                                    "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n\n" +
                                    plain;
        const auto pv = previewOf(dressed);
        CHECK(pv.kind == PreviewKind::Image);
        CHECK(pv.imageFormat == "svg");
        CHECK(pv.text == dressed);
    }
    SUBCASE("larger than the head but within the SVG limit: read whole") {
        const std::string padded = open + "<!-- " + std::string(600, '.') + " -->" + body + "</svg>\n";
        ScriptedReader reader(bytesOf(padded));
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Image);
        CHECK(pv->imageFormat == "svg");
        CHECK(pv->text == padded);
        CHECK(textOf(pv->bytes) == padded);
        CHECK(!pv->truncated);
        CHECK(pv->note == "");
        CHECK(reader.reads.size() == 1 + (padded.size() - 64 + 99) / 100);
    }
    SUBCASE("a picture whose source is longer than a text preview: whole bytes, the text cut with a note for when it is shown as text") {
        const std::string padded = open + "<!-- " + std::string(70 * KiB, '.') + " -->" + body + "</svg>\n";
        const auto pv = previewOf(padded);
        CHECK(pv.kind == PreviewKind::Image);
        CHECK(textOf(pv.bytes) == padded);
        CHECK(pv.text == padded.substr(0, 64 * KiB));
        CHECK(pv.textNote == "first 64 KiB of " + sizeBinary(padded.size()));
        CHECK(pv.note == "");
    }
    SUBCASE("above the SVG limit: text, and nothing past the head is read") {
        const std::string large = open + "<!-- " + std::string(5000, '.') + " -->" + body + "</svg>\n";
        ScriptedReader reader(bytesOf(large));
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall);
        REQUIRE(pv);
        CHECK(pv->kind == PreviewKind::Text);
        CHECK(pv->imageFormat == "svg");
        CHECK(pv->truncated);
        CHECK(pv->bytes.size() == 64);
        CHECK(pv->text == large.substr(0, 64));
        CHECK(pv->note == "first 64 B of " + sizeBinary(large.size()) + " · SVG image above the 4 KiB preview limit: shown as text");
        CHECK(reader.reads.size() == 1);
    }
    SUBCASE("the default SVG limit is 512 KiB: a byte more and it is text") {
        const std::size_t frame = open.size() + std::string_view("<!--  -->").size() + body.size() + std::string_view("</svg>\n").size();
        const std::string atLimit = open + "<!-- " + std::string(512 * KiB - frame, '.') + " -->" + body + "</svg>\n";
        REQUIRE(atLimit.size() == 512 * KiB);
        CHECK(previewOf(atLimit).kind == PreviewKind::Image);
        const std::string over = open + "<!-- " + std::string(512 * KiB - frame + 1, '.') + " -->" + body + "</svg>\n";
        const auto pv = previewOf(over);
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.imageFormat == "svg");
        CHECK(pv.note == "first 64 KiB of " + sizeBinary(over.size()) + " · SVG image above the 512 KiB preview limit: shown as text");
    }
    SUBCASE("cancelled while the rest of it is read") {
        const std::string padded = open + "<!-- " + std::string(600, '.') + " -->" + body + "</svg>\n";
        ScriptedReader reader(bytesOf(padded));
        std::atomic<bool> cancelled{false};
        reader.beforeRead = [&](int call) {
            if (call == 1) cancelled = true;
        };
        auto pv = preview(reader, reader.file(), reader.fileStat(), kSmall, &cancelled);
        REQUIRE(!pv);
        CHECK(pv.error().category() == ErrorCategory::Cancelled);
        CHECK(reader.reads.size() == 2);
    }

    const std::pair<const char*, std::string> hostile[] = {
        {"a script", open + "<script>alert(1)</script>" + body + "</svg>"},
        {"an embedded picture from the host", open + R"svg(<image href="file:///etc/passwd" width="10" height="10"/>)svg" + "</svg>"},
        {"an embedded picture by path", open + R"svg(<image xlink:href="/etc/passwd"/>)svg" + "</svg>"},
        {"an embedded picture as data", open + R"svg(<image href="data:image/png;base64,AAAA"/>)svg" + "</svg>"},
        {"foreign content", open + "<foreignObject><p>hello</p></foreignObject></svg>"},
        {"an event on the root", R"svg(<svg xmlns="http://www.w3.org/2000/svg" onload="go()">)svg" + body + "</svg>"},
        {"an event with spaces around the sign", open + R"svg(<rect onclick = "go()"/>)svg" + "</svg>"},
        {"an event in capitals", open + R"svg(<rect ONMOUSEOVER="go()"/>)svg" + "</svg>"},
        {"a reference into another file", open + R"svg(<use href="other.svg#a"/>)svg" + "</svg>"},
        {"a link out", open + R"svg(<a xlink:href="https://example.com"><rect/></a>)svg" + "</svg>"},
        {"an empty reference", open + R"svg(<use href=""/>)svg" + "</svg>"},
        {"a reference spelled with an entity", open + R"svg(<use href="&#102;ile:///etc/passwd"/>)svg" + "</svg>"},
        {"a paint server on the network", open + R"svg(<rect fill="url(http://example.com/p.svg#g)"/>)svg" + "</svg>"},
        {"a paint server in a file, quoted", open + R"svg(<rect style="fill:url('file:///tmp/p.svg#g')"/>)svg" + "</svg>"},
        {"an entity declaration", R"svg(<!DOCTYPE svg [ <!ENTITY e "x"> ]>)svg" + open + body + "</svg>"},
        {"an imported style sheet", open + R"svg(<style>@import "x.css";</style>)svg" + "</svg>"},
        {"a base that sends its own references elsewhere", R"svg(<svg xmlns="http://www.w3.org/2000/svg" xml:base="file:///etc/">)svg" + body + "</svg>"},
        {"a linked style sheet", R"svg(<?xml-stylesheet href="x.css" type="text/css"?>)svg" + open + body + "</svg>"},
        // QtSvg goes by the local name and opens "#x/red.png" relative to its working directory.
        {"a prefixed picture that names a file starting with #", open + R"svg(<s:image xmlns:s="http://www.w3.org/2000/svg" href="#x/red.png"/>)svg" + "</svg>"},
        {"a prefixed picture in capitals", open + R"svg(<SVG:IMAGE xlink:href="#x"/>)svg" + "</svg>"},
        {"a prefixed script", open + R"svg(<svg:script>go()</svg:script>)svg" + "</svg>"},
        {"prefixed foreign content", open + R"svg(<a:foreignObject xmlns:a="http://www.w3.org/2000/svg"/>)svg" + "</svg>"},
        // QtSvg opens the style sheet a processing instruction names as a file, "#" or not.
        {"a style sheet linked by a name that starts with #", R"svg(<?xml-stylesheet type="text/css" href="#canary.css"?>)svg" + open + body + "</svg>"},
        // QtSvg reads url (...) with white space before the parenthesis as url(...).
        {"a paint server after a space", open + R"svg(<rect fill="url (red.png#x)"/>)svg" + "</svg>"},
        {"a paint server after a tab", open + "<rect style=\"fill:url\t(file:///tmp/p.svg#g)\"/></svg>"},
        // The parser reads the text in the encoding declared; the gate reads bytes.
        {"an encoding the gate does not read", R"svg(<?xml version="1.0" encoding="UTF-7"?>)svg" + open + body + "</svg>"},
        {"an EBCDIC encoding", R"svg(<?xml version='1.0' encoding = 'IBM037'?>)svg" + open + body + "</svg>"},
    };
    for (const auto& [what, svg] : hostile) {
        CAPTURE(what);
        CHECK(!svgSafeToRender(svg));
        const auto pv = previewOf(svg);
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.imageFormat == "svg");
        CHECK(pv.text == svg);
        CHECK(pv.note == "SVG image with scripts or references outside the file: shown as text");
    }
    CHECK(svgSafeToRender(plain));
    CHECK(svgSafeToRender(""));
    CHECK(svgSafeToRender(R"svg(<?xml version="1.0" encoding="ISO-8859-1"?>)svg" + plain));
    CHECK(svgSafeToRender(R"svg(<?xml version='1.0' encoding='utf-8' standalone='no'?>)svg" + plain));
    CHECK(svgSafeToRender(open + "<text>a url is not a reference (nor is url-encoding)</text></svg>"));
    // Names that merely start like a banned one, and closing tags, are no element of that kind.
    CHECK(svgSafeToRender(open + "<imagery/><scripted/><x:image-set/></svg>"));
    CHECK(svgSafeToRender("<svg>a < b, 1<2</svg>"));

    SUBCASE("a hostile one larger than the head goes back to the head as text") {
        const std::string padded = open + "<!-- " + std::string(600, '.') + " --><script/>" + body + "</svg>\n";
        const auto pv = previewOf(padded, kSmall);
        CHECK(pv.kind == PreviewKind::Text);
        CHECK(pv.truncated);
        CHECK(pv.bytes.size() == 64);
        CHECK(pv.text == padded.substr(0, 64));
        CHECK(pv.note.starts_with("first 64 B of "));
        CHECK(pv.note.ends_with(" · SVG image with scripts or references outside the file: shown as text"));
    }
    SUBCASE("markup that is not an SVG document is plain text") {
        for (const char* other : {"<html><body><svg width=\"1\"></svg></body></html>", "<?xml version=\"1.0\"?>\n<plist version=\"1.0\"/>", "<svgfoo/>", "<svg",
                                  "<!-- never closed <svg xmlns=\"http://www.w3.org/2000/svg\"/>", "<!DOCTYPE svg [ <svg/>", "see <svg/> below"}) {
            CAPTURE(other);
            const auto pv = previewOf(other);
            CHECK(pv.kind == PreviewKind::Text);
            CHECK(pv.imageFormat == "");
            CHECK(pv.note == "");
        }
    }
    SUBCASE("the smallest ones") {
        CHECK(previewOf("<svg/>").kind == PreviewKind::Image);
        CHECK(previewOf("  \n<svg>\n</svg>").kind == PreviewKind::Image);
        CHECK(previewOf("<svg\twidth='1'/>").kind == PreviewKind::Image);
    }
}

TEST_CASE("a keyboard step lands on a row of the list") {
    CHECK(movedIndex(-1, 0, 1) == -1);
    CHECK(movedIndex(3, 0, -1) == -1);
    CHECK(movedIndex(-1, 5, 1) == 0);
    CHECK(movedIndex(-1, 5, 10) == 0);
    CHECK(movedIndex(-1, 5, -1) == 4);
    CHECK(movedIndex(-1, 5, -10) == 4);
    CHECK(movedIndex(-1, 5, 0) == -1);
    CHECK(movedIndex(0, 5, 1) == 1);
    CHECK(movedIndex(2, 5, -1) == 1);
    CHECK(movedIndex(2, 5, 0) == 2);
    CHECK(movedIndex(0, 5, -1) == 0);
    CHECK(movedIndex(4, 5, 1) == 4);
    CHECK(movedIndex(1, 5, 10) == 4);
    CHECK(movedIndex(3, 5, -10) == 0);
    CHECK(movedIndex(0, 1, 1) == 0);
    CHECK(movedIndex(9, 5, -1) == 4);   // a selection the list has shrunk under
    CHECK(movedIndex(2, 5, std::numeric_limits<int>::max()) == 4);
    CHECK(movedIndex(2, 5, std::numeric_limits<int>::min()) == 0);
}

TEST_CASE("going up tells which directory was left; a second reader reads the same files") {
    auto disk = drstein::test::mbrComposite();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    NodePath exfat;
    for (std::size_t i = 0; i < tree->children.size(); ++i)
        if (tree->children[i].partition && tree->children[i].partition->index == 2) exfat = {static_cast<int>(i)};
    REQUIRE(!exfat.empty());
    auto browser = Browser::open(*tree, exfat);
    REQUIRE(browser);

    CHECK(browser->up() == "");
    CHECK(browser->jumpTo(0) == "");
    REQUIRE(browser->goTo("/dir/nested/deep"));
    CHECK(browser->jumpTo(3) == "");    // where it already is
    CHECK(browser->jumpTo(99) == "");
    CHECK(browser->jumpTo(static_cast<std::size_t>(-1)) == "");
    CHECK(browser->pathText() == "/dir/nested/deep");
    CHECK(browser->up() == "deep");
    CHECK(browser->pathText() == "/dir/nested");
    CHECK(browser->jumpTo(0) == "dir");
    CHECK(browser->pathText() == "/");
    REQUIRE(browser->goTo("/dir/nested/deep"));
    CHECK(browser->jumpTo(1) == "nested");
    CHECK(browser->pathText() == "/dir");
    CHECK(browser->up() == "dir");
    CHECK(browser->up() == "");
    CHECK(browser->breadcrumbs().size() == 1);

    // The second reader the preview worker owns: opened the same way, independent of the first.
    CHECK(!openReader(*tree, {99}));
    CHECK(!openReader(*tree, {}));
    auto second = openReader(*tree, exfat);
    REQUIRE(second);
    auto listing = browser->listCurrent();
    REQUIRE(listing);
    int compared = 0;
    for (const auto& e : listing->entries) {
        if (e.stat.type != fs::FileType::File) continue;
        auto mine = preview(browser->reader(), e.entry.inode, e.stat);
        auto theirs = preview(**second, e.entry.inode, e.stat);
        REQUIRE(mine);
        REQUIRE(theirs);
        CHECK(mine->kind == theirs->kind);
        CHECK((mine->bytes == theirs->bytes));
        ++compared;
    }
    CHECK(compared > 0);
}

// ---------------------------------------------------------------- PreviewLoader

namespace {

constexpr auto kPatience = 20s;   // only ever how long a failing test takes

// What a test shares with the reader inside the loader. The reader can outlive the test's
// scope: a worker that is still in a read keeps it until that read returns.
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;         // every read goes through
    int allowed = 0;          // while closed: how many reads go through all the same
    int entered = 0;          // reads that have started
    bool readerGone = false;
    std::vector<std::uint64_t> inodes;   // which file each read was of

    void close() {
        std::lock_guard lock(mutex);
        open = false;
    }
    void allow(int reads) {
        {
            std::lock_guard lock(mutex);
            allowed += reads;
        }
        changed.notify_all();
    }
    void release() {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        changed.notify_all();
    }
    bool waitEntered(int reads) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, kPatience, [&] { return entered >= reads; });
    }
    bool waitReaderGone() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, kPatience, [&] { return readerGone; });
    }
    bool isReaderGone() {
        std::lock_guard lock(mutex);
        return readerGone;
    }
    std::vector<std::uint64_t> readInodes() {
        std::lock_guard lock(mutex);
        return inodes;
    }
};

class GatedReader final : public fs::Reader {
public:
    GatedReader(std::shared_ptr<Gate> gate, std::map<std::uint64_t, std::vector<std::byte>> files, std::set<std::uint64_t> failing)
        : m_gate(std::move(gate)), m_files(std::move(files)), m_failing(std::move(failing)) {}
    ~GatedReader() override {
        {
            std::lock_guard lock(m_gate->mutex);
            m_gate->readerGone = true;
        }
        m_gate->changed.notify_all();
    }

    // One directory, the root; a file is named by its inode number written out ("10").
    Expected<fs::Inode> root() const override { return fs::Inode{1}; }
    Expected<fs::Inode> lookup(const fs::Inode& dir, std::string_view name) override {
        for (const auto& [id, content] : m_files)
            if (dir.id == 1 && name == std::to_string(id)) return fs::Inode{id};
        return fail(ErrorCategory::NotFound, "no such entry");
    }
    Expected<fs::Stat> stat(const fs::Inode& inode) override {
        const auto it = m_files.find(inode.id);
        if (it == m_files.end()) return fail(ErrorCategory::NotFound, "no such inode");
        fs::Stat st;
        st.type = fs::FileType::File;
        st.size = it->second.size();
        return st;
    }
    Expected<std::vector<fs::DirEntry>> readdir(const fs::Inode&) override { return std::vector<fs::DirEntry>{}; }
    Expected<std::string> readlink(const fs::Inode&) override { return fail(ErrorCategory::InvalidArgument, "not a symbolic link"); }
    Expected<std::size_t> read(const fs::Inode& file, std::uint64_t offset, std::span<std::byte> dst) override {
        {
            std::unique_lock lock(m_gate->mutex);
            const int mine = ++m_gate->entered;
            m_gate->inodes.push_back(file.id);
            m_gate->changed.notify_all();
            m_gate->changed.wait(lock, [&] { return m_gate->open || mine <= m_gate->allowed; });
        }
        if (m_failing.contains(file.id)) return fail(ErrorCategory::Io, "the medium gave up");
        const auto it = m_files.find(file.id);
        if (it == m_files.end()) return fail(ErrorCategory::NotFound, "no such inode");
        if (offset >= it->second.size()) return std::size_t{0};
        const std::size_t n = std::min<std::size_t>(dst.size(), it->second.size() - static_cast<std::size_t>(offset));
        std::memcpy(dst.data(), it->second.data() + offset, n);
        return n;
    }

private:
    std::shared_ptr<Gate> m_gate;
    std::map<std::uint64_t, std::vector<std::byte>> m_files;
    std::set<std::uint64_t> m_failing;
};

struct Inbox {
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<PreviewResult> results;

    bool waitFor(std::uint64_t ticket) {
        std::unique_lock lock(mutex);
        return arrived.wait_for(lock, kPatience, [&] {
            return std::any_of(results.begin(), results.end(), [&](const PreviewResult& r) { return r.ticket == ticket; });
        });
    }
    std::vector<std::uint64_t> tickets() {
        std::lock_guard lock(mutex);
        std::vector<std::uint64_t> out;
        for (const auto& r : results) out.push_back(r.ticket);
        return out;
    }
    PreviewResult take(std::size_t index) {
        std::lock_guard lock(mutex);
        return results.at(index);
    }
};

PreviewLoader::Deliver into(std::shared_ptr<Inbox> inbox) {
    return [inbox = std::move(inbox)](PreviewResult result) {
        {
            std::lock_guard lock(inbox->mutex);
            inbox->results.push_back(std::move(result));
        }
        inbox->arrived.notify_all();
    };
}

// A transform the test can hold the worker in.
struct Hold {
    std::mutex mutex;
    std::condition_variable changed;
    bool holding = true;
    int entered = 0;
    std::thread::id thread;

    PreviewLoader::Transform transform(std::shared_ptr<Hold> self) {
        return [self = std::move(self)](const Preview& pv) -> std::shared_ptr<void> {
            std::unique_lock lock(self->mutex);
            ++self->entered;
            self->thread = std::this_thread::get_id();
            self->changed.notify_all();
            self->changed.wait(lock, [&] { return !self->holding; });
            return std::make_shared<std::string>("decoded: " + pv.text);
        };
    }
    bool waitEntered(int times) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, kPatience, [&] { return entered >= times; });
    }
    void release() {
        {
            std::lock_guard lock(mutex);
            holding = false;
        }
        changed.notify_all();
    }
};

struct Rig {
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
    std::map<std::uint64_t, std::vector<std::byte>> files{{10, bytesOf("file ten\n")}, {11, bytesOf("file eleven\n")}, {12, bytesOf("file twelve\n")}};
    std::set<std::uint64_t> failing;
    std::unique_ptr<PreviewLoader> loader;

    void start(const PreviewLimits& limits = {}, PreviewLoader::Transform transform = nullptr) {
        loader = std::make_unique<PreviewLoader>(std::make_unique<GatedReader>(gate, files, failing), limits, std::move(transform), into(inbox));
    }
    std::uint64_t request(std::uint64_t inode) {
        fs::Stat st;
        st.type = fs::FileType::File;
        st.size = files.at(inode).size();
        return loader->request(PreviewTarget{{std::to_string(inode)}, st});
    }
    // The worker lets go of the reader when it exits: once that happened nothing can arrive.
    bool shutDown() {
        loader.reset();
        gate->release();
        return gate->waitReaderGone();
    }
};

using Tickets = std::vector<std::uint64_t>;

} // namespace

TEST_CASE("the loader reads on its own thread and delivers what the transform made") {
    Rig rig;
    auto hold = std::make_shared<Hold>();
    hold->release();
    rig.start({}, hold->transform(hold));
    rig.loader->cancel();   // with nothing to cancel
    const auto first = rig.request(10);
    CHECK(first != 0);
    REQUIRE(rig.inbox->waitFor(first));
    const auto second = rig.request(11);
    CHECK(second > first);
    REQUIRE(rig.inbox->waitFor(second));
    rig.loader->cancel();   // after the fact
    REQUIRE(rig.shutDown());

    CHECK((rig.inbox->tickets() == Tickets{first, second}));
    const auto a = rig.inbox->take(0), b = rig.inbox->take(1);
    REQUIRE(a.preview);
    CHECK(a.preview->kind == PreviewKind::Text);
    CHECK(a.preview->text == "file ten\n");
    REQUIRE(a.decoded);
    CHECK(*std::static_pointer_cast<std::string>(a.decoded) == "decoded: file ten\n");
    REQUIRE(b.preview);
    CHECK(b.preview->text == "file eleven\n");
    CHECK(*std::static_pointer_cast<std::string>(b.decoded) == "decoded: file eleven\n");
    CHECK(hold->thread != std::this_thread::get_id());
    CHECK((rig.gate->readInodes() == Tickets{10, 11}));
}

TEST_CASE("the loader serves only the latest request") {
    Rig rig;
    rig.gate->close();
    rig.start();
    const auto first = rig.request(10);
    REQUIRE(rig.gate->waitEntered(1));   // the worker is inside the read of file 10
    const auto second = rig.request(11);
    const auto third = rig.request(12);
    CHECK(second > first);
    CHECK(third > second);
    rig.gate->release();
    REQUIRE(rig.inbox->waitFor(third));
    REQUIRE(rig.shutDown());

    CHECK((rig.inbox->tickets() == Tickets{third}));
    CHECK((rig.gate->readInodes() == Tickets{10, 12}));   // file 11 was replaced while it waited: never read
    const auto result = rig.inbox->take(0);
    REQUIRE(result.preview);
    CHECK(result.preview->text == "file twelve\n");
    CHECK(!result.decoded);   // no transform, nothing decoded
}

TEST_CASE("a newer request stops a large read at its next chunk") {
    Rig rig;
    rig.files[10] = binary(kPngMagic, 16 + 8 * 16);
    rig.gate->close();
    rig.gate->allow(2);   // the head and the first chunk go through; the second chunk is held
    rig.start(PreviewLimits{.head = 16, .image = 1 * MiB, .svg = 1 * MiB, .chunk = 16});
    const auto big = rig.request(10);
    REQUIRE(rig.gate->waitEntered(3));
    const auto small = rig.request(11);
    rig.gate->release();
    REQUIRE(rig.inbox->waitFor(small));
    REQUIRE(rig.shutDown());

    CHECK(small > big);
    CHECK((rig.inbox->tickets() == Tickets{small}));
    CHECK((rig.gate->readInodes() == Tickets{10, 10, 10, 11}));   // three of the picture's nine reads
}

TEST_CASE("cancel drops the request in progress and the one waiting") {
    Rig rig;
    rig.gate->close();
    rig.start();
    const auto first = rig.request(10);
    REQUIRE(rig.gate->waitEntered(1));
    const auto second = rig.request(11);
    rig.loader->cancel();
    rig.gate->release();
    // Requests are served in order: when this one arrives the cancelled ones are behind the worker.
    const auto third = rig.request(12);
    REQUIRE(rig.inbox->waitFor(third));
    REQUIRE(rig.shutDown());

    CHECK(second > first);
    CHECK((rig.inbox->tickets() == Tickets{third}));
    CHECK((rig.gate->readInodes() == Tickets{10, 12}));
}

TEST_CASE("a request or a cancel made while the transform runs wins over its result") {
    Rig rig;
    auto hold = std::make_shared<Hold>();
    rig.start({}, hold->transform(hold));
    const auto first = rig.request(10);
    REQUIRE(hold->waitEntered(1));   // the worker is inside the transform of file 10
    SUBCASE("request") {
        const auto second = rig.request(11);
        hold->release();
        REQUIRE(rig.inbox->waitFor(second));
        REQUIRE(rig.shutDown());
        CHECK(second > first);
        CHECK((rig.inbox->tickets() == Tickets{second}));
        CHECK(*std::static_pointer_cast<std::string>(rig.inbox->take(0).decoded) == "decoded: file eleven\n");
    }
    SUBCASE("cancel") {
        rig.loader->cancel();
        hold->release();
        const auto second = rig.request(12);
        REQUIRE(rig.inbox->waitFor(second));
        REQUIRE(rig.shutDown());
        CHECK((rig.inbox->tickets() == Tickets{second}));
    }
}

TEST_CASE("the loader is destroyed without waiting for a read, and delivers nothing afterwards") {
    Rig rig;
    rig.gate->close();
    rig.start();
    rig.request(10);
    REQUIRE(rig.gate->waitEntered(1));
    auto destroyed = std::async(std::launch::async, [&] { rig.loader.reset(); });
    const bool returned = destroyed.wait_for(kPatience) == std::future_status::ready;
    CHECK(returned);
    CHECK(!rig.gate->isReaderGone());   // the worker is still in the read, and still has its reader
    rig.gate->release();
    destroyed.get();
    REQUIRE(rig.gate->waitReaderGone());
    CHECK(rig.inbox->tickets().empty());
}

TEST_CASE("the loader is destroyed without waiting for a transform, and delivers nothing afterwards") {
    Rig rig;
    auto hold = std::make_shared<Hold>();
    rig.start({}, hold->transform(hold));
    rig.request(10);
    REQUIRE(hold->waitEntered(1));
    auto destroyed = std::async(std::launch::async, [&] { rig.loader.reset(); });
    const bool returned = destroyed.wait_for(kPatience) == std::future_status::ready;
    CHECK(returned);
    CHECK(!rig.gate->isReaderGone());
    hold->release();
    destroyed.get();
    REQUIRE(rig.gate->waitReaderGone());
    CHECK(rig.inbox->tickets().empty());
}

TEST_CASE("an idle loader goes away at once and takes its reader with it") {
    Rig rig;
    rig.start();
    REQUIRE(rig.shutDown());
    CHECK(rig.inbox->tickets().empty());
    CHECK(rig.gate->readInodes().empty());
}

TEST_CASE("the loader delivers failures too") {
    SUBCASE("a file that cannot be read: an unreadable preview, transformed like any other") {
        Rig rig;
        rig.failing = {11};
        auto hold = std::make_shared<Hold>();
        hold->release();
        rig.start({}, hold->transform(hold));
        const auto ticket = rig.request(11);
        REQUIRE(rig.inbox->waitFor(ticket));
        const auto after = rig.request(10);   // and the loader carries on
        REQUIRE(rig.inbox->waitFor(after));
        REQUIRE(rig.shutDown());
        CHECK((rig.inbox->tickets() == Tickets{ticket, after}));
        const auto result = rig.inbox->take(0);
        REQUIRE(result.preview);
        CHECK(result.preview->kind == PreviewKind::Unreadable);
        CHECK(result.preview->note == "the medium gave up");
        CHECK(result.decoded);
        CHECK(rig.inbox->take(1).preview->text == "file ten\n");
    }
    SUBCASE("a transform that throws: an error, and no thread dies of it") {
        Rig rig;
        rig.start({}, [](const Preview&) -> std::shared_ptr<void> { throw std::runtime_error("the decoder blew up"); });
        const auto ticket = rig.request(10);
        REQUIRE(rig.inbox->waitFor(ticket));
        REQUIRE(rig.shutDown());
        const auto result = rig.inbox->take(0);
        REQUIRE(!result.preview);
        CHECK(result.preview.error().category() == ErrorCategory::Internal);
        CHECK(result.preview.error().message() == "the decoder blew up");
        CHECK(!result.decoded);
    }
    SUBCASE("no reader at all: an error") {
        auto inbox = std::make_shared<Inbox>();
        PreviewLoader loader(nullptr, {}, nullptr, into(inbox));
        fs::Stat st;
        st.type = fs::FileType::File;
        st.size = 5;
        const auto ticket = loader.request(PreviewTarget{{"10"}, st});
        REQUIRE(inbox->waitFor(ticket));
        const auto result = inbox->take(0);
        REQUIRE(!result.preview);
        CHECK(result.preview.error().category() == ErrorCategory::InvalidArgument);
        CHECK(!result.decoded);
    }
}

TEST_CASE("a burst of requests ends with the last one delivered, each ticket at most once and in order") {
    Rig rig;
    rig.start();
    std::uint64_t last = 0;
    for (int i = 0; i < 500; ++i) {
        last = rig.request(10 + static_cast<std::uint64_t>(i % 3));
        if (i % 7 == 0) rig.loader->cancel();
        if (i % 7 == 0) last = rig.request(12);
    }
    REQUIRE(rig.inbox->waitFor(last));
    REQUIRE(rig.shutDown());
    const auto tickets = rig.inbox->tickets();
    REQUIRE(!tickets.empty());
    CHECK(tickets.back() == last);
    CHECK(std::adjacent_find(tickets.begin(), tickets.end(), std::greater_equal<>()) == tickets.end());
}

namespace {

// A small tree that numbers its files afresh in every instance, as btrfs numbers subvolumes in
// the order one reader meets them: an inode of one reader names nothing, or another file, in
// the next. Node 0 is the root; a lookup finds the first of two entries of the same name.
class RenumberingReader final : public fs::Reader {
public:
    struct Node {
        std::string name;
        std::size_t parent = 0;
        bool dir = false;
        std::string content;
        std::int64_t mtime = 0;
    };

    explicit RenumberingReader(std::vector<Node> nodes) : m_nodes(std::move(nodes)), m_base(nextBase()) {}

    Expected<fs::Inode> root() const override { return fs::Inode{m_base}; }
    Expected<fs::Inode> lookup(const fs::Inode& dir, std::string_view name) override {
        auto d = index(dir);
        if (!d) return fail(d.error());
        for (std::size_t i = 1; i < m_nodes.size(); ++i)
            if (m_nodes[i].parent == *d && m_nodes[i].name == name) return fs::Inode{m_base + i};
        return fail(ErrorCategory::NotFound, "no entry named '" + std::string(name) + "'");
    }
    Expected<fs::Stat> stat(const fs::Inode& inode) override {
        auto i = index(inode);
        if (!i) return fail(i.error());
        const Node& n = m_nodes[*i];
        fs::Stat st;
        st.type = n.dir ? fs::FileType::Directory : fs::FileType::File;
        st.size = n.content.size();
        st.mtime = n.mtime;
        return st;
    }
    Expected<std::vector<fs::DirEntry>> readdir(const fs::Inode& dir) override {
        auto d = index(dir);
        if (!d) return fail(d.error());
        std::vector<fs::DirEntry> out;
        for (std::size_t i = 1; i < m_nodes.size(); ++i)
            if (m_nodes[i].parent == *d) out.push_back(fs::DirEntry{m_nodes[i].name, fs::Inode{m_base + i}, m_nodes[i].dir ? fs::FileType::Directory : fs::FileType::File});
        return out;
    }
    Expected<std::size_t> read(const fs::Inode& file, std::uint64_t offset, std::span<std::byte> dst) override {
        auto i = index(file);
        if (!i) return fail(i.error());
        const std::string& c = m_nodes[*i].content;
        if (offset >= c.size()) return std::size_t{0};
        const std::size_t n = std::min<std::size_t>(dst.size(), c.size() - static_cast<std::size_t>(offset));
        std::memcpy(dst.data(), c.data() + offset, n);
        return n;
    }
    Expected<std::string> readlink(const fs::Inode&) override { return fail(ErrorCategory::InvalidArgument, "not a symbolic link"); }

private:
    static std::uint64_t nextBase() {
        static std::atomic<std::uint64_t> instances{0};
        return ++instances * 1000;
    }
    Expected<std::size_t> index(const fs::Inode& inode) const {
        if (inode.id < m_base || inode.id - m_base >= m_nodes.size()) return fail(ErrorCategory::InvalidArgument, "unknown subvolume in inode id");
        return static_cast<std::size_t>(inode.id - m_base);
    }

    std::vector<Node> m_nodes;
    std::uint64_t m_base;
};

const std::vector<RenumberingReader::Node> kTree{
    {"/", 0, true, "", 0},
    {"photos", 0, true, "", 0},
    {"2026", 1, true, "", 0},
    {"note.txt", 2, false, "found again by its path\n", 1'700'000'000},
    {"twin", 0, false, "first\n", 1},
    {"twin", 0, false, "the second twin\n", 2},
};

const Entry& entryNamed(const Listing& listing, std::string_view name, std::size_t nth = 0) {
    for (const auto& e : listing.entries)
        if (e.entry.name == name && nth-- == 0) return e;
    FAIL("no entry named " << name);
    throw std::logic_error("unreachable");
}

} // namespace

TEST_CASE("the loader finds a file again by its path, whatever its own reader numbers it") {
    auto browser = Browser::fromReader(std::make_unique<RenumberingReader>(kTree), "tree", "test");
    REQUIRE(browser);
    REQUIRE(browser->goTo("/photos/2026"));
    auto listing = browser->listCurrent();
    REQUIRE(listing);
    const Entry note = entryNamed(*listing, "note.txt");
    CHECK((browser->pathTo("note.txt") == std::vector<std::string>{"photos", "2026", "note.txt"}));

    auto reader = std::make_unique<RenumberingReader>(kTree);
    CHECK(!reader->stat(note.entry.inode));   // the browser's inode means nothing to the loader's reader
    auto inbox = std::make_shared<Inbox>();
    PreviewLoader loader(std::move(reader), {}, nullptr, into(inbox));

    const auto found = loader.request(PreviewTarget{browser->pathTo("note.txt"), note.stat});
    REQUIRE(inbox->waitFor(found));
    const auto a = inbox->take(0);
    REQUIRE(a.preview);
    CHECK(a.preview->kind == PreviewKind::Text);
    CHECK(a.preview->text == "found again by its path\n");

    // Gone since it was listed: unreadable, with the reader's words.
    const auto gone = loader.request(PreviewTarget{{"photos", "2026", "gone.txt"}, note.stat});
    REQUIRE(inbox->waitFor(gone));
    const auto b = inbox->take(1);
    REQUIRE(b.preview);
    CHECK(b.preview->kind == PreviewKind::Unreadable);
    CHECK(b.preview->note == "no entry named 'gone.txt'");

    // Two entries of one name, as on damaged media: the name finds the first. The second is not
    // shown as the first one's content.
    REQUIRE(browser->goTo("/"));
    auto root = browser->listCurrent();
    REQUIRE(root);
    const Entry first = entryNamed(*root, "twin", 0), second = entryNamed(*root, "twin", 1);
    REQUIRE(first.stat.size != second.stat.size);
    const auto twin1 = loader.request(PreviewTarget{browser->pathTo("twin"), first.stat});
    REQUIRE(inbox->waitFor(twin1));
    const auto twin2 = loader.request(PreviewTarget{browser->pathTo("twin"), second.stat});
    REQUIRE(inbox->waitFor(twin2));
    const auto c = inbox->take(2), d = inbox->take(3);
    REQUIRE(c.preview);
    CHECK(c.preview->text == "first\n");
    REQUIRE(d.preview);
    CHECK(d.preview->kind == PreviewKind::Unreadable);
    CHECK(d.preview->note == "not the file listed under this name: another entry has the same name, or it changed");

    // A path that names nothing at all.
    const auto none = loader.request(PreviewTarget{{}, note.stat});
    REQUIRE(inbox->waitFor(none));
    CHECK(inbox->take(4).preview->kind == PreviewKind::Unreadable);
}

TEST_CASE("the loader tells which request its worker is in, and whether it reads or decodes") {
    using Phase = PreviewActivity::Phase;
    Rig rig;
    auto hold = std::make_shared<Hold>();
    rig.gate->close();
    rig.start({}, hold->transform(hold));
    CHECK(rig.loader->activity().ticket == 0);
    CHECK(rig.loader->activity().phase == Phase::Idle);

    const auto first = rig.request(10);
    REQUIRE(rig.gate->waitEntered(1));
    auto a = rig.loader->activity();
    CHECK(a.ticket == first);
    CHECK(a.phase == Phase::Reading);

    rig.gate->release();
    REQUIRE(hold->waitEntered(1));
    a = rig.loader->activity();
    CHECK(a.ticket == first);
    CHECK(a.phase == Phase::Transforming);

    // Superseded, the worker is still in the first one's decode: that is what the caller sees.
    const auto second = rig.request(11);
    a = rig.loader->activity();
    CHECK(a.ticket == first);
    CHECK(a.phase == Phase::Transforming);

    hold->release();
    REQUIRE(rig.inbox->waitFor(second));
    a = rig.loader->activity();   // idle before it delivers
    CHECK(a.ticket == 0);
    CHECK(a.phase == Phase::Idle);
    REQUIRE(rig.shutDown());
    CHECK((rig.inbox->tickets() == Tickets{second}));
}

TEST_CASE("the process can wait for the workers that outlive their loaders") {
    Rig rig;
    auto hold = std::make_shared<Hold>();
    rig.start({}, hold->transform(hold));
    rig.request(10);
    REQUIRE(hold->waitEntered(1));
    rig.loader.reset();
    CHECK(PreviewLoader::workersAlive() >= 1);
    CHECK(!PreviewLoader::waitForWorkers(50ms));   // still in its decode
    hold->release();
    CHECK(PreviewLoader::waitForWorkers(kPatience));
    CHECK(PreviewLoader::workersAlive() == 0);
    CHECK(rig.gate->isReaderGone());   // its reader went before the worker counted itself out
    CHECK(rig.inbox->tickets().empty());
}

TEST_CASE("a folder is entered as it was listed, even beside another of the same name") {
    const std::vector<RenumberingReader::Node> tree{
        {"/", 0, true, "", 0},
        {"dup", 0, true, "", 0},
        {"dup", 0, true, "", 0},
        {"in-first.txt", 1, false, "1\n", 0},
        {"in-second.txt", 2, false, "2\n", 0},
        {"file", 0, false, "x\n", 0},
    };
    auto browser = Browser::fromReader(std::make_unique<RenumberingReader>(tree), "tree", "test");
    REQUIRE(browser);
    auto root = browser->listCurrent();
    REQUIRE(root);
    const Entry second = entryNamed(*root, "dup", 1);

    REQUIRE(browser->enter("dup", second.entry.inode));
    CHECK(browser->pathText() == "/dup");
    auto inside = browser->listCurrent();
    REQUIRE(inside);
    REQUIRE(inside->entries.size() == 1);
    CHECK(inside->entries[0].entry.name == "in-second.txt");

    CHECK(browser->up() == "dup");
    REQUIRE(browser->enter("dup"));   // by name: the first of the two
    inside = browser->listCurrent();
    REQUIRE(inside);
    REQUIRE(inside->entries.size() == 1);
    CHECK(inside->entries[0].entry.name == "in-first.txt");

    CHECK(browser->up() == "dup");
    CHECK(!browser->enter("file", entryNamed(*root, "file").entry.inode));   // not a directory
    CHECK(browser->pathText() == "/");
}

namespace {

std::string be16s(unsigned v) { return {static_cast<char>((v >> 8) & 0xFF), static_cast<char>(v & 0xFF)}; }

// A JPEG's markers around nothing worth decoding: a frame of kind `sof` (0xC0 baseline, 0xC2
// progressive), the first component sampled 2 x 2 and the others 1 x 1, and `scans` scans of a
// few bytes of entropy-coded data each, a stuffed FF and a restart marker among them.
std::string jpegMarkers(unsigned sof, unsigned width, unsigned height, unsigned components, unsigned scans) {
    std::string j = "\xFF\xD8\xFF\xE0"s + be16s(16) + "JFIF\0"s + std::string(9, '\0');
    std::string frame = "\x08"s + be16s(height) + be16s(width) + static_cast<char>(components);
    for (unsigned c = 0; c < components; ++c) frame += std::string{static_cast<char>(c + 1), static_cast<char>(c == 0 ? 0x22 : 0x11), 0};
    j += "\xFF"s + static_cast<char>(sof) + be16s(2 + frame.size()) + frame;
    j += "\xFF\xC4"s + be16s(5) + "\x00\xFF\xDA"s;   // a table whose bytes look like a scan marker
    for (unsigned n = 0; n < scans; ++n) {
        const std::string scan = "\x01\x01\x00\x00\x3F\x00"s;
        j += "\xFF\xDA"s + be16s(2 + scan.size()) + scan + "\x12\xFF\x00\x34\xFF\xD3\x56"s;
    }
    return j + "\xFF\xD9";
}

} // namespace

TEST_CASE("a JPEG that would hold the whole picture to decode it is refused by its headers") {
    SUBCASE("a baseline JPEG decodes in strips at any size: a picture") {
        const auto pv = previewOf(jpegMarkers(0xC0, 16384, 16384, 3, 1));
        CHECK(pv.kind == PreviewKind::Image);
        CHECK(pv.imageFormat == "jpeg");
    }
    SUBCASE("a progressive photo of usual size and scans: a picture") {
        CHECK(previewOf(jpegMarkers(0xC2, 4000, 3000, 3, 10)).kind == PreviewKind::Image);
        CHECK(previewOf(jpegMarkers(0xC2, 4000, 3000, 3, 100)).kind == PreviewKind::Image);
    }
    SUBCASE("progressive and large: every coefficient is kept, whatever size it is drawn at") {
        // 16384 x 16384, 4:2:0: 512 MiB of luma coefficients and 128 MiB for each chroma plane.
        const std::string file = jpegMarkers(0xC2, 16384, 16384, 3, 1);
        const auto pv = previewOf(file);
        CHECK(pv.kind == PreviewKind::Hex);
        CHECK(pv.imageFormat == "jpeg");
        CHECK(pv.note == "progressive JPEG image, 16384 × 16384: too large to decode");
        CHECK(textOf(pv.bytes) == file);
        // No scan at all yet, as in a file cut short: the frame alone says it.
        CHECK(previewOf(jpegMarkers(0xC2, 16384, 16384, 3, 0)).kind == PreviewKind::Hex);
        // Just within: 8192 x 8192 holds 192 MiB.
        CHECK(previewOf(jpegMarkers(0xC2, 8192, 8192, 3, 1)).kind == PreviewKind::Image);
    }
    SUBCASE("a baseline JPEG in several scans keeps its coefficients too") {
        const auto pv = previewOf(jpegMarkers(0xC0, 16384, 16384, 3, 3));
        CHECK(pv.kind == PreviewKind::Hex);
        CHECK(pv.note == "progressive JPEG image, 16384 × 16384: too large to decode");
    }
    SUBCASE("every scan walks all coefficients again: too many is refused at any size") {
        const auto pv = previewOf(jpegMarkers(0xC2, 64, 64, 1, 101));
        CHECK(pv.kind == PreviewKind::Hex);
        CHECK(pv.note == "JPEG image in 101 scans: too many to decode");
    }
    SUBCASE("a large one past the head goes back to its head as hex") {
        std::string file = jpegMarkers(0xC2, 16384, 16384, 3, 1);
        file.insert(file.size() - 2, std::string(300, '\x55'));   // more entropy-coded data
        const auto pv = previewOf(file, kSmall);
        CHECK(pv.kind == PreviewKind::Hex);
        CHECK(pv.bytes.size() == 64);
        CHECK(pv.truncated);
    }
}
