// SPDX-License-Identifier: MIT
#include "drstein/core/source.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/imaging.hpp"
#include "stein/image/operations.hpp"
#include "stein/image/stein_format.hpp"

#include <algorithm>

namespace drstein::core {

using namespace stein;

std::string_view toString(SourceGroup g) {
    switch (g) {
    case SourceGroup::Internal: return "Internal";
    case SourceGroup::Removable: return "Removable";
    case SourceGroup::Virtual: return "Virtual";
    case SourceGroup::Images: return "Images";
    }
    return "?";
}

std::string busName(platform::Bus bus) {
    using platform::Bus;
    switch (bus) {
    case Bus::Ata: return "SATA";
    case Bus::Scsi: return "SCSI";
    case Bus::Nvme: return "NVMe";
    case Bus::Usb: return "USB";
    case Bus::Sd: return "SD";
    case Bus::Mmc: return "MMC";
    case Bus::Virtual: return "virtual";
    case Bus::Loop: return "loop";
    case Bus::Nbd: return "NBD";
    case Bus::DeviceMapper: return "device-mapper";
    case Bus::Md: return "md RAID";
    case Bus::Other: return "other";
    case Bus::Unknown: break;
    }
    return "";
}

namespace {

std::string iconFor(const platform::DiskInfo& d) {
    using platform::Bus;
    if (d.isVirtual) return "virtual";
    switch (d.bus) {
    case Bus::Nvme: return "nvme";
    case Bus::Usb: return "usb";
    case Bus::Sd:
    case Bus::Mmc: return "sd";
    case Bus::Loop:
    case Bus::Nbd:
    case Bus::DeviceMapper:
    case Bus::Md:
    case Bus::Virtual: return "virtual";
    default: return "sata";
    }
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += " · ";
        out += p;
    }
    return out;
}

SourceDescriptor fromDisk(const platform::DiskInfo& d) {
    SourceDescriptor s;
    s.id = "disk:" + d.identity();
    s.kind = SourceKind::Disk;
    s.group = d.isVirtual ? SourceGroup::Virtual : d.removable ? SourceGroup::Removable : SourceGroup::Internal;
    s.name = d.kernelName.empty() ? std::filesystem::path(d.osPath).filename().string() : d.kernelName;
    s.title = d.model.empty() ? s.name : d.model;
    s.path = d.osPath;
    s.sizeBytes = d.geometry.sizeBytes;
    s.icon = iconFor(d);
    s.disk = d;
    std::vector<std::string> sub{d.model, busName(d.bus)};
    if (d.backingFile) sub = {d.backingFile->filename().string(), "loop"};
    s.subtitle = join(sub);
    std::vector<std::string> id;
    if (!d.serial.empty()) id.push_back(d.serial);
    if (!d.firmware.empty()) id.push_back("fw " + d.firmware);
    id.push_back(std::to_string(d.geometry.logicalSectorSize) + " B logical / " + std::to_string(d.geometry.physicalSectorSize) + " B physical");
    id.push_back(d.removable ? "removable" : "not removable");
    if (d.rotational) id.push_back("rotational");
    if (d.readOnly) id.push_back("read-only");
    if (d.backingFile) id.push_back("backed by " + d.backingFile->string());
    s.identityLine = join(id);
    return s;
}

} // namespace

Expected<std::vector<SourceDescriptor>> listDisks(platform::Platform& platform) {
    auto disks = platform.enumerate();
    if (!disks) return fail(disks.error());
    std::vector<SourceDescriptor> out;
    out.reserve(disks->size());
    for (const auto& d : *disks) out.push_back(fromDisk(d));
    std::stable_sort(out.begin(), out.end(), [](const SourceDescriptor& a, const SourceDescriptor& b) {
        if (a.group != b.group) return static_cast<int>(a.group) < static_cast<int>(b.group);
        return a.name < b.name;
    });
    return out;
}

