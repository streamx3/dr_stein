// SPDX-License-Identifier: MIT
#include "drstein/core/source.hpp"
#include "drstein/core/topology.hpp"
#include "fixtures.hpp"

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

namespace {

probe::Node probed(const std::shared_ptr<BlockDevice>& device) {
    auto tree = probe::probe(device);
    REQUIRE(tree);
    return std::move(*tree);
}

const TopologyRow* rowNamed(const std::vector<TopologyRow>& rows, const std::string& name) {
    for (const auto& r : rows)
        if (r.name == name) return &r;
    return nullptr;
}

} // namespace

TEST_CASE("a GPT disk becomes rows, segments and details") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);

    auto rows = topologyRows(tree, false);
    REQUIRE(rows.size() >= 4);
    CHECK(rows[0].depth == 0);
    CHECK(rows[0].kindLabel == "Device");
    CHECK(rows[0].content == "GPT · 128 entries");
    CHECK(rows[0].name.empty());

    const auto* root = rowNamed(rows, "root");
    REQUIRE(root);
    CHECK(root->kindLabel == "Partition 1");
    CHECK(root->depth == 1);
    CHECK(root->content.starts_with("ext4"));
    CHECK(root->canBrowse);
    CHECK(root->canInspect);
    CHECK(root->healthText == "OK");
    CHECK(root->usedFraction >= 0);
    CHECK(root->segment >= 0);

    const auto* data = rowNamed(rows, "Data");
    REQUIRE(data);
    CHECK(data->kindLabel == "Partition 2");
    CHECK(data->content.starts_with("FAT32"));
    CHECK(data->segment != root->segment);

    const auto* free = rowNamed(rows, "Free space");
    REQUIRE(free);
    CHECK(free->healthText.empty());
    CHECK(free->content == "—");

    auto segs = segments(tree);
    int partitions = 0, frees = 0;
    for (const auto& s : segs) (s.isFree ? frees : partitions)++;
    CHECK(partitions == 2);
    CHECK(frees >= 1);
    CHECK(segs.front().label.find("root") != std::string::npos);
    CHECK(segs.front().label.find("ext4") != std::string::npos);
    CHECK(segs.front().region.offset == disk.part1Offset);

    auto d = nodeDetails(tree, root->path);
    CHECK(d.kindLabel == "Partition 1");
    CHECK(d.title == "root");
    CHECK(d.regionText.starts_with("LBA 2 048"));
    bool typeRow = false, contentRow = false;
    for (const auto& r : d.rows) {
        if (r.key == "type") {
            typeRow = true;
            CHECK(r.value == "8300 · Linux filesystem");
        }
        if (r.key == "content") contentRow = true;
    }
    CHECK(typeRow);
    CHECK(contentRow);
    CHECK(d.usedFraction.has_value());
    CHECK(d.usedLabel == "ext4 used");
    CHECK(d.canBrowse);

    auto dev = nodeDetails(tree, {});
    CHECK(dev.kindLabel == "Device");
    bool guid = false;
    for (const auto& r : dev.rows)
        if (r.key == "disk GUID") guid = true;
    CHECK(guid);
}

TEST_CASE("expert mode adds the table's metadata regions") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    auto plain = topologyRows(tree, false);
    auto expert = topologyRows(tree, true);
    const auto regions = tree.table->metadataRegions();   // GPT: PMBR, primary header, primary entries, backup entries, backup header
    CHECK(expert.size() == plain.size() + regions.size());
    int meta = 0;
    for (const auto& r : expert)
        if (r.isMetadata) {
            ++meta;
            CHECK(r.kindLabel == "Metadata");
            CHECK(r.canInspect);
            CHECK(r.content.starts_with("LBA "));
        }
    CHECK(meta == static_cast<int>(regions.size()));
    CHECK(rowNamed(expert, "Protective MBR"));
    CHECK(rowNamed(expert, "GPT primary header"));
    CHECK(rowNamed(expert, "GPT primary entries"));
    CHECK(rowNamed(expert, "GPT backup entries"));
    CHECK(rowNamed(expert, "GPT backup header"));
    auto md = metadataDetails(tree, 1);
    CHECK(md.kindLabel == "Metadata");
    CHECK(!md.rows.empty());
}

TEST_CASE("node paths address the tree") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    CHECK(nodeAt(tree, {}) == &tree);
    REQUIRE(nodeAt(tree, {0}));
    CHECK(nodeAt(tree, {0}) == &tree.children[0]);
    CHECK(nodeAt(tree, {99}) == nullptr);
    CHECK(parentOf({2, 1}) == NodePath{2});
    CHECK(parentOf({}).empty());
}

