// SPDX-License-Identifier: MIT
#include "drstein/core/partition_edit.hpp"
#include "drstein/core/source.hpp"
#include "fixtures.hpp"

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

TEST_CASE("sector and size texts parse like the CLI") {
    CHECK(parseLba("2048s", 512).value() == 2048);
    CHECK(parseLba("1 MiB", 512).value() == 2048);
    CHECK(parseLba("1M", 4096).value() == 256);
    CHECK(!parseLba("1023", 512));            // not a multiple of the sector size
    CHECK(!parseLba("", 512));
    CHECK(!parseLba("2048x", 512));
}

TEST_CASE("partition types parse per scheme") {
    auto efi = parseType(pt::TableType::Gpt, "EF00");
    REQUIRE(efi);
    CHECK(pt::types::name(*efi) == "EFI System");
    CHECK(parseType(pt::TableType::Gpt, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B").value() == *efi);
    CHECK(!parseType(pt::TableType::Gpt, "nonsense"));
    CHECK(parseType(pt::TableType::Mbr, "83").value().mbrId == 0x83);
    CHECK(parseType(pt::TableType::Mbr, "0x07").value().mbrId == 0x07);
    CHECK(!parseType(pt::TableType::Mbr, "zz"));
    CHECK(parseType(pt::TableType::Gpt, "").value() == *pt::types::forRole(pt::Role::LinuxFilesystem, pt::TableType::Gpt));
    auto choices = partitionTypeChoices(pt::TableType::Gpt);
    CHECK(choices.size() > 20);
    bool ef = false;
    for (const auto& c : choices)
        if (c.code == "EF00") ef = true;
    CHECK(ef);
}

TEST_CASE("an edit session previews, diffs, undoes and applies") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    CHECK(!session->isRealDevice());

    auto rows = previewRows(session->base(), session->preview());
    int parts = 0;
    for (const auto& r : rows) {
        CHECK(!r.changed);
        if (!r.isFree) ++parts;
    }
    CHECK(parts == 2);
    CHECK(stackSummary(session->stack()) == "nothing pending");
    CHECK(changedRangesText(session->stack()) == "nothing yet");

    // Add in the largest free region with the defaults: a new row appears.
    AddPartitionForm add;
    add.typeText = "8300";
    add.name = "Scratch";
    REQUIRE(session->addPartition(add));
    rows = previewRows(session->base(), session->preview());
    bool fresh = false;
    for (const auto& r : rows)
        if (r.change == "new") {
            fresh = true;
            CHECK(r.name == "Scratch");
            CHECK(r.typeCode == "8300");
            CHECK(r.colorIndex == 2);
        }
    CHECK(fresh);
    auto pending = pendingRows(session->stack());
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].n == 1);
    CHECK(pending[0].destructive);   // wipes signatures in the new range
    CHECK(changedRangesText(session->stack()) != "nothing yet");

    EditPartitionForm rename;
    rename.name = "Archive";
    REQUIRE(session->updatePartition(2, rename));
    rows = previewRows(session->base(), session->preview());
    bool renamed = false;
    for (const auto& r : rows)
        if (r.change == "rename") {
            renamed = true;
            CHECK(r.name == "Data → Archive");
        }
    CHECK(renamed);
    CHECK(stackSummary(session->stack()) == "2 pending · 1 destructive");

    REQUIRE(session->deletePartition(1));
    rows = previewRows(session->base(), session->preview());
    bool deleted = false;
    for (const auto& r : rows)
        if (r.deleted) {
            deleted = true;
            CHECK(r.name == "root");
            CHECK(r.change == "delete");
        }
    CHECK(deleted);

    REQUIRE(session->undoLast());
    rows = previewRows(session->base(), session->preview());
    for (const auto& r : rows) CHECK(!r.deleted);
    CHECK(pendingRows(session->stack()).size() == 2);

    auto segs = previewSegments(session->base(), session->preview());
    CHECK(segs.size() >= baseSegments(session->base()).size());

    NullProgressSink sink;
    Progress progress(sink);
    auto applied = session->apply(progress);
    REQUIRE(applied);
    CHECK(applied->postconditionOk);
    CHECK(session->stack().empty());
    REQUIRE(session->base().table);
    CHECK(session->base().table->partitions().size() == 3);
    CHECK(session->base().table->find(2)->name == "Archive");

    // Bad input is refused before anything is pushed.
    AddPartitionForm bad;
    bad.startText = "17s";
    bad.sizeText = "1 MiB";
    bad.typeText = "zz";
    CHECK(!session->addPartition(bad));
    CHECK(session->stack().empty());
    session->clear();
}

