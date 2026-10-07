// SPDX-License-Identifier: MIT
#include "drstein/core/format.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace drstein::core {

namespace {

std::string trimmed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

} // namespace

std::string sizeText(stein::ByteCount bytes, bool decimal) {
    const double base = decimal ? 1000.0 : 1024.0;
    static const char* dec[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    static const char* bin[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    const char** units = decimal ? dec : bin;
    if (bytes < 1000) return std::to_string(bytes) + " B";
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= base && i < 5) {
        v /= base;
        ++i;
    }
    return trimmed(v, 1) + " " + units[i];
}

std::string sizeBinary(stein::ByteCount bytes) {
    static const char* bin[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 5) {
        v /= 1024.0;
        ++i;
    }
    // Exact values print without decimals ("512 MiB"), the rest with one ("16.5 KiB").
    const bool exact = std::fabs(v - std::round(v)) < 1e-9;
    return trimmed(v, exact ? 0 : 1) + " " + bin[i];
}

std::string grouped(std::uint64_t value) {
    const std::string digits = std::to_string(value);
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i && (n - i) % 3 == 0) out += " ";
        out += digits[i];
    }
    return out;
}

std::string lbaText(stein::Lba lba) { return "LBA " + grouped(lba); }

std::string lbaRangeText(stein::Lba first, stein::Lba last, std::uint32_t sectorSize) {
    const stein::ByteCount bytes = last >= first ? (last - first + 1) * sectorSize : 0;
    return "LBA " + grouped(first) + " – " + grouped(last) + " · " + sizeBinary(bytes);
}

std::string byteRangeText(const stein::Region& region) {
    return "bytes " + grouped(region.offset) + " – " + grouped(region.end());
}

std::string hexRangeText(const stein::Region& region) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "0x%llX–0x%llX", static_cast<unsigned long long>(region.offset),
                  static_cast<unsigned long long>(region.end()));
    return buf;
}

std::string rateText(double bytesPerSecond) {
    if (bytesPerSecond <= 0) return "—";
    return sizeText(static_cast<stein::ByteCount>(bytesPerSecond)) + "/s";
}

std::string durationText(double seconds) {
    if (seconds < 0) return "unknown";
    if (seconds < 10) return trimmed(seconds, 1) + " s";
    const long s = static_cast<long>(std::llround(seconds));
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
    return std::to_string(s / 3600) + " h " + std::to_string((s % 3600) / 60) + " min";
}

std::string etaText(double seconds) { return seconds < 0 ? "ETA unknown" : "ETA " + durationText(seconds); }

std::string percentText(double fraction) {
    if (fraction < 0) return "—";
    return std::to_string(static_cast<int>(std::llround(fraction * 100.0))) + "%";
}

std::string timeText(std::int64_t secondsSinceEpoch) {
    if (secondsSinceEpoch == 0) return {};
    const std::time_t t = static_cast<std::time_t>(secondsSinceEpoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string modeText(const stein::fs::Stat& st) {
    using stein::fs::FileType;
    std::string m;
    switch (st.type) {
    case FileType::Directory: m = "d"; break;
    case FileType::Symlink: m = "l"; break;
    case FileType::CharDevice: m = "c"; break;
    case FileType::BlockDevice: m = "b"; break;
    case FileType::Fifo: m = "p"; break;
    case FileType::Socket: m = "s"; break;
    default: m = "-"; break;
    }
    const char* bits = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) m += (st.mode & (0400u >> i)) ? bits[i] : '-';
    return m;
}

std::string validityText(stein::layout::Validity v) {
    switch (v) {
    case stein::layout::Validity::Ok: return "OK";
    case stein::layout::Validity::Info: return "Info";
    case stein::layout::Validity::Warning: return "Warning";
    case stein::layout::Validity::Error: return "Error";
    }
    return "?";
}

ErrorPresentation present(const stein::Error& error) {
    using stein::ErrorCategory;
    ErrorPresentation p;
    p.message = error.message();
    p.severity = Severity::Error;
    p.needsElevation = p.busy = p.integrity = false;
    switch (error.category()) {
    case ErrorCategory::Permission:
        p.title = "Needs elevation";
        p.hint = "Raw disks need root or Administrator rights. Image files work without them. "
                 "Relaunch Dr Stein elevated (sudo on Linux and macOS, \"Run as administrator\" on Windows) to open this device.";
        p.needsElevation = true;
        p.severity = Severity::Warning;
        break;
    case ErrorCategory::Busy:
        p.title = "Device is busy";
        p.hint = "A volume on this device is mounted or in use. Unmount it and try again.";
        p.busy = true;
        p.severity = Severity::Warning;
        break;
    case ErrorCategory::Unsupported:
        p.title = "Not supported yet";
        p.hint = "This is a known limitation of this version, not damage.";
        p.severity = Severity::Info;
        break;
    case ErrorCategory::Integrity:
        p.title = "Integrity check failed";
        p.hint = "Data did not match its checksum. Do not restore from this image until the cause is understood.";
        p.integrity = true;
        break;
    case ErrorCategory::InvalidFormat:
        p.title = "Malformed on-disk structure";
        p.hint = "The bytes do not form a valid structure. The Hex view shows which fields fail.";
        break;
    case ErrorCategory::NotFound:
        p.title = "Not found";
        p.severity = Severity::Warning;
        break;
    case ErrorCategory::InvalidArgument:
        p.title = "Invalid input";
        p.severity = Severity::Warning;
        break;
    case ErrorCategory::OutOfRange:
        p.title = "Out of range";
        p.severity = Severity::Warning;
        break;
    case ErrorCategory::Cancelled:
        p.title = "Cancelled";
        p.severity = Severity::Info;
        break;
    case ErrorCategory::Io:
        p.title = "I/O error";
        p.hint = error.osCode() ? "OS error code " + std::to_string(error.osCode()) + "." : "";
        break;
    case ErrorCategory::Internal:
        p.title = "Internal error";
        p.hint = "This is a bug. Please report it with the message above.";
        break;
    case ErrorCategory::None:
        p.title = "OK";
        p.severity = Severity::Info;
        break;
    }
    return p;
}

} // namespace drstein::core
