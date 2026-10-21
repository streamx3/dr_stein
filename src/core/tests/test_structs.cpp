// SPDX-License-Identifier: MIT
#include "drstein/core/structs.hpp"
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

const StructRef* byLabelPart(const std::vector<StructRef>& refs, const std::string& part) {
    for (const auto& r : refs)
        if (r.label.find(part) != std::string::npos) return &r;
    return nullptr;
}

const FieldRow* byName(const std::vector<FieldRow>& rows, const std::string& name) {
    for (const auto& r : rows)
        if (r.name == name) return &r;
    return nullptr;
}

} // namespace

TEST_CASE("the device node lists the table's structures") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    auto refs = structuresFor(tree, {});
    REQUIRE(refs.size() >= 3);
    for (const auto& r : refs) {
        CHECK(r.source == StructSource::Table);
        CHECK(r.id.starts_with("table/"));
        CHECK(r.shownSize % 512 == 0);
        CHECK(r.shownSize >= r.node.size);
        CHECK(r.where.starts_with("LBA "));
        CHECK(!r.validity.empty());
    }
    const auto* header = byLabelPart(refs, "primary");
    REQUIRE(header);
    CHECK(header->offset == 512);
    CHECK(header->validity.starts_with("all "));

    auto fields = fieldRows(*header);
    REQUIRE(!fields.empty());
    const auto* sig = byName(fields, "signature");
    REQUIRE(sig);
    CHECK(sig->offset == 512);
    CHECK(sig->size == 8);
    CHECK(sig->value == "EFI PART");
    CHECK(sig->editable);
    auto at = fieldAt(fields, 515);
    REQUIRE(at);
    CHECK(fields[*at].name == "signature");
    CHECK(!fieldAt(fields, 100));
}

TEST_CASE("a partition's content lists the filesystem's structures with offsets on the slice") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    auto refs = structuresFor(tree, {0});
    REQUIRE(!refs.empty());
    CHECK(refs[0].source == StructSource::Content);
    CHECK(refs[0].displayBase == disk.part1Offset);
    CHECK(refs[0].device == tree.children[0].device);
    auto fields = fieldRows(refs[0]);
    CHECK(!fields.empty());
}

TEST_CASE("field values encode by type") {
    FieldRow u32;
    u32.type = layout::FieldType::U32;
    u32.size = 4;
    u32.value = "92";
    std::byte le[4] = {std::byte{92}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto r = encodeFieldValue(u32, le, "93");
    REQUIRE(r);
    CHECK(std::to_integer<int>((*r)[0]) == 93);
    CHECK(encodeFieldValue(u32, le, "0x10").value()[0] == std::byte{0x10});
    CHECK(!encodeFieldValue(u32, le, "abc"));
    CHECK(!encodeFieldValue(u32, le, "4294967296"));

    // Big-endian is inferred from the printed value.
    FieldRow be;
    be.type = layout::FieldType::U16;
    be.size = 2;
    be.value = "1";
    std::byte bytes[2] = {std::byte{0}, std::byte{1}};
    auto r2 = encodeFieldValue(be, bytes, "2");
    REQUIRE(r2);
    CHECK((*r2)[0] == std::byte{0});
    CHECK((*r2)[1] == std::byte{2});

    FieldRow ascii;
    ascii.type = layout::FieldType::Ascii;
    ascii.size = 8;
    std::byte cur[8] = {std::byte{'E'}, std::byte{'F'}, std::byte{'I'}, std::byte{' '}, std::byte{'P'}, std::byte{'A'}, std::byte{'R'}, std::byte{'T'}};
    auto r3 = encodeFieldValue(ascii, cur, "ABC");
    REQUIRE(r3);
    CHECK((*r3)[0] == std::byte{'A'});
    CHECK((*r3)[3] == std::byte{0});
    CHECK(!encodeFieldValue(ascii, cur, "longer than eight"));

    FieldRow guid;
    guid.type = layout::FieldType::Guid;
    guid.size = 16;
    std::byte zero[16] = {};
    auto r4 = encodeFieldValue(guid, zero, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
    REQUIRE(r4);
    CHECK((*r4)[0] == std::byte{0x28});   // mixed-endian on disk
    CHECK((*r4)[3] == std::byte{0xC1});

    FieldRow bytesField;
    bytesField.type = layout::FieldType::Bytes;
    bytesField.size = 2;
    auto r5 = encodeFieldValue(bytesField, std::span<const std::byte>(zero, 2), "55 AA");
    REQUIRE(r5);
    CHECK((*r5)[0] == std::byte{0x55});
    CHECK((*r5)[1] == std::byte{0xAA});
}

TEST_CASE("edits live on an overlay until written") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    auto refs = structuresFor(tree, {});
    const auto* header = byLabelPart(refs, "primary");
    REQUIRE(header);
    StructEditor editor(disk.device, *header);
    REQUIRE(editor.load());
    CHECK(editor.dirtyBytes() == 0);
    auto fields = fieldRows(editor.current());
    const auto* sig = byName(fields, "signature");
    REQUIRE(sig);

    // Break the signature: the overlay changes, the device does not, the tree flags it.
    REQUIRE(editor.setField(*sig, "EFI PARS"));
    CHECK(editor.dirtyBytes() == 1);
    CHECK(editor.isDirty(512 + 7));
    CHECK(!editor.isDirty(512));
    auto regions = editor.dirtyRegions();
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].offset == 519);
    CHECK(regions[0].length == 1);
    std::byte onDevice[8];
    REQUIRE(disk.device->readAt(512, onDevice));
    CHECK(onDevice[7] == std::byte{'T'});
    CHECK(editor.current().validity != header->validity);   // re-described: now flagged (or the table fell back to the backup)

    editor.revert();
    CHECK(editor.dirtyBytes() == 0);
    CHECK(editor.current().validity == header->validity);

    // A harmless edit written through: the revision field keeps its value when set to the same.
    const auto* rev = byName(fields, "revision");
    REQUIRE(rev);
    REQUIRE(editor.setByte(512 + 20, 0x01));   // byte 20: reserved
    CHECK(editor.dirtyBytes() == 1);
    REQUIRE(editor.commitTo(*disk.device));
    CHECK(editor.dirtyBytes() == 0);
    std::byte after[1];
    REQUIRE(disk.device->readAt(512 + 20, after));
    CHECK(after[0] == std::byte{0x01});
}