TEST_CASE("a new table on a blank device") {
    auto blank = std::make_shared<MemoryDevice>(64 * MiB, 512);
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*blank, tmp.file("blank.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    AddPartitionForm add;
    CHECK(!session->addPartition(add));   // no table yet
    REQUIRE(session->createTable(pt::TableType::Mbr));
    add.typeText = "83";
    add.sizeText = "8 MiB";
    REQUIRE(session->addPartition(add));
    auto rows = previewRows(session->base(), session->preview());
    REQUIRE(!rows.empty());
    CHECK(rows[0].change == "new");
    CHECK(rows[0].typeCode == "0x83");
    CHECK(rows[0].sizeText == "8 MiB");

    // Logicals need an extended container first; then the library places the EBR at its start.
    AddPartitionForm logical;
    logical.typeText = "83";
    logical.logical = true;
    auto refused = session->addPartition(logical);
    REQUIRE(!refused);
    CHECK(refused.error().category() == ErrorCategory::NotFound);
    AddPartitionForm extended;
    extended.typeText = "05";
    extended.sizeText = "32 MiB";
    REQUIRE(session->addPartition(extended));
    REQUIRE(session->preview().table->find(2));
    CHECK(session->preview().table->find(2)->isExtended);
    REQUIRE(session->addPartition(logical));
    const auto* l = session->preview().table->find(5);
    REQUIRE(l);
    CHECK(l->isLogical);
    const auto* ext = session->preview().table->find(2);
    REQUIRE(ext);
    CHECK(l->ebrLba == ext->firstLba);
    CHECK(l->firstLba == ext->firstLba + pt::MbrTable::kDefaultEbrGap);
    CHECK(l->lastLba == ext->lastLba);   // an empty size runs to the end of the container
    CHECK(pendingRows(session->stack()).back().title.starts_with("Add logical partition 5: Linux"));
}

TEST_CASE("MBR: a logical goes inside the extended container, a primary stays outside it") {
    auto disk = drstein::test::mbrComposite();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("mbr.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    REQUIRE(session->preview().table);
    CHECK(session->preview().table->type() == pt::TableType::Mbr);
    const Lba extFirst = disk.extendedOffset / 512, extLast = (disk.extendedOffset + disk.extendedSize) / 512 - 1;
    const Lba firstLogicalEnd = (disk.logicalOffset + disk.logicalSize) / 512 - 1;

    // Defaults: the container's largest gap, data one gap past where the EBR will go.
    AddPartitionForm add;
    add.typeText = "83";
    add.logical = true;
    REQUIRE(session->addPartition(add));
    REQUIRE(session->preview().table->find(6));
    const pt::Partition added = *session->preview().table->find(6);   // the preview is rebuilt by every push
    CHECK(added.isLogical);
    CHECK(added.firstLba == firstLogicalEnd + 1 + pt::MbrTable::kDefaultEbrGap);
    CHECK(added.lastLba == extLast);
    REQUIRE(added.ebrLba.has_value());
    CHECK(*added.ebrLba > firstLogicalEnd);
    CHECK(*added.ebrLba < added.firstLba);
    CHECK(*added.ebrLba > extFirst);
    auto pending = pendingRows(session->stack());
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].title.starts_with("Add logical partition 6: Linux"));
    CHECK(pending[0].destructive);
    bool fresh = false;
    for (const auto& r : previewRows(session->base(), session->preview()))
        if (r.change == "new") {
            fresh = true;
            CHECK(r.index == 6);
            CHECK(r.typeCode == "0x83");
        }
    CHECK(fresh);

    // A primary with defaults takes the free tail after the container, never the container's gap.
    AddPartitionForm primary;
    primary.typeText = "0x0C";
    REQUIRE(session->addPartition(primary));
    REQUIRE(session->preview().table->find(4));
    const pt::Partition p4 = *session->preview().table->find(4);
    CHECK(!p4.isLogical);
    CHECK(p4.firstLba == extLast + 1);
    CHECK(p4.lastLba == (session->preview().table->lastUsableLba() + 1) / 2048 * 2048 - 1);
    CHECK(pendingRows(session->stack())[1].title.starts_with("Add partition: "));   // the library numbers primaries itself
    // With every primary slot taken and no gap outside the container, a primary has nowhere to go.
    AddPartitionForm fifth;
    fifth.typeText = "83";
    CHECK(!session->addPartition(fifth));
    CHECK(pendingRows(session->stack()).size() == 2);

    NullProgressSink sink;
    Progress progress(sink);
    auto applied = session->apply(progress);
    REQUIRE(applied);
    CHECK(applied->postconditionOk);
    REQUIRE(session->base().table);
    const auto& table = *session->base().table;
    CHECK(table.partitions().size() == 6);
    REQUIRE(table.find(6));
    CHECK(table.find(6)->isLogical);
    CHECK(table.find(6)->ebrLba == added.ebrLba);
    CHECK(table.find(6)->firstLba == added.firstLba);
    CHECK(table.metadataRegions().size() == 3);   // the MBR and both EBRs
    for (const auto& d : table.diagnostics()) CHECK(d.severity < layout::Validity::Error);
}