TEST_CASE("usage comes from the superblock or the allocation map") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    const probe::Node* ext = nullptr;
    const probe::Node* fat = nullptr;
    for (const auto& c : tree.children) {
        if (c.partition && c.partition->index == 1) ext = &c;
        if (c.partition && c.partition->index == 2) fat = &c;
    }
    REQUIRE(ext);
    REQUIRE(fat);
    auto u1 = usageOf(*ext);
    REQUIRE(u1);
    CHECK(u1->totalBytes > 0);
    CHECK(u1->usedBytes <= u1->totalBytes);
    auto u2 = usageOf(*fat);   // FAT has no used count in its boot sector; the FAT itself is read
    REQUIRE(u2);
    CHECK(u2->totalBytes > 0);
    CHECK(u2->fraction() >= 0);
}

TEST_CASE("a whole-device filesystem has one segment and no table rows") {
    auto fat = drstein::test::loadFixture("pt/fat_whole.sparse");
    auto tree = probed(fat);
    auto rows = topologyRows(tree, true);
    CHECK(rows[0].content.starts_with("FAT"));
    for (const auto& r : rows) CHECK(!r.isMetadata);
    auto segs = segments(tree);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].path.empty());
}

TEST_CASE("APM: the map and the free slots get the OS's device names, like lsblk") {
    // parted fixture: slot 1 the map, 2 "Data", 3 "primary", 4 and 5 Apple_Free. Linux names all five.
    auto tree = probed(drstein::test::loadFixture("pt/apm_basic.sparse"));
    auto rows = topologyRows(tree, false);
    const auto* map = rowNamed(rows, "Apple partition map");
    REQUIRE(map != nullptr);
    CHECK(map->isMetadata);
    CHECK(map->kindLabel == "Partition 1");
    CHECK(map->region.offset == 512);
    CHECK(map->sizeBytes == 63 * 512);
    platform::DiskInfo sda;
    sda.osPath = "/dev/sda";
    sda.kernelName = "sda";
    annotateOsDevices(rows, tree, sda, "linux", {});
    CHECK(rowNamed(rows, "Apple partition map")->osDevice == "sda1");
    CHECK(rowNamed(rows, "Data")->osDevice == "sda2");
    CHECK(rowNamed(rows, "primary")->osDevice == "sda3");
    std::vector<std::string> freeDevices;
    for (const auto& r : rows)
        if (r.kindLabel == "Free") freeDevices.push_back(r.osDevice);
    // Both gaps are Apple_Free slots, so both are rows even though the first is under a MiB.
    REQUIRE(freeDevices.size() == 2);
    CHECK(freeDevices[0] == "sda4");
    CHECK(freeDevices[1] == "sda5");
    CHECK(rows[1].name == "Apple partition map");   // right after the device, as the OS numbers it
    for (const auto& r : rows)
        if (r.kindLabel == "Free") {
            auto d = nodeDetails(tree, r.path);
            annotateOsDevice(d, tree, r.path, sda, "linux", {});
            CHECK(d.osDevice == "/dev/" + r.osDevice);
            CHECK(d.rows.front().key == "device");
            break;
        }
    auto d = metadataDetails(tree, 0);
    CHECK(d.title == "Apple partition map");
    CHECK(d.kindLabel == "Partition 1");
    // Expert mode adds nothing for APM: its one metadata region is the map row already.
    CHECK(topologyRows(tree, true).size() == rows.size());
}

TEST_CASE("partition device names follow the platform's convention") {
    platform::DiskInfo mac;
    mac.osPath = "/dev/rdisk4";
    mac.kernelName = "disk4";
    CHECK(partitionKernelName(mac, 2, "macos") == "disk4s2");
    CHECK(partitionOsPath(mac, 2, "macos") == "/dev/disk4s2");
    platform::DiskInfo sda;
    sda.osPath = "/dev/sda";
    sda.kernelName = "sda";
    CHECK(partitionOsPath(sda, 1, "linux") == "/dev/sda1");
    platform::DiskInfo nvme;
    nvme.osPath = "/dev/nvme0n1";
    nvme.kernelName = "nvme0n1";
    CHECK(partitionOsPath(nvme, 3, "linux") == "/dev/nvme0n1p3");
    platform::DiskInfo win;
    win.osPath = "\\\\.\\PhysicalDrive0";
    CHECK(partitionOsPath(win, 1, "windows").empty());

    auto disk = drstein::test::compositeDisk();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    auto rows = topologyRows(*tree, false);
    std::vector<OsMount> mounts{{"/dev/disk4s2", "/Volumes/Data", "msdos", false}};
    annotateOsDevices(rows, *tree, mac, "macos", mounts);
    bool found = false;
    for (const auto& r : rows)
        if (r.name == "Data") {
            found = true;
            CHECK(r.osDevice == "disk4s2");
            CHECK(r.mountpoint == "/Volumes/Data");
        }
    CHECK(found);
    for (const auto& r : rows)
        if (r.name == "Data") {
            auto d = nodeDetails(*tree, r.path);
            annotateOsDevice(d, *tree, r.path, mac, "macos", mounts);
            CHECK(d.rows.front().key == "device");
            CHECK(d.rows.front().value == "/dev/disk4s2");
            CHECK(d.mountpoint == "/Volumes/Data");
            CHECK(!d.canMount);
        }
}

