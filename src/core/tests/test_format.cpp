// SPDX-License-Identifier: MIT
#include "drstein/core/format.hpp"

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

TEST_CASE("sizes read like what disks are sold as") {
    CHECK(sizeText(500107862016ull) == "500.1 GB");
    CHECK(sizeText(2000398934016ull) == "2.0 TB");
    CHECK(sizeText(188'000'000'000ull) == "188.0 GB");
    CHECK(sizeText(999) == "999 B");
    CHECK(sizeText(1536) == "1.5 kB");
    CHECK(sizeText(512 * MiB, false) == "512.0 MiB");
    CHECK(sizeBinary(512 * MiB) == "512 MiB");
    CHECK(sizeBinary(16896) == "16.5 KiB");
    CHECK(sizeBinary(92) == "92 B");
    CHECK(sizeBinary(4096) == "4 KiB");
}

TEST_CASE("numbers group with thin spaces") {
    CHECK(grouped(0) == "0");
    CHECK(grouped(999) == "999");
    CHECK(grouped(1000) == "1 000");
    CHECK(grouped(976773167) == "976 773 167");
    CHECK(lbaText(34) == "LBA 34");
    CHECK(lbaRangeText(2048, 1050623, 512) == "LBA 2 048 – 1 050 623 · 512 MiB");
    CHECK(hexRangeText(Region{0x200, 0x200}) == "0x200–0x400");
}

TEST_CASE("rates, durations and percentages") {
    CHECK(rateText(481'000'000) == "481.0 MB/s");
    CHECK(rateText(0) == "—");
    CHECK(durationText(598) == "9 min 58 s");
    CHECK(durationText(4.3) == "4.3 s");
    CHECK(durationText(3725) == "1 h 2 min");
    CHECK(etaText(-1) == "ETA unknown");
    CHECK(percentText(0.614) == "61%");
    CHECK(percentText(-1) == "—");
}

TEST_CASE("mode strings") {
    fs::Stat st;
    st.type = fs::FileType::Directory;
    st.mode = 0755;
    CHECK(modeText(st) == "drwxr-xr-x");
    st.type = fs::FileType::Symlink;
    st.mode = 0777;
    CHECK(modeText(st) == "lrwxrwxrwx");
    st.type = fs::FileType::File;
    st.mode = 0600;
    CHECK(modeText(st) == "-rw-------");
}

TEST_CASE("the error model decides the treatment") {
    auto p = present(Error(ErrorCategory::Permission, "/dev/sdb needs root"));
    CHECK(p.needsElevation);
    CHECK(p.severity == Severity::Warning);
    CHECK(p.title == "Needs elevation");
    CHECK(p.message == "/dev/sdb needs root");
    CHECK(!p.hint.empty());

    auto b = present(Error(ErrorCategory::Busy, "mounted"));
    CHECK(b.busy);
    auto i = present(Error(ErrorCategory::Integrity, "chunk 7 mismatch"));
    CHECK(i.integrity);
    CHECK(i.severity == Severity::Error);
    auto u = present(Error(ErrorCategory::Unsupported, "no"));
    CHECK(u.severity == Severity::Info);
    auto io = present(Error(ErrorCategory::Io, "short read", 5));
    CHECK(io.hint.find("5") != std::string::npos);
}
