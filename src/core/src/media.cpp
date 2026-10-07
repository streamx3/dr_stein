// SPDX-License-Identifier: MIT
#include "drstein/core/media.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/topology.hpp"

#include <cstdio>

namespace drstein::core {

using namespace stein;

std::vector<CellState> scanCells(ByteCount total, const std::vector<Region>& bad, std::size_t cells, ByteCount tested) {
    std::vector<CellState> out(cells, CellState::Untested);
    if (!total || !cells) return out;
    const double per = static_cast<double>(total) / static_cast<double>(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        const ByteCount start = static_cast<ByteCount>(per * static_cast<double>(i));
        if (start < tested) out[i] = CellState::Ok;
    }
    for (const auto& r : bad) {
        if (r.empty()) continue;
        const auto first = static_cast<std::size_t>(static_cast<double>(r.offset) / per);
        const auto last = static_cast<std::size_t>(static_cast<double>(r.end() - 1) / per);
        for (std::size_t i = first; i <= last && i < cells; ++i) out[i] = CellState::Bad;
    }
    return out;
}

namespace {

std::string toLowerFirst(std::string s) {
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] + 32);
    return s;
}

std::string inPartition(const probe::Node& tree, const Region& r) {
    for (const auto& c : tree.children) {
        if (c.kind != probe::NodeKind::Partition) continue;
        if (c.region.contains(r.offset)) {
            std::string s = "in " + toLowerFirst(kindLabel(c));
            const std::string name = displayName(c);
            if (!name.empty()) s += " (" + name + ")";
            return s;
        }
    }
    return "outside any partition";
}

} // namespace

ScanSummary summarize(const ops::SurfaceScanResult& r, const probe::Node& tree, std::uint32_t sectorSize) {
    ScanSummary s;
    s.healthy = r.healthy();
    s.scannedText = sizeText(r.bytesRead) + (tree.region.length ? " · " + percentText(static_cast<double>(r.bytesRead) / static_cast<double>(tree.region.length)) : "");
    if (r.badRegions.empty()) s.unreadableText = "none";
    else {
        ByteCount bytes = 0;
        for (const auto& b : r.badRegions) bytes += b.length;
        s.unreadableText = std::to_string(r.badRegions.size()) + (r.badRegions.size() == 1 ? " region" : " regions") + " · " + sizeBinary(bytes);
    }
    s.rateText = (r.seconds > 0 ? rateText(static_cast<double>(r.bytesRead) / r.seconds) : std::string("—")) + " · " + durationText(r.seconds);
    for (const auto& b : r.badRegions)
        s.badLines.push_back("LBA " + grouped(b.offset / sectorSize) + " – " + grouped((b.end() - 1) / sectorSize) + " · " + sizeBinary(b.length) + " · " + inPartition(tree, b));
    return s;
}

Expected<ops::SurfaceScanResult> runSurfaceScan(BlockDevice& device, Progress& progress, Report& report) {
    report.start();
    report.addDetail("device", device.name());
    report.addDetail("size", sizeText(device.size()));
    auto r = ops::surfaceScan(device, progress, report);
    if (!r) {
        report.addLine(r.error().toString());
        report.finish(ReportStatus::Error);
        return fail(r.error());
    }
    report.finish(r->healthy() ? ReportStatus::Success : ReportStatus::Error);
    return r;
}

Expected<ops::CapacityTestResult> runCapacityTest(BlockDevice& device, const CapacityTestRequest& request, Progress& progress, Report& report) {
    if (!request.expectedConfirm.empty() && request.confirmText != request.expectedConfirm)
        return fail(ErrorCategory::InvalidArgument, "type the device name (" + request.expectedConfirm + ") to confirm erasing it");
    if (device.isReadOnly()) return fail(ErrorCategory::Permission, "the device is opened read-only");
    ops::CapacityTestOptions o;
    o.quickStride = request.quick ? request.quickStride : 0;
    o.keepPattern = request.keepPattern;
    report.start();
    report.addDetail("device", device.name());
    report.addDetail("mode", request.quick ? "quick · every " + std::to_string(request.quickStride) + "th chunk" : "full");
    auto r = ops::capacityTest(device, o, progress, report);
    if (!r) {
        report.addLine(r.error().toString());
        report.finish(ReportStatus::Error);
        return fail(r.error());
    }
    report.finish(r->healthy() ? ReportStatus::Success : ReportStatus::Error);
    return r;
}

CapacitySummary summarize(const ops::CapacityTestResult& r) {
    CapacitySummary s;
    s.healthy = r.healthy();
    char buf[64];
    std::snprintf(buf, sizeof buf, "write %d MiB/s · read %d MiB/s", static_cast<int>(r.writeMiBps), static_cast<int>(r.readMiBps));
    s.speedText = buf;
    if (r.healthy()) {
        s.verdict = "OK";
        s.detail = sizeText(r.bytesVerified) + " verified" + (r.bytesVerified < r.claimedBytes ? " (quick mode: every Nth chunk)" : "");
    } else if (r.realCapacityEstimate) {
        s.verdict = "FAKE OR DAMAGED";
        s.detail = "claims " + sizeText(r.claimedBytes) + ", real capacity about " + sizeText(*r.realCapacityEstimate) + (r.wraparound ? " (addresses wrap around)" : "");
    } else {
        s.verdict = "DAMAGED";
        s.detail = std::to_string(r.chunksBad) + " bad chunk(s)" + (r.firstMismatch ? ", pattern stops at " + sizeText(*r.firstMismatch) : "");
    }
    return s;
}

} // namespace drstein::core
