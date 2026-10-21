// SPDX-License-Identifier: MIT
#include "drstein/core/browse.hpp"
#include "fixtures.hpp"

#include <doctest.h>

#include <fstream>
#include <map>
#include <set>

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

namespace {

// What the host that built the exFAT fixture saw: "f <size> <path>", "d <size> <path>" and
// "sha256 <hex> <path>" lines, paths relative to the root.
struct ExfatOracle {
    std::map<std::string, std::uint64_t> files;
    std::set<std::string> dirs;
    std::map<std::string, std::string> sha;
};

ExfatOracle exfatOracle() {
    ExfatOracle o;
    std::ifstream in(std::string(STEIN_FIXTURE_DIR) + "/exfatfs/exfat.oracle.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("sha256 ")) {
            const auto sp = line.find(' ', 7);
            o.sha[line.substr(sp + 1)] = line.substr(7, sp - 7);
        } else if (line.size() > 4 && (line[0] == 'f' || line[0] == 'd') && line[1] == ' ') {
            const auto sp = line.find(' ', 2);
            if (line[0] == 'd') o.dirs.insert(line.substr(sp + 1));
            else o.files[line.substr(sp + 1)] = std::stoull(line.substr(2, sp - 2));
        }
    }
    return o;
}

NodePath partitionPath(const probe::Node& tree, std::uint32_t index) {
    for (std::size_t i = 0; i < tree.children.size(); ++i)
        if (tree.children[i].partition && tree.children[i].partition->index == index) return {static_cast<int>(i)};
    return {};
}

} // namespace

TEST_CASE("exFAT in the MBR composite's second primary lists what the oracle lists") {
    auto disk = drstein::test::mbrComposite();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    const NodePath exfatPath = partitionPath(*tree, 2);
    REQUIRE(!exfatPath.empty());
    auto browser = Browser::open(*tree, exfatPath);
    REQUIRE(browser);
    CHECK(browser->fsName() == "exFAT");
    CHECK(!browser->caseSensitive());
    const auto oracle = exfatOracle();
    REQUIRE(!oracle.files.empty());

    // The root: every listed entry is in the oracle with its size, every top-level oracle path is listed.
    auto listing = browser->listCurrent();
    REQUIRE(listing);
    std::set<std::string> listed;
    for (const auto& e : listing->entries) {
        listed.insert(e.entry.name);
        if (e.isDir()) {
            CHECK_MESSAGE(oracle.dirs.count(e.entry.name), e.entry.name);
        } else {
            auto it = oracle.files.find(e.entry.name);
            REQUIRE_MESSAGE(it != oracle.files.end(), e.entry.name);
            CHECK(it->second == e.stat.size);
        }
    }
    for (const auto& [path, size] : oracle.files)
        if (path.find('/') == std::string::npos) CHECK_MESSAGE(listed.count(path), path);
    for (const auto& dir : oracle.dirs)
        if (dir.find('/') == std::string::npos) CHECK_MESSAGE(listed.count(dir), dir);

    // One file's bytes hash to what the host recorded.
    const Entry* hello = nullptr;
    for (const auto& e : listing->entries)
        if (e.entry.name == "hello.txt") hello = &e;
    REQUIRE(hello);
    auto whole = fs::readAll(browser->reader(), hello->entry.inode);
    REQUIRE(whole);
    CHECK(whole->size() == hello->stat.size);
    CHECK(Hasher::hex(Hasher::digest(HashAlgorithm::Sha256, *whole)) == oracle.sha.at("hello.txt"));

    // Nested directories descend; the leaf matches too.
    REQUIRE(browser->goTo("/dir/nested/deep"));
    listing = browser->listCurrent();
    REQUIRE(listing);
    bool leaf = false;
    for (const auto& e : listing->entries)
        if (e.entry.name == "leaf.txt") {
            leaf = true;
            auto bytes = fs::readAll(browser->reader(), e.entry.inode);
            REQUIRE(bytes);
            CHECK(Hasher::hex(Hasher::digest(HashAlgorithm::Sha256, *bytes)) == oracle.sha.at("dir/nested/deep/leaf.txt"));
        }
    CHECK(leaf);

    // The logical inside the extended container is browsable too; the container itself is not.
    auto ext4 = Browser::open(*tree, partitionPath(*tree, 5));
    REQUIRE(ext4);
    CHECK(ext4->fsName() == "ext4");
    CHECK(!Browser::open(*tree, partitionPath(*tree, 3)));
}
