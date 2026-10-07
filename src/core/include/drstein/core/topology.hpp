// SPDX-License-Identifier: MIT
// The Topology view: the probe tree as rows, the byte-proportional bar as
// segments, one node's facts as a details card. Pure functions over
// probe::Node; nothing here touches the device except usageOf().
#pragma once

#include "stein/core/units.hpp"
#include "stein/fs/reader.hpp"
#include "stein/layout/node.hpp"
#include "stein/platform/platform.hpp"
#include "stein/probe/topology.hpp"

#include <optional>
#include <string>
#include <vector>

namespace drstein::core {

// Child indices from the root; {} is the root. Stable while the tree object lives.
using NodePath = std::vector<int>;
const stein::probe::Node* nodeAt(const stein::probe::Node& root, const NodePath& path);
NodePath parentOf(const NodePath& path);

struct TopologyRow {
    NodePath path;
    int depth = 0;
    std::string kindLabel;         // "Device", "Partition 2", "Free", "Decrypted", "Volume", "Metadata"
    std::string osDevice;          // "disk4s2", "sda1" for partitions of a real disk; "" otherwise
    std::string mountpoint;        // where the OS has it mounted; "" when it does not
    std::string name;              // GPT name, fs label, volume name; "" for the device (the UI shows the source title)
    std::string content;           // "FAT32 "EFI" · clean", "LUKS2 → LVM2 → ext4", "GPT · 128 entries"
    std::string sizeText;
    stein::ByteCount sizeBytes = 0;
    stein::Region region;
    stein::layout::Validity health = stein::layout::Validity::Ok;
    std::string healthText;        // "OK", "1 warning", "2 errors", "" for free space
    int segment = -1;              // index into segments(); nested nodes inherit their partition's
    bool isMetadata = false;       // synthesized from the table's metadata regions; expert mode only
    int metadataIndex = -1;        // which metadata region, when isMetadata
    bool canBrowse = false, canInspect = false, canRepair = false, canMount = false, isLockedContainer = false;
    double usedFraction = -1;      // from the superblock when it says; -1 unknown
};
std::vector<TopologyRow> topologyRows(const stein::probe::Node& root, bool expertMode);

struct Segment {
    NodePath path;
    stein::Region region;
    std::string label;             // "EFI · 512 MiB", "Linux filesystem · LUKS2 → LVM2 → ext4 · 400 GB"
    bool isFree = false;
    int colorIndex = 0;            // cycles through the theme's partition palette
};
std::vector<Segment> segments(const stein::probe::Node& root);

struct DetailRow {
    std::string key, value;
    bool mono = true;
};
struct NodeDetails {
    std::string kindLabel, title, regionText;
    std::vector<DetailRow> rows;
    std::vector<stein::probe::Note> notes;
    std::optional<double> usedFraction;
    std::string usedLabel, usedText;              // "ext4 used", "243.9 GB of 399.9 GB"
    std::vector<stein::fs::SubvolumeInfo> subvolumes;
    bool canBrowse = false, canInspect = false, canRepair = false, canMount = false, isLockedContainer = false;
    std::string osDevice, mountpoint;             // see TopologyRow
};
// Fills osDevice / mountpoint on partition rows of a real disk from the OS mount table.
struct OsMount {
    std::string device;            // "/dev/disk4s2"
    std::string mountpoint;        // "/Volumes/UDisk"
    std::string fsType;
    bool readOnly = false;
};
void annotateOsDevices(std::vector<TopologyRow>& rows, const stein::probe::Node& root, const stein::platform::DiskInfo& disk,
                       std::string_view platformName, const std::vector<OsMount>& mounts);
void annotateOsDevice(NodeDetails& details, const stein::probe::Node& root, const NodePath& path, const stein::platform::DiskInfo& disk,
                      std::string_view platformName, const std::vector<OsMount>& mounts);
NodeDetails nodeDetails(const stein::probe::Node& root, const NodePath& path);
// Details of a synthesized metadata row (expert mode).
NodeDetails metadataDetails(const stein::probe::Node& root, int metadataIndex);

// Usage computed from the allocation bitmap (L1) when the superblock does not
// say. Reads the bitmap; call it off the GUI thread for big filesystems.
struct Usage {
    stein::ByteCount usedBytes = 0, totalBytes = 0;
    double fraction() const { return totalBytes ? static_cast<double>(usedBytes) / static_cast<double>(totalBytes) : -1; }
};
stein::Expected<Usage> usageOf(const stein::probe::Node& node);

// Helpers shared with other views.
std::string contentChain(const stein::probe::Node& node);          // "LUKS2 → LVM2 → ext4"
std::string contentSummary(const stein::probe::Node& node);        // "FAT32 "EFI" · clean"
std::string partitionTypeText(const stein::pt::Partition& p);      // "EF00 · EFI System" / "0x83 · Linux"
std::string displayName(const stein::probe::Node& node);           // row name as above
std::string kindLabel(const stein::probe::Node& node);
std::string schemeName(stein::pt::TableType type);                 // "GPT", "MBR", "APM"
// "512 MiB" when the size is a round binary number, "400 GB" otherwise: how people name partitions.
std::string sizeBinaryOrDecimal(stein::ByteCount bytes);

} // namespace drstein::core
