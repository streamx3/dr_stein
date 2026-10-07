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
