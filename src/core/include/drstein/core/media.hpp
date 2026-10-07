// SPDX-License-Identifier: MIT
// The Tools view: surface scan (read-only) and the capacity / fake-flash
// test (destructive, confirmed by typing the device's kernel name).
#pragma once

#include "stein/core/progress.hpp"
#include "stein/core/report.hpp"
#include "stein/ops/media_test.hpp"
#include "stein/probe/topology.hpp"

#include <string>
#include <vector>

namespace drstein::core {

enum class CellState { Untested, Ok, Bad };
// The grid: `cells` equal slices of [0, total); a slice is Bad when a bad region touches it,
// Untested beyond `tested` bytes (a scan in progress).
std::vector<CellState> scanCells(stein::ByteCount total, const std::vector<stein::Region>& bad, std::size_t cells, stein::ByteCount tested);

struct ScanSummary {
    std::string scannedText;      // "500.1 GB · 100%"
    std::string unreadableText;   // "2 regions · 24 KiB" / "none"
    std::string rateText;         // "1.9 GB/s · 4 min 21 s"
    std::vector<std::string> badLines;   // "LBA 412 788 224 – 412 788 255 · 16 KiB · in partition 2 (root)"
    bool healthy = true;
};
ScanSummary summarize(const stein::ops::SurfaceScanResult& r, const stein::probe::Node& tree, std::uint32_t sectorSize);
stein::Expected<stein::ops::SurfaceScanResult> runSurfaceScan(stein::BlockDevice& device, stein::Progress& progress, stein::Report& report);

struct CapacityTestRequest {
    std::string confirmText;      // what the user typed
    std::string expectedConfirm;  // the kernel name; empty for image files (no confirmation needed)
    bool quick = true;
    std::uint32_t quickStride = 16;
    bool keepPattern = false;
};
// Refuses with InvalidArgument unless confirmText == expectedConfirm (when one is expected).
stein::Expected<stein::ops::CapacityTestResult> runCapacityTest(stein::BlockDevice& device, const CapacityTestRequest& request, stein::Progress& progress,
                                                                stein::Report& report);

struct CapacitySummary {
    std::string verdict;          // "OK", "FAKE OR DAMAGED", "DAMAGED"
    std::string detail;           // "claims 64 GB, real capacity about 7.9 GB"
    std::string speedText;        // "write 23 MiB/s · read 88 MiB/s"
    bool healthy = true;
};
CapacitySummary summarize(const stein::ops::CapacityTestResult& r);

} // namespace drstein::core
