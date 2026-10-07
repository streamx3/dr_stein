// SPDX-License-Identifier: MIT
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
