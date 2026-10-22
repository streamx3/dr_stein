// SPDX-License-Identifier: MIT
#include "drstein/core/mounts.hpp"
#include "drstein/core/opened.hpp"
#include "drstein/core/source.hpp"
#include "drstein/core/topology.hpp"
#include "fixtures.hpp"

#include <doctest.h>

#include <array>
#include <cstring>
#include <fstream>

using namespace drstein::core;
using namespace stein;

namespace {

probe::Node probed(const std::shared_ptr<BlockDevice>& device) {
    auto tree = probe::probe(device);
    REQUIRE(tree);
    return std::move(*tree);
}

// A "key=value" line of a fixture's oracle file (tests/fixtures/luks/*.oracle.txt, written by cryptsetup's side).
std::string oracleValue(const std::string& relativePath, const std::string& key) {
    std::ifstream in(std::string(STEIN_FIXTURE_DIR) + "/" + relativePath);
    std::string line;
    while (std::getline(in, line))
        if (line.starts_with(key + "=")) return line.substr(key.size() + 1);
    return {};
}

std::vector<std::uint8_t> fromHex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

bool hasNote(const probe::Node& node, const std::string& code) {
    for (const auto& n : node.notes)
        if (n.code == code) return true;
    return false;
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


TEST_CASE("LUKS2 through core::openSource: locked without the passphrase, a payload row with it") {
    // The app's own path: an image file described, opened and probed with the session's passphrases.
    drstein::test::TempDir tmp;
    auto fixture = drstein::test::loadFixture("luks/luks2.sparse");
    const auto file = drstein::test::writeDevice(*fixture, tmp.file("luks2.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);

    auto locked = openSource(*desc, OpenOptions{});
    REQUIRE(locked);
    auto rows = topologyRows(locked->tree, false);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].isLockedContainer);
    CHECK(!rows[0].canBrowse);
    CHECK(!rows[0].canMount);
    CHECK(rows[0].content.starts_with("LUKS2"));
    CHECK(contentChain(locked->tree) == "LUKS2");
    auto d = nodeDetails(locked->tree, {});
    CHECK(d.isLockedContainer);
    for (const auto& i : d.issues) CHECK(i.code != "luks.unsupported");

    OpenOptions wrong;
    wrong.passphrases = {"not it"};
    auto still = openSource(*desc, wrong);
    REQUIRE(still);
    rows = topologyRows(still->tree, false);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].isLockedContainer);

    OpenOptions right;
    right.passphrases = {"not it", "luks test passphrase"};
    auto open = openSource(*desc, right);
    REQUIRE(open);
    rows = topologyRows(open->tree, false);
    REQUIRE(rows.size() == 2);
    CHECK(!rows[0].isLockedContainer);
    CHECK(rows[1].kindLabel == "Decrypted");
    CHECK(rows[1].depth == 1);
    CHECK(rows[1].path == NodePath{0});
    CHECK(rows[1].canBrowse);
    CHECK(rows[1].canInspect);
    CHECK(rows[1].canMount == MountManager::available());
    CHECK(rows[1].content.starts_with("ext2"));
    CHECK(contentChain(open->tree) == "LUKS2 → ext2");
    auto pd = nodeDetails(open->tree, {0});
    CHECK(pd.kindLabel == "Decrypted");
    CHECK(pd.canBrowse);
    CHECK(pd.canMount == MountManager::available());
    CHECK(!pd.isLockedContainer);

    // The image list is for an encrypted .stein file; a raw image ignores it, and it is never
    // run through the container's KDF.
    OpenOptions apart;
    apart.imagePassphrases = {"luks test passphrase"};
    auto notForContainers = openSource(*desc, apart);
    REQUIRE(notForContainers);
    CHECK(topologyRows(notForContainers->tree, false).size() == 1);
    CHECK(nodeDetails(notForContainers->tree, {}).isLockedContainer);
}

