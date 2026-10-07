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
}