Expected<SourceDescriptor> describeImageFile(const std::filesystem::path& file, const std::string& passphrase) {
    std::error_code ec;
    const auto abs = std::filesystem::absolute(file, ec);
    if (!std::filesystem::is_regular_file(abs, ec)) return fail(ErrorCategory::NotFound, abs.string() + " is not a regular file");

    SourceDescriptor s;
    s.id = "image:" + abs.string();
    s.kind = SourceKind::ImageFile;
    s.group = SourceGroup::Images;
    s.name = abs.filename().string();
    s.title = s.name;
    s.path = abs.string();
    s.icon = "image";

    ImageDescriptor img;
    img.path = abs;
    auto fmt = image::detectVdiskFormat(abs);
    if (!fmt) return fail(fmt.error());
    img.format = *fmt;
    img.formatName = std::string(image::toString(*fmt));

    std::vector<std::string> sub{abs.parent_path().filename().string()};
    std::vector<std::string> id;

    if (*fmt == image::VdiskFormat::Stein) {
        auto info = image::imageInfo(abs, passphrase);
        if (!info) return fail(info.error());
        img.variant = std::to_string(info->header.version) + "." + std::to_string(info->header.versionMinor);
        img.virtualSize = info->header.totalSize;
        img.compressed = info->header.compression != image::Compression::None;
        img.encrypted = info->encrypted;
        img.locked = info->encrypted && !info->unlocked;
        img.complete = info->complete;
        img.segments = info->segments.size();
        if (info->imageHashHex) img.storedHash = *info->imageHashHex;
        const auto& src = info->manifest.get("source");
        img.sourceName = src.get("name").asString();
        img.sourceIdentity = src.get("identity").asString();
        img.created = info->manifest.get("created").asString();
        if (auto prov = provenanceFromSource(src)) {
            img.partitionImage = true;
            img.provenanceText = prov->text();
        }
        if (!info->complete) img.notes.push_back("incomplete: the writer did not finish; missing chunks read as errors");
        for (const auto& seg : info->segments)
            if (!seg.trailer) img.notes.push_back(seg.path.filename().string() + ": no trailer, recovered by scanning");
        if (img.partitionImage) sub.push_back("partition");
        sub.push_back(img.compressed ? "LZ4" : "uncompressed");
        sub.push_back(img.locked ? "locked" : img.complete ? "complete" : "incomplete");
        id.push_back("stein " + img.variant + (img.partitionImage ? " · partition image" : " · whole-device image"));
        if (img.partitionImage) id.push_back(img.provenanceText);
        else if (!img.sourceName.empty()) id.push_back("source " + img.sourceName);
        if (!img.sourceIdentity.empty()) id.push_back(img.sourceIdentity);
        id.push_back(sizeText(img.virtualSize));
        if (!img.storedHash.empty()) id.push_back("SHA-256 recorded");
        if (img.encrypted) id.push_back(std::to_string(info->keySlots) + " key slot(s)");
    } else if (*fmt == image::VdiskFormat::Raw) {
        if (auto split = image::findSplitRaw(abs)) {
            img.virtualSize = split->totalBytes;
            img.segments = split->members.size();
            sub.push_back("split raw");
            sub.push_back(std::to_string(img.segments) + " parts");
        } else {
            img.virtualSize = std::filesystem::file_size(abs, ec);
            sub.push_back("raw");
        }
        id.push_back("raw image");
        id.push_back(sizeText(img.virtualSize));
    } else {
        image::VdiskInfo vi;
        auto dev = image::openVdisk(abs, &vi);
        if (!dev) return fail(dev.error());
        img.variant = vi.variant;
        img.virtualSize = vi.virtualSize;
        img.compressed = vi.compressed;
        img.segments = std::max<std::size_t>(1, vi.files.size());
        img.notes = vi.notes;
        img.storedHash = !vi.storedMd5.empty() ? vi.storedMd5 : vi.storedSha1;
        sub.push_back(img.formatName);
        if (img.segments > 1) sub.push_back(std::to_string(img.segments) + " segments");
        id.push_back(img.formatName + (vi.variant.empty() ? "" : " (" + vi.variant + ")"));
        if (!vi.storedMd5.empty()) id.push_back("stored MD5 recorded");
        id.push_back(sizeText(img.virtualSize) + " virtual");
        id.push_back("opened read-only");
    }
    s.sizeBytes = img.virtualSize;
    s.subtitle = join(sub);
    s.identityLine = join(id);
    s.image = std::move(img);
    return s;
}

bool isElevated() { return platform::current().isElevated(); }

} // namespace drstein::core