TEST_CASE("logical partitions are an MBR thing") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("gpt.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    AddPartitionForm add;
    add.typeText = "8300";
    add.logical = true;
    auto r = session->addPartition(add);
    REQUIRE(!r);
    CHECK(r.error().category() == ErrorCategory::Unsupported);
    CHECK(session->stack().empty());
}

// Review findings against 28d32fc, each on the MBR composite unless it says otherwise.

TEST_CASE("MBR: a default logical between two logicals stops before the next one's EBR") {
    auto disk = drstein::test::mbrComposite();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("mbr.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    const Lba extLast = (disk.extendedOffset + disk.extendedSize) / 512 - 1;
    const Lba firstLogicalEnd = (disk.logicalOffset + disk.logicalSize) / 512 - 1;
    constexpr Lba gap = pt::MbrTable::kDefaultEbrGap;

    // A 2 MiB logical at the container's end; the library puts its EBR one gap before its data.
    const Lba tailStart = extLast + 1 - 4096;
    AddPartitionForm tail;
    tail.typeText = "83";
    tail.logical = true;
    tail.startText = std::to_string(tailStart) + "s";
    REQUIRE(session->addPartition(tail));
    REQUIRE(session->preview().table->find(6));
    const pt::Partition six = *session->preview().table->find(6);
    CHECK(six.firstLba == tailStart);
    CHECK(six.lastLba == extLast);
    REQUIRE(six.ebrLba.has_value());
    CHECK(*six.ebrLba == tailStart - gap);

    // The 6 MiB between logical 5 and that EBR is now the largest candidate. The default end must
    // stop at the EBR's sector, or the library has no room for the chain and refuses the add.
    AddPartitionForm add;
    add.typeText = "83";
    add.logical = true;
    REQUIRE(session->addPartition(add));
    const auto& table = *session->preview().table;
    const pt::Partition* mid = nullptr;
    for (const auto& p : table.partitions())
        if (p.isLogical && p.firstLba == firstLogicalEnd + 1 + gap) mid = &p;
    REQUIRE(mid);
    CHECK(mid->index == 6);   // its place in the chain: behind 5, in front of the tail one
    CHECK(mid->lastLba == *six.ebrLba - 1);
    CHECK(mid->ebrLba == firstLogicalEnd + 1);
    REQUIRE(table.find(7));
    CHECK(table.find(7)->firstLba == tailStart);
    CHECK(table.find(7)->ebrLba == six.ebrLba);
    auto pending = pendingRows(session->stack());
    REQUIRE(pending.size() == 2);
    CHECK(pending[0].title.starts_with("Add logical partition 6: Linux"));
    // Inserted in front of an existing logical: the slot it gets is one the table API calls busy, so
    // the text names none rather than the wrong one; the preview row carries the real slot.
    CHECK(pending[1].title.starts_with("Add logical partition: Linux"));
    int fresh = 0;
    for (const auto& r : previewRows(session->base(), session->preview())) {
        CHECK(!r.deleted);
        if (r.change == "new") ++fresh;
        if (r.index == 5) CHECK(!r.changed);
    }
    CHECK(fresh == 2);

    NullProgressSink sink;
    Progress progress(sink);
    auto applied = session->apply(progress);
    REQUIRE(applied);
    CHECK(applied->postconditionOk);
    REQUIRE(session->base().table);
    const auto& written = *session->base().table;
    CHECK(written.partitions().size() == 6);
    REQUIRE(written.find(6));
    REQUIRE(written.find(7));
    CHECK(written.find(6)->firstLba == firstLogicalEnd + 1 + gap);
    CHECK(written.find(6)->lastLba == tailStart - gap - 1);
    CHECK(written.find(6)->ebrLba == firstLogicalEnd + 1);
    CHECK(written.find(7)->firstLba == tailStart);
    CHECK(written.find(7)->ebrLba == tailStart - gap);
    CHECK(written.metadataRegions().size() == 4);   // the MBR and three EBRs
    for (const auto& d : written.diagnostics()) CHECK(d.severity < layout::Validity::Error);
}

TEST_CASE("MBR: a gap that cannot hold an EBR and a MiB of data is no place for a logical") {
    auto disk = drstein::test::mbrComposite();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("mbr.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    const Lba extLast = (disk.extendedOffset + disk.extendedSize) / 512 - 1;
    const Lba firstLogicalEnd = (disk.logicalOffset + disk.logicalSize) / 512 - 1;
    constexpr Lba gap = pt::MbrTable::kDefaultEbrGap;

    // Fill the container's tail, EBR gap included: what is left inside is the first logical's own
    // EBR gap at the container's start and the new one's at its end, one gap each.
    AddPartitionForm fill;
    fill.typeText = "83";
    fill.logical = true;
    fill.startText = std::to_string(firstLogicalEnd + 1 + gap) + "s";
    REQUIRE(session->addPartition(fill));
    REQUIRE(session->preview().table->find(6));
    CHECK(session->preview().table->find(6)->ebrLba == firstLogicalEnd + 1);
    CHECK(session->preview().table->find(6)->lastLba == extLast);

    // Before: the first gap "won" as the largest candidate and produced a one-sector logical on top
    // of partition 5, refused by the library for the overlap. The honest answer is "no room".
    AddPartitionForm add;
    add.typeText = "83";
    add.logical = true;
    auto refused = session->addPartition(add);
    REQUIRE(!refused);
    CHECK(refused.error().category() == ErrorCategory::OutOfRange);
    CHECK(refused.error().message() == "no free space inside the extended partition");
    CHECK(pendingRows(session->stack()).size() == 1);
}

TEST_CASE("MBR: a logical added in front takes slot 5; rows and colours follow the partitions, not the slots") {
    auto disk = drstein::test::mbrComposite();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("mbr.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    const Lba extFirst = disk.extendedOffset / 512, extLast = (disk.extendedOffset + disk.extendedSize) / 512 - 1;
    constexpr Lba gap = pt::MbrTable::kDefaultEbrGap;
    const Lba tailStart = extLast + 1 - 4096;
    NullProgressSink sink;
    Progress progress(sink);

    // Round one: a logical at the container's end, then the ext4 one in front of it deleted, each
    // applied on its own. The one left is the chain's head now, so the rewritten chain puts its EBR
    // in the container's first sector and it comes back from the re-read as slot 5.
    AddPartitionForm tail;
    tail.typeText = "83";
    tail.logical = true;
    tail.startText = std::to_string(tailStart) + "s";
    REQUIRE(session->addPartition(tail));
    {
        auto applied = session->apply(progress);
        REQUIRE(applied);
        CHECK(applied->postconditionOk);
    }
    REQUIRE(session->base().table);
    REQUIRE(session->base().table->find(6));
    CHECK(session->base().table->find(6)->firstLba == tailStart);
    REQUIRE(session->deletePartition(5));
    {
        auto applied = session->apply(progress);
        REQUIRE(applied);
        CHECK(applied->postconditionOk);
    }
    REQUIRE(session->base().table);
    REQUIRE(session->base().table->find(5));
    CHECK(session->base().table->find(5)->firstLba == tailStart);
    CHECK(session->base().table->find(5)->ebrLba == extFirst);
    CHECK(!session->base().table->find(6));

    // Round two: a default logical lands in the space in front of it and becomes slot 5; the old
    // one moves to slot 6 without changing. Its end leaves the default EBR gap before the old one's
    // data, where the chain will put that EBR.
    AddPartitionForm add;
    add.typeText = "83";
    add.logical = true;
    REQUIRE(session->addPartition(add));
    const auto& table = *session->preview().table;
    REQUIRE(table.find(5));
    REQUIRE(table.find(6));
    CHECK(table.find(5)->firstLba == extFirst + gap);
    CHECK(table.find(5)->lastLba == tailStart - gap - 1);
    CHECK(table.find(6)->firstLba == tailStart);
    CHECK(table.find(6)->lastLba == extLast);
    CHECK(pendingRows(session->stack()).back().title.starts_with("Add logical partition: Linux"));
    // Base colours in base order: FAT16 0, exFAT 1, the tail logical 2; the newcomer continues.
    bool sawNew = false, sawOld = false;
    for (const auto& r : previewRows(session->base(), session->preview())) {
        CHECK(!r.deleted);
        if (r.index == 5) {
            sawNew = true;
            CHECK(r.change == "new");
            CHECK(r.colorIndex == 3);
        }
        if (r.index == 6) {
            sawOld = true;
            CHECK(r.change == "");
            CHECK(!r.changed);
            CHECK(r.colorIndex == 2);
        }
    }
    CHECK(sawNew);
    CHECK(sawOld);
    for (const auto& s : previewSegments(session->base(), session->preview())) {
        const auto* n = nodeAt(session->preview(), s.path);
        if (!n || !n->partition) continue;
        if (n->partition->index == 5) CHECK(s.colorIndex == 3);
        if (n->partition->index == 6) CHECK(s.colorIndex == 2);
    }

    auto applied = session->apply(progress);
    REQUIRE(applied);
    CHECK(applied->postconditionOk);
    const auto& written = *session->base().table;
    REQUIRE(written.find(5));
    REQUIRE(written.find(6));
    CHECK(written.find(5)->firstLba == extFirst + gap);
    CHECK(written.find(5)->ebrLba == extFirst);
    CHECK(written.find(6)->firstLba == tailStart);
    CHECK(written.find(6)->ebrLba == tailStart - gap);
    CHECK(written.metadataRegions().size() == 3);
    for (const auto& d : written.diagnostics()) CHECK(d.severity < layout::Validity::Error);
}

TEST_CASE("pending titles are the library's, whatever table the preview ends with") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("gpt.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto session = EditSession::open(*desc, OpenOptions{});
    REQUIRE(session);
    AddPartitionForm add;
    add.typeText = "8300";
    add.index = 5;
    REQUIRE(session->addPartition(add));
    REQUIRE(session->createTable(pt::TableType::Mbr));   // the preview is MBR now, the add was GPT
    auto pending = pendingRows(session->stack());
    REQUIRE(pending.size() == 2);
    CHECK(pending[0].title.starts_with("Add partition 5: Linux filesystem"));
    CHECK(pending[1].title == "Create MBR partition table");
}

TEST_CASE("the table re-read note says only what happened") {
    const Expected<void> ok{};
    const Expected<void> busy = fail(ErrorCategory::Busy, "BLKRRPART failed on /dev/sdb: Device or resource busy");
    auto asked = tableRereadNote("linux", ok);   // not named `linux`: GCC predefines that macro under gnu++
    CHECK(asked.happened);
    CHECK(asked.text == "asked the OS");
    // macOS has no re-read call: the kernel does it when the last writer closes the disk, which the
    // editor has not done yet. The note must not claim a re-read.
    auto mac = tableRereadNote("macos", ok);
    CHECK(!mac.happened);
    CHECK(mac.text.find("asked") == std::string::npos);
    CHECK(mac.text.find("closes the disk") != std::string::npos);
    auto refused = tableRereadNote("linux", busy);
    CHECK(!refused.happened);
    CHECK(refused.text.find("did not re-read") != std::string::npos);
    CHECK(refused.text.find("Device or resource busy") != std::string::npos);
    CHECK(refused.text.find("reconnect") != std::string::npos);
}
