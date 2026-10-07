// SPDX-License-Identifier: MIT
#include "drstein/core/browse.hpp"
#include "fixtures.hpp"

#include <doctest.h>

using namespace drstein::core;
using namespace stein;

TEST_CASE("the viewer lists, descends, previews, hashes and copies out") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    CHECK(!Browser::open(*tree, {99}));
    auto browser = Browser::open(*tree, {0});
    REQUIRE(browser);
    CHECK(browser->fsName() == "ext4");
    CHECK(browser->pathText() == "/");
    CHECK(browser->breadcrumbs().size() == 1);

    auto listing = browser->listCurrent();
    REQUIRE(listing);
    REQUIRE(!listing->entries.empty());
    // Directories come first, sorted case-insensitively.
    bool seenFile = false;
    std::string prev;
    for (const auto& e : listing->entries) {
        if (e.isDir()) CHECK(!seenFile);
        else seenFile = true;
        CHECK(!e.modeText.empty());
        CHECK(e.modeText.size() == 10);
    }

    const Entry* dir = nullptr;
    const Entry* file = nullptr;
    for (const auto& e : listing->entries) {
        if (!dir && e.isDir()) dir = &e;
        if (!file && e.stat.type == fs::FileType::File && e.stat.size > 0) file = &e;
    }
    REQUIRE(dir);
    REQUIRE(file);
    CHECK(file->sizeText != "—");
    CHECK(dir->sizeText == "—");

    auto pv = preview(browser->reader(), file->entry.inode, file->stat);
    REQUIRE(pv);
    CHECK(pv->kind != PreviewKind::Unreadable);
    CHECK(pv->bytes.size() == std::min<std::uint64_t>(file->stat.size, 256 * KiB));

    NullProgressSink sink;
    Progress progress(sink);
    auto hash = sha256Hex(browser->reader(), file->entry.inode, file->stat, progress);
    REQUIRE(hash);
    CHECK(hash->size() == 64);
    auto whole = fs::readAll(browser->reader(), file->entry.inode);
    REQUIRE(whole);
    CHECK(*hash == Hasher::hex(Hasher::digest(HashAlgorithm::Sha256, *whole)));

    drstein::test::TempDir tmp;
    auto stats = copyOut(browser->reader(), file->entry.inode, file->entry.name, tmp.path(), progress);
    REQUIRE(stats);
    CHECK(stats->files == 1);
    CHECK(std::filesystem::file_size(tmp.file(file->entry.name)) == file->stat.size);
    CHECK(copyStatsText(*stats).starts_with("1 file"));
    auto treeStats = copyOut(browser->reader(), dir->entry.inode, dir->entry.name, tmp.path(), progress);
    REQUIRE(treeStats);
    CHECK(std::filesystem::is_directory(tmp.file(dir->entry.name)));

    REQUIRE(browser->enter(dir->entry.name));
    CHECK(browser->pathText() == "/" + dir->entry.name);
    CHECK(browser->breadcrumbs().size() == 2);
    CHECK(!browser->enter(file->entry.name));
    browser->up();
    CHECK(browser->pathText() == "/");
    REQUIRE(browser->goTo("/" + dir->entry.name + "/.."));
    CHECK(browser->pathText() == "/");
    REQUIRE(browser->goTo(dir->entry.name));
    browser->jumpTo(0);
    CHECK(browser->pathText() == "/");
    CHECK(!browser->goTo("/does/not/exist"));
}

TEST_CASE("FAT is browsable too and previews classify content") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    // Children are partitions and free gaps in disk order; find partition 2.
    NodePath fatPath;
    for (std::size_t i = 0; i < tree->children.size(); ++i)
        if (tree->children[i].partition && tree->children[i].partition->index == 2) fatPath = {static_cast<int>(i)};
    REQUIRE(!fatPath.empty());
    auto browser = Browser::open(*tree, fatPath);
    REQUIRE(browser);
    CHECK(browser->fsName() == "FAT32");
    CHECK(!browser->caseSensitive());
    auto listing = browser->listCurrent();
    REQUIRE(listing);
    for (const auto& e : listing->entries) {
        if (e.stat.type != fs::FileType::File) continue;
        auto pv = preview(browser->reader(), e.entry.inode, e.stat, 64);
        REQUIRE(pv);
        if (e.stat.size == 0) CHECK(pv->kind == PreviewKind::Empty);
        else {
            CHECK(pv->bytes.size() <= 64);
            CHECK(pv->truncated == (e.stat.size > 64));
        }
    }
}