TEST_CASE("structures round-trip through a sparse piece") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probed(disk.device);
    auto refs = structuresFor(tree, {});
    const auto* header = byLabelPart(refs, "primary");
    REQUIRE(header);
    drstein::test::TempDir tmp;
    StructEditor editor(disk.device, *header);
    REQUIRE(editor.load());
    REQUIRE(editor.saveTo(tmp.file("header.sparse")));
    REQUIRE(editor.setByte(512 + 20, 0x7F));
    CHECK(editor.dirtyBytes() == 1);
    REQUIRE(editor.restoreFrom(tmp.file("header.sparse")));
    CHECK(editor.dirtyBytes() == 0);
}

TEST_CASE("exFAT: the boot checksum is a sector-wide field; setting it refills the sector") {
    auto dev = drstein::test::loadFixture("fs/exfat.sparse");
    auto tree = probed(dev);
    auto refs = structuresFor(tree, {});
    const auto* region = byLabelPart(refs, "main boot region");
    REQUIRE(region);
    CHECK(region->source == StructSource::Content);
    CHECK(region->validity.starts_with("all "));
    StructEditor ed(dev, *region);
    REQUIRE(ed.load());
    auto fields = fieldRows(ed.current());
    const auto* drive = byName(fields, "drive_select");
    const auto* checksum = byName(fields, "checksum");
    REQUIRE(drive);
    REQUIRE(checksum);
    CHECK(checksum->type == stein::layout::FieldType::Crc32);
    CHECK(checksum->size == 512);
    CHECK(checksum->editable);
    CHECK(checksum->message.empty());

    // Edit a boot-sector byte: the checksum field (in another sector of the same region) flags the fix.
    REQUIRE(ed.setField(*drive, "129"));
    fields = fieldRows(ed.current());
    checksum = byName(fields, "checksum");
    REQUIRE(checksum);
    CHECK(checksum->validity == stein::layout::Validity::Error);
    const auto at = checksum->message.find("computed 0x");
    REQUIRE(at != std::string::npos);
    const std::string value = checksum->message.substr(at + 9);
    REQUIRE(ed.setField(*checksum, value));
    fields = fieldRows(ed.current());
    checksum = byName(fields, "checksum");
    REQUIRE(checksum);
    CHECK(checksum->validity == stein::layout::Validity::Ok);
    CHECK(ed.current().validity.starts_with("all "));
    // Every 32-bit word of the sector carries the value.
    const auto bytes = ed.bytes();
    const std::size_t start = static_cast<std::size_t>(checksum->offset - ed.current().shownOffset);
    const std::uint32_t expected = static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
    for (std::size_t i = 0; i < 512; i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 4; k-- > 0;) w = (w << 8) | std::to_integer<std::uint32_t>(bytes[start + i + k]);
        CHECK(w == expected);
    }
    CHECK(ed.dirtyBytes() > 1);   // the drive_select byte and most of the checksum sector
}

