// SPDX-License-Identifier: MIT
#include "drstein/core/profiles.hpp"
#include "fixtures.hpp"

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

TEST_CASE("profiles are stored, listed and rendered as cards") {
    auto disk = drstein::test::compositeDisk();
    drstein::test::TempDir tmp;
    const auto file = drstein::test::writeDevice(*disk.device, tmp.file("disk.img"));
    ProfileStore store(tmp.file("profiles"));
    CHECK(store.list().value().empty());

    auto profile = profileFromFile(file, tmp.file("backup.stein"), "Workstation");
    profile.description = "Nightly";
    profile.image.chunkSize = 1 * MiB;
    auto saved = store.save(profile);
    REQUIRE(saved);
    CHECK(saved->file.filename() == "Workstation.json");
    auto again = store.save(profile);
    REQUIRE(again);
    CHECK(again->file.filename() == "Workstation-2.json");
    REQUIRE(store.remove(again->file));
    auto listed = store.list();
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().profile.name == "Workstation");

    auto card = profileCard(listed->front());
    CHECK(card.kicker == "Nightly");
    CHECK(card.diskPresent);
    CHECK(card.diskText.find("present") != std::string::npos);
    CHECK(!card.imagePresent);
    CHECK(card.imageText == "backup.stein · not created yet");
    CHECK(card.canBackup);
    CHECK(!card.canRestore);
    CHECK(!card.canVerify);
    CHECK(card.fitsText == "— (no image)");
    CHECK(card.policyText.find("lock target") != std::string::npos);
    CHECK(card.selectorText.find("path") != std::string::npos);

    // Back up, then the card changes: image present, restore possible.
    NullProgressSink sink;
    Progress progress(sink);
    app::RunOptions o;
    o.unlock = true;   // a path-only target
    auto result = runScenario(Scenario::Backup, listed->front().profile, o, progress);
    REQUIRE(result);
    CHECK(result->ok);
    CHECK(result->created.has_value());
    auto card2 = profileCard(listed->front());
    CHECK(card2.imagePresent);
    CHECK(card2.imageComplete);
    CHECK(card2.canRestore);
    CHECK(card2.canVerify);
    CHECK(card2.fitsText.starts_with("yes"));

    auto verified = runScenario(Scenario::Verify, listed->front().profile, o, progress);
    REQUIRE(verified);
    CHECK(verified->ok);
    CHECK(toString(Scenario::Restore) == "restore");
}

TEST_CASE("a profile from disk info carries the identity") {
    platform::DiskInfo d;
    d.osPath = "/dev/sdb";
    d.model = "SanDisk Ultra";
    d.serial = "4C530001230812117583";
    d.geometry.sizeBytes = 61'530'439'680ull;
    d.removable = true;
    auto p = profileFromDisk(d, "/mnt/backup/stick.stein", "Stick");
    CHECK(p.target.serial == d.serial);
    CHECK(p.target.model == d.model);
    CHECK(p.target.sizeTolerance == doctest::Approx(0.02));
    CHECK(p.target.allowRemovable);
    CHECK(p.policy.requireElevated);
    CHECK(p.validate());
}