TEST_CASE("a cached master key opens LUKS2 without a passphrase; a passphrase reports its key; Cancel leaves the container locked") {
    // The session's path after the first unlock: openSource with the key the oracle lists, no passphrase at all.
    drstein::test::TempDir tmp;
    auto fixture = drstein::test::loadFixture("luks/luks2.sparse");
    const auto file = drstein::test::writeDevice(*fixture, tmp.file("luks2.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    const std::vector<std::uint8_t> oracleKey = fromHex(oracleValue("luks/luks2.oracle.txt", "master_key"));
    REQUIRE(oracleKey.size() == 64);

    // The cache is keyed by the UUID the locked probe already shows (the one blkid prints).
    auto locked = openSource(*desc, OpenOptions{});
    REQUIRE(locked);
    REQUIRE(locked->tree.content);
    const std::string uuid = locked->tree.content->info().uuid;
    CHECK(uuid == "a3e67338-7a03-4d96-8f7d-eadb8846c668");   // luks2.dump.txt

    int reported = 0;
    OpenOptions keyed;
    keyed.masterKeys[uuid] = oracleKey;
    keyed.onUnlocked = [&](const std::string&, const std::vector<std::uint8_t>&) { ++reported; };
    auto open = openSource(*desc, keyed);
    REQUIRE(open);
    auto rows = topologyRows(open->tree, false);
    REQUIRE(rows.size() == 2);
    CHECK(!rows[0].isLockedContainer);
    CHECK(rows[1].kindLabel == "Decrypted");
    CHECK(rows[1].content.starts_with("ext2"));
    CHECK(rows[1].canBrowse);
    CHECK(contentChain(open->tree) == "LUKS2 → ext2");
    CHECK(reported == 0);   // nothing was derived, nothing to cache
    CHECK(!hasNote(open->tree, "luks.locked"));
    CHECK(!hasNote(open->tree, "luks.unlock"));
    CHECK(keyed.masterKeys[uuid] == oracleKey);   // the caller's copy is untouched; only the probe's copies are wiped

    // A stale key (the container was re-keyed, say) is skipped without a word and the passphrase does the
    // work; the key it yields comes back once, by UUID, and is the oracle's.
    OpenOptions stale;
    stale.masterKeys[uuid] = oracleKey;
    stale.masterKeys[uuid][0] ^= 0xFF;
    stale.passphrases = {"luks test passphrase"};
    std::string reportedUuid;
    std::vector<std::uint8_t> reportedKey;
    stale.onUnlocked = [&](const std::string& u, const std::vector<std::uint8_t>& k) {
        ++reported;
        reportedUuid = u;
        reportedKey = k;
    };
    auto derived = openSource(*desc, stale);
    REQUIRE(derived);
    CHECK(topologyRows(derived->tree, false).size() == 2);
    CHECK(reported == 1);
    CHECK(reportedUuid == uuid);
    CHECK(reportedKey == oracleKey);
    CHECK(!hasNote(derived->tree, "luks.unlock"));   // the stale key's digest mismatch is not a diagnostic

    // Cancel before the first slice: the container is left as found, with no luks.locked (nothing was
    // refuted), the hook was consulted and no key is reported.
    OpenOptions cancel;
    cancel.passphrases = {"luks test passphrase"};
    int polls = 0;
    cancel.cancelled = [&] {
        ++polls;
        return true;
    };
    cancel.onUnlocked = [&](const std::string&, const std::vector<std::uint8_t>&) { ++reported; };
    auto cancelled = openSource(*desc, cancel);
    REQUIRE(cancelled);
    rows = topologyRows(cancelled->tree, false);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].isLockedContainer);
    CHECK(polls > 0);
    CHECK(reported == 1);
    CHECK(!hasNote(cancelled->tree, "luks.locked"));

    // Cancel from inside the derivation (the third poll): the slot would have opened had the KDF run to
    // its end, so a locked result is the proof that Argon2 polls between its slices.
    OpenOptions late;
    late.passphrases = {"luks test passphrase"};
    polls = 0;
    late.cancelled = [&] { return ++polls >= 3; };
    auto lateCancel = openSource(*desc, late);
    REQUIRE(lateCancel);
    CHECK(topologyRows(lateCancel->tree, false).size() == 1);
    CHECK(polls >= 3);
    CHECK(!hasNote(lateCancel->tree, "luks.locked"));

    // No cancel, same passphrase: the derivation runs through, as a control.
    OpenOptions through;
    through.passphrases = {"luks test passphrase"};
    polls = 0;
    through.cancelled = [&] {
        ++polls;
        return false;
    };
    auto opened = openSource(*desc, through);
    REQUIRE(opened);
    CHECK(topologyRows(opened->tree, false).size() == 2);
    CHECK(polls >= 3);
}

