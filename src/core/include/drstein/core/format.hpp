// SPDX-License-Identifier: MIT
// Every string a view shows that the library does not already provide.
// Pinned by tests so the UI vocabulary stays stable.
#pragma once

#include "stein/core/error.hpp"
#include "stein/core/units.hpp"
#include "stein/fs/reader.hpp"
#include "stein/layout/node.hpp"

#include <cstdint>
#include <string>

namespace drstein::core {

// "500.1 GB" (decimal, what disks are sold as) or "465.7 GiB" (binary).
std::string sizeText(stein::ByteCount bytes, bool decimal = true);
// Binary with exact small values: "512 MiB", "16.5 KiB", "92 B".
std::string sizeBinary(stein::ByteCount bytes);
// "500 107 862 016" with thin-space grouping (U+2009).
std::string grouped(std::uint64_t value);
// "LBA 976 773 167"
std::string lbaText(stein::Lba lba);
// "LBA 2 048 – 1 050 623 · 512 MiB"
std::string lbaRangeText(stein::Lba first, stein::Lba last, std::uint32_t sectorSize);
// "bytes 0 – 500 107 862 016"
std::string byteRangeText(const stein::Region& region);
// "0x200–0x400"
std::string hexRangeText(const stein::Region& region);
// "481 MB/s"
std::string rateText(double bytesPerSecond);
// "9 min 58 s", "4.3 s", "ETA unknown" when negative.
std::string durationText(double seconds);
std::string etaText(double seconds);
// "61%"
std::string percentText(double fraction);
// "2026-09-28 22:14" in local time; "" for 0.
std::string timeText(std::int64_t secondsSinceEpoch);
// ISO 8601 UTC ("2026-10-06T23:28:08Z") to local "2026-10-07 01:28"; the input back when it does not parse.
std::string isoToLocalText(const std::string& iso);
// "drwxr-xr-x"
std::string modeText(const stein::fs::Stat& st);
// "OK", "Info", "Warning", "Error"
std::string validityText(stein::layout::Validity v);

// The error model of 19-gui-brief.md §5: a category decides the UI treatment.
enum class Severity { Info, Warning, Error };
struct ErrorPresentation {
    std::string title;      // "Needs elevation"
    std::string message;    // the library's sentence
    std::string hint;       // "Relaunch Dr Stein with sudo to open raw disks."
    Severity severity;
    bool needsElevation;    // Permission
    bool busy;              // Busy: offer unmount
    bool integrity;         // Integrity: red, never silent
};
ErrorPresentation present(const stein::Error& error);

} // namespace drstein::core