TEST_CASE("a filesystem that can fix its own metadata is repairable at the content level") {
    auto dev = drstein::test::loadFixture("exfatfs/exfat.sparse");
    std::byte one{0x81};
    REQUIRE(dev->writeAt(0x6F, std::span<const std::byte>(&one, 1)));   // boot sector byte: checksum fails, backup drifts
    auto tree = probe::probe(dev);
    REQUIRE(tree);
    auto rows = topologyRows(*tree, false);
    REQUIRE(!rows.empty());
    CHECK(rows[0].canRepair);
    auto d = nodeDetails(*tree, {});
    CHECK(d.canRepair);
    CHECK(d.contentRepairable);
    bool repairableIssue = false;
    for (const auto& i : d.issues) repairableIssue = repairableIssue || (i.repairable && i.code == "exfat.boot_checksum");
    CHECK(repairableIssue);
    REQUIRE(tree->content);
    REQUIRE(tree->content->repair(*dev));
    auto again = probe::probe(dev);
    REQUIRE(again);
    CHECK(!nodeDetails(*again, {}).canRepair);
}

TEST_CASE("MBR: the extended container and its logical are rows of their own") {
    auto disk = drstein::test::mbrComposite();
    auto tree = probed(disk.device);
    auto rows = topologyRows(tree, false);
    REQUIRE(rows.size() >= 5);
    CHECK(rows[0].kindLabel == "Device");
    CHECK(rows[0].content == "MBR · 4 partitions");
    const TopologyRow* fat = nullptr;
    const TopologyRow* exfat = nullptr;
    const TopologyRow* extended = nullptr;
    const TopologyRow* logical = nullptr;
    for (const auto& r : rows) {
        if (r.kindLabel == "Partition 1") fat = &r;
        if (r.kindLabel == "Partition 2") exfat = &r;
        if (r.kindLabel == "Partition 3") extended = &r;
        if (r.kindLabel == "Partition 5") logical = &r;
    }
    REQUIRE(fat);
    CHECK(fat->content.starts_with("FAT16"));
    CHECK(fat->canBrowse);
    REQUIRE(exfat);
    CHECK(exfat->content.starts_with("exFAT"));
    CHECK(exfat->canBrowse);
    CHECK(exfat->region.offset == disk.exfatOffset);
    REQUIRE(extended);
    CHECK(extended->depth == 1);
    CHECK(extended->name == pt::types::name(pt::PartitionType::mbr(0x05)));
    CHECK(extended->content == "extended container");
    CHECK(extended->region.offset == disk.extendedOffset);
    CHECK(extended->sizeBytes == disk.extendedSize);
    CHECK(extended->segment == -1);   // the bar shows what fills it, not the container
    CHECK(!extended->canBrowse);
    CHECK(!extended->canInspect);
    REQUIRE(logical);
    CHECK(logical->depth == 1);   // the probe lists logicals beside the primaries, in disk order
    CHECK(logical->content.starts_with("ext4"));
    CHECK(logical->region.offset == disk.logicalOffset);
    CHECK(logical->sizeBytes == disk.logicalSize);
    CHECK(logical->canBrowse);
    CHECK(logical->segment >= 0);

    auto segs = segments(tree);
    int parts = 0;
    for (const auto& s : segs)
        if (!s.isFree) ++parts;
    CHECK(parts == 3);
    CHECK(segs[logical->segment].region.offset == disk.logicalOffset);

    auto de = nodeDetails(tree, extended->path);
    CHECK(de.kindLabel == "Partition 3");
    bool flagged = false;
    for (const auto& r : de.rows)
        if (r.key == "flags") {
            flagged = true;
            CHECK(r.value.find("extended") != std::string::npos);
        }
    CHECK(flagged);
    auto dl = nodeDetails(tree, logical->path);
    flagged = false;
    for (const auto& r : dl.rows)
        if (r.key == "flags") {
            flagged = true;
            CHECK(r.value.find("logical") != std::string::npos);
        }
    CHECK(flagged);
    CHECK(dl.canBrowse);

    // The OS numbers logicals from 5, like the table does.
    platform::DiskInfo sda;
    sda.osPath = "/dev/sda";
    sda.kernelName = "sda";
    annotateOsDevices(rows, tree, sda, "linux", {});
    for (const auto& r : rows) {
        if (r.kindLabel == "Partition 3") CHECK(r.osDevice == "sda3");
        if (r.kindLabel == "Partition 5") CHECK(r.osDevice == "sda5");
    }

    // Expert mode adds the MBR sector and the one EBR, named as the Hex view names the structure.
    auto expert = topologyRows(tree, true);
    CHECK(expert.size() == rows.size() + 2);
    CHECK(rowNamed(expert, "MBR sector"));
    const auto* ebr = rowNamed(expert, "EBR @ LBA " + std::to_string(disk.extendedOffset / 512));
    REQUIRE(ebr);
    CHECK(ebr->isMetadata);
    CHECK(ebr->region.offset == disk.extendedOffset);
    CHECK(ebr->sizeBytes == 512);
}