TEST_CASE("a LUKS container that cannot be opened in process is not offered for unlocking; the card says why") {
    auto dev = drstein::test::loadFixture("luks/luks1.sparse");
    // LUKS1 header: cipher-mode is 32 NUL-padded bytes at offset 40. cbc-essiv is a real mode,
    // just not one the in-process path implements.
    std::array<std::byte, 32> mode{};
    const std::string_view want = "cbc-essiv:sha256";
    std::memcpy(mode.data(), want.data(), want.size());
    REQUIRE(dev->writeAt(40, mode));
    auto tree = probe::probe(dev);
    REQUIRE(tree);
    REQUIRE(tree->content);
    CHECK(tree->content->type() == fs::FsType::Luks1);
    auto rows = topologyRows(*tree, false);
    REQUIRE(rows.size() == 1);
    CHECK(!rows[0].isLockedContainer);
    auto d = nodeDetails(*tree, {});
    CHECK(!d.isLockedContainer);
    int said = 0;
    std::string why;
    for (const auto& i : d.issues)
        if (i.code == "luks.unsupported") {
            ++said;
            why = i.message;
        }
    CHECK(said == 1);
    CHECK(why.find("cbc-essiv") != std::string::npos);

    // With a passphrase the probe leaves its own note; the card still says it once and stays locked-less.
    probe::Options o;
    o.passphrases = {"luks test passphrase"};
    o.tcrypt = false;
    auto probed = probe::probe(dev, o);
    REQUIRE(probed);
    CHECK(probed->children.empty());
    d = nodeDetails(*probed, {});
    said = 0;
    for (const auto& i : d.issues)
        if (i.code == "luks.unsupported") ++said;
    CHECK(said == 1);
    CHECK(!d.isLockedContainer);
    CHECK(!topologyRows(*probed, false)[0].isLockedContainer);

    // The untouched fixture is unlockable.
    auto plain = probe::probe(drstein::test::loadFixture("luks/luks1.sparse"));
    REQUIRE(plain);
    CHECK(nodeDetails(*plain, {}).isLockedContainer);
}

TEST_CASE("mount directories are named after the label, sanitised; the mount table check is read-only") {
    CHECK(mountDirectoryName("inside_luks", "Partition 2") == "inside_luks");
    CHECK(mountDirectoryName("", "Partition 2") == "Partition 2");
    CHECK(mountDirectoryName("", "") == "volume");
    CHECK(mountDirectoryName("Photos/2024:Trip", "x") == "Photos_2024_Trip");
    CHECK(mountDirectoryName("back\\slash", "x") == "back_slash");
    CHECK(mountDirectoryName("..", "fallback") == "fallback");
    CHECK(mountDirectoryName(" .hidden ", "x") == "hidden");
    CHECK(mountDirectoryName("tab\there", "x") == "tab_here");
    CHECK(mountDirectoryName(std::string(100, 'a'), "x").size() == 64);
    CHECK(mountDirectoryName("Данные", "x") == "Данные");   // UTF-8 passes through untouched
    // A plain directory is not a mountpoint; the check reads the mount table and never touches the path.
    CHECK(!MountManager::isMounted(std::filesystem::temp_directory_path() / "drstein-not-a-mountpoint"));
#if defined(__APPLE__) || defined(__linux__)
    // The root is always one; a path through a symlink (/var -> /private/var on macOS) that is
    // not a mountpoint stays a non-mountpoint whichever way it is spelled.
    CHECK(MountManager::isMounted("/"));
    CHECK(!MountManager::isMounted("/var/drstein-not-a-mountpoint"));
#endif
    MountManager empty;
    CHECK(!empty.find("image:/x", {0}));
    CHECK(!empty.findByMountpoint("/nowhere"));
    CHECK(empty.reapEnded().empty());
}
