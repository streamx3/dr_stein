// SPDX-License-Identifier: MIT
#include "drstein/core/imaging.hpp"
#include "drstein/core/progress.hpp"
#include "fixtures.hpp"
#include "stein/block/file_device.hpp"
#include "stein/block/slice_device.hpp"

#include <algorithm>

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

TEST_CASE("sizes parse with units") {
    CHECK(parseSize("4 MiB").value() == 4 * MiB);
    CHECK(parseSize("1M").value() == MiB);
    CHECK(parseSize("2G").value() == 2 * GiB);
    CHECK(parseSize("512k").value() == 512 * KiB);
    CHECK(parseSize("1.5 GiB").value() == GiB + GiB / 2);
    CHECK(parseSize("4096").value() == 4096);
    CHECK(!parseSize(""));
    CHECK(!parseSize("lots"));
    CHECK(!parseSize("4 parsecs"));
    CHECK(looksRaw("x.img"));
    CHECK(looksRaw("X.DD"));
    CHECK(!looksRaw("x.stein"));
}

TEST_CASE("the create form validates into options") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto opened = openSource(*desc, OpenOptions{});
    REQUIRE(opened);

    CreateImageForm form;
    CHECK(!validate(form, *opened));   // no destination
    form.destination = tmp.file("nope/out.stein");
    CHECK(!validate(form, *opened));   // folder missing
    form.destination = tmp.file("out.stein");
    auto plan = validate(form, *opened);
    REQUIRE(plan);
    CHECK(!plan->raw);
    CHECK(plan->verifyAfter);
    CHECK(plan->options.chunkSize == 4 * MiB);
    CHECK(plan->options.usedBlocksOnly);
    CHECK(plan->options.sourceName == desc->title);
    CHECK(plan->warnings.empty());

    form.chunkSizeText = "1 KiB";
    CHECK(!validate(form, *opened));
    form.chunkSizeText = "1 MiB";
    form.splitSizeText = "512 KiB";
    CHECK(!validate(form, *opened));   // smaller than a chunk
    form.splitSizeText = "64 MiB";
    form.encrypt = true;
    CHECK(!validate(form, *opened));   // no passphrase
    form.passphrase = "secret";
    plan = validate(form, *opened);
    REQUIRE(plan);
    CHECK(plan->options.passphrase == "secret");
    CHECK(plan->options.splitSize == 64 * MiB);

    form.destination = tmp.file("out.img");
    plan = validate(form, *opened);
    REQUIRE(plan);
    CHECK(plan->raw);
    CHECK(!plan->verifyAfter);
    CHECK(plan->warnings.size() == 2);   // verify n/a, options n/a

    const std::string note = usedBlocksNote(opened->tree);
    CHECK(note.find("ext4") != std::string::npos);
    CHECK(note.find("FAT32") != std::string::npos);
}

TEST_CASE("create, verify and restore a .stein image") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto desc = describeImageFile(file);
    REQUIRE(desc);
    auto opened = openSource(*desc, OpenOptions{});
    REQUIRE(opened);

    CreateImageForm form;
    form.destination = tmp.file("backup.stein");
    form.chunkSizeText = "1 MiB";
    form.notes = "test";
    auto plan = validate(form, *opened);
    REQUIRE(plan);

    ProgressRelay relay;
    int updates = 0;
    relay.setHandlers([&](const ProgressSnapshot&) { ++updates; }, [](std::string) {});
    Progress progress(relay, &relay.cancelToken());
    Report report("Create");
    auto out = runCreate(*opened, *plan, progress, report);
    REQUIRE(out);
    CHECK(out->verified);
    CHECK(out->verify.complete);
    CHECK(out->verify.chunksBad == 0);
    CHECK(out->result.stats.freeBytesSkipped > 0);   // used-blocks-only did something
    CHECK(report.status() == ReportStatus::Success);
    REQUIRE(report.children().size() == 2);
    CHECK(report.children()[0]->title() == "Create image");
    CHECK(report.children()[1]->title() == "Verify level 3");
    CHECK(updates > 0);

    auto desc2 = describeImageFile(tmp.file("backup.stein"));
    REQUIRE(desc2);
    REQUIRE(desc2->image);
    CHECK(desc2->image->complete);
    CHECK(desc2->image->compressed);
    CHECK(desc2->image->virtualSize == disk.device->size());
    CHECK(desc2->subtitle.find("LZ4") != std::string::npos);
    CHECK(desc2->image->sourceName == desc->title);

    VerifyForm vf;
    vf.image = tmp.file("backup.stein");
    vf.level = 2;
    Report vreport("Verify");
    auto v = runVerify(vf, progress, vreport);
    REQUIRE(v);
    CHECK(v->structureOk);
    CHECK(verifyText(*v).find("complete") != std::string::npos);

    // Restore into a fresh raw file and compare the partition table region.
    auto target = FileDevice::create(tmp.file("restored.img"), disk.device->size());
    REQUIRE(target);
    RestoreForm rf;
    rf.image = tmp.file("backup.stein");
    Report rreport("Restore");
    auto r = runRestore(rf, **target, progress, rreport);
    REQUIRE(r);
    CHECK(rreport.status() == ReportStatus::Success);
    std::vector<std::byte> a(1 * MiB), b(1 * MiB);
    REQUIRE((*target)->readAt(0, a));
    REQUIRE(disk.device->readAt(0, b));
    CHECK(a == b);
    REQUIRE((*target)->readAt(disk.part2Offset, a));
    REQUIRE(disk.device->readAt(disk.part2Offset, b));
    CHECK(a == b);

    // An image opened as a source probes like the disk.
    auto reopened = openSource(*desc2, OpenOptions{});
    REQUIRE(reopened);
    REQUIRE(reopened->tree.table);
    CHECK(reopened->tree.table->partitions().size() == 2);
}

