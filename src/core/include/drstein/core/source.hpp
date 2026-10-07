// SPDX-License-Identifier: MIT
// Sources: what the sidebar lists. A disk the OS knows about, or an image
// file the user opened. Describing a source never needs privileges.
#pragma once

#include "stein/core/error.hpp"
#include "stein/core/units.hpp"
#include "stein/image/vdisk.hpp"
#include "stein/platform/platform.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace drstein::core {

enum class SourceKind { Disk, ImageFile };
enum class SourceGroup { Internal, Removable, Virtual, Images };
std::string_view toString(SourceGroup g);   // "Internal", "Removable", "Virtual", "Images"

struct ImageDescriptor {
    std::filesystem::path path;
    stein::image::VdiskFormat format = stein::image::VdiskFormat::Raw;
    std::string formatName;        // "stein", "qcow2", "E01"...
    std::string variant;           // "1.1", "v3", "dynamic"
    stein::ByteCount virtualSize = 0;
    bool compressed = false, encrypted = false, locked = false, complete = true;
    std::size_t segments = 1;
    std::vector<std::string> notes;
    std::string sourceName, sourceIdentity, created;   // .stein manifest
    std::string storedHash;                            // sha256 (.stein) or md5 (E01), hex
    bool partitionImage = false;                       // .stein of one partition (manifest source.kind)
    std::string provenanceText;                        // "partition 2 "Data" of Samsung SSD 980 · LBA … · 0700"
};

struct SourceDescriptor {
    std::string id;                // "disk:<identity>" or "image:<absolute path>"
    SourceKind kind = SourceKind::Disk;
    SourceGroup group = SourceGroup::Internal;
    std::string name;              // sidebar first line: "nvme0n1", "ws-2026-10-04.stein"
    std::string subtitle;          // sidebar second line
    std::string title;             // identity strip: "Samsung SSD 980"
    std::string path;              // osPath or file path
    std::string identityLine;      // serial · firmware · sector sizes · flags
    stein::ByteCount sizeBytes = 0;
    std::string icon;              // "nvme", "sata", "usb", "sd", "virtual", "image"
    std::optional<stein::platform::DiskInfo> disk;
    std::optional<ImageDescriptor> image;
};

stein::Expected<std::vector<SourceDescriptor>> listDisks(stein::platform::Platform& platform);
stein::Expected<SourceDescriptor> describeImageFile(const std::filesystem::path& file, const std::string& passphrase = {});
bool isElevated();
std::string busName(stein::platform::Bus bus);   // "NVMe", "SATA", "USB"...

} // namespace drstein::core