TEST_CASE("a raw image is a plain copy") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto opened = openSource(describeImageFile(file).value(), OpenOptions{});
    REQUIRE(opened);
    CreateImageForm form;
    form.destination = tmp.file("copy.img");
    auto plan = validate(form, *opened);
    REQUIRE(plan);
    NullProgressSink sink;
    Progress progress(sink);
    Report report("Create");
    auto out = runCreate(*opened, *plan, progress, report);
    REQUIRE(out);
    CHECK(!out->verified);
    CHECK(out->rawStats.bytesRead == disk.device->size());
    CHECK(std::filesystem::file_size(tmp.file("copy.img")) == disk.device->size());
}

TEST_CASE("cancellation stops a job") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto opened = openSource(describeImageFile(file).value(), OpenOptions{});
    REQUIRE(opened);
    CreateImageForm form;
    form.destination = tmp.file("cancelled.stein");
    form.chunkSizeText = "64 KiB";
    form.verifyAfter = false;
    auto plan = validate(form, *opened);
    REQUIRE(plan);
    ProgressRelay relay;
    relay.setHandlers([&](const ProgressSnapshot&) { relay.cancel(); }, [](std::string) {});
    Progress progress(relay, &relay.cancelToken());
    Report report("Create");
    auto out = runCreate(*opened, *plan, progress, report);
    CHECK(!out);
    if (!out) CHECK(out.error().category() == ErrorCategory::Cancelled);
    relay.reset();
    CHECK(!relay.isCancelled());
}

TEST_CASE("a partition image carries its provenance and restores into a partition") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    auto opened = openSource(describeImageFile(file).value(), OpenOptions{});
    REQUIRE(opened);

    auto scopes = sourceScopes(*opened);
    REQUIRE(scopes.size() == 3);   // device + 2 partitions (free gaps are not scopes)
    CHECK(!scopes[0].partition);
    CHECK(scopes[0].label.starts_with("Whole device"));
    CHECK(scopes[2].partition);
    CHECK(scopes[2].label.starts_with("Partition 2"));
    CHECK(scopes[2].label.find("Data") != std::string::npos);
    CHECK(scopes[2].label.find("FAT32") != std::string::npos);

    CreateImageForm form;
    form.sourcePath = scopes[2].path;
    form.destination = tmp.file("data-p2.stein");
    form.chunkSizeText = "1 MiB";
    auto plan = validate(form, *opened);
    REQUIRE(plan);
    CHECK(plan->partition);
    CHECK(plan->device->size() == disk.part2Size);
    CHECK(plan->scopeText.starts_with("partition 2"));
    CHECK(plan->options.sourceExtra.get("kind").asString() == "partition");
    CHECK(plan->options.sourceExtra.get("partition").get("index").asUInt() == 2);
    CHECK(plan->options.sourceName.find("partition 2") != std::string::npos);

    NullProgressSink sink;
    Progress progress(sink);
    Report report("Create");
    auto out = runCreate(*opened, *plan, progress, report);
    REQUIRE(out);
    CHECK(out->result.stats.bytesRead == disk.part2Size);

    auto desc = describeImageFile(form.destination);
    REQUIRE(desc);
    REQUIRE(desc->image);
    CHECK(desc->image->partitionImage);
    CHECK(desc->image->virtualSize == disk.part2Size);
    CHECK(desc->image->provenanceText.starts_with("partition 2 \"Data\" of disk.img"));
    CHECK(desc->image->provenanceText.find("LBA") != std::string::npos);
    CHECK(desc->subtitle.find("partition") != std::string::npos);
    CHECK(desc->identityLine.find("partition image") != std::string::npos);

    auto scope = restoreScopeOf(form.destination);
    REQUIRE(scope);
    CHECK(scope->partition);
    REQUIRE(scope->provenance);
    CHECK(scope->provenance->firstLba == disk.part2Offset / 512);
    CHECK(scope->provenance->parentTable == "GPT");
    CHECK(scope->size == disk.part2Size);
    auto whole = restoreScopeOf(file);
    REQUIRE(whole);
    CHECK(!whole->partition);

    // The partition image opens as a bare filesystem, and restores into a slice of another disk.
    auto reopened = openSource(*desc, OpenOptions{});
    REQUIRE(reopened);
    CHECK(reopened->tree.content);
    auto target = std::make_shared<MemoryDevice>(disk.device->size(), 512);
    auto slice = SliceDevice::create(target, Region{disk.part2Offset, disk.part2Size}, "p2");
    REQUIRE(slice);
    RestoreForm rf;
    rf.image = form.destination;
    Report rreport("Restore");
    REQUIRE(runRestore(rf, **slice, progress, rreport));
    std::vector<std::byte> a(64 * KiB), b(64 * KiB);
    REQUIRE(target->readAt(disk.part2Offset, a));
    REQUIRE(disk.device->readAt(disk.part2Offset, b));
    CHECK(a == b);
    REQUIRE(target->readAt(0, a));
    CHECK(std::all_of(a.begin(), a.end(), [](std::byte x) { return x == std::byte{0}; }));   // nothing outside the slice
}
