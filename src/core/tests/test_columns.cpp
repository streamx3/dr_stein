// SPDX-License-Identifier: MIT
#include "drstein/core/columns.hpp"
#include "fixtures.hpp"

#include <doctest.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace drstein::core;
using namespace stein;

namespace {

Crumb crumb(std::string name, std::uint64_t id) { return {std::move(name), fs::Inode{id}}; }

Entry entry(std::string name, std::uint64_t id, fs::FileType type) {
    Entry e;
    e.entry.name = std::move(name);
    e.entry.inode = fs::Inode{id};
    e.entry.type = type;
    e.stat.type = type;
    return e;
}

Listing listing(std::uint64_t dir, std::initializer_list<std::string> names) {
    Listing l;
    l.dir = fs::Inode{dir};
    std::uint64_t id = dir * 100;
    for (const auto& n : names) l.entries.push_back(entry(n, ++id, fs::FileType::File));
    return l;
}

// A filesystem in memory that counts directory reads and fails the ones it is told to.
class FakeReader final : public fs::Reader {
public:
    struct Node {
        std::string name;
        fs::FileType type = fs::FileType::File;
        std::vector<std::uint64_t> children;
    };

    std::map<std::uint64_t, Node> nodes;
    std::set<std::uint64_t> failing;
    std::map<std::uint64_t, int> reads;

    FakeReader() { nodes[1] = {"/", fs::FileType::Directory, {}}; }

    void add(std::uint64_t parent, std::uint64_t id, std::string name, fs::FileType type) {
        nodes[id] = {std::move(name), type, {}};
        nodes[parent].children.push_back(id);
    }

    Expected<fs::Inode> root() const override { return fs::Inode{1}; }
    Expected<fs::Inode> lookup(const fs::Inode& dir, std::string_view name) override {
        for (auto id : nodes.at(dir.id).children)
            if (nodes.at(id).name == name) return fs::Inode{id};
        return fail(ErrorCategory::NotFound, std::string(name) + " not found");
    }
    Expected<fs::Stat> stat(const fs::Inode& inode) override {
        const auto it = nodes.find(inode.id);
        if (it == nodes.end()) return fail(ErrorCategory::NotFound, "no such inode");
        fs::Stat st;
        st.type = it->second.type;
        st.mode = 0755;
        return st;
    }
    Expected<std::vector<fs::DirEntry>> readdir(const fs::Inode& dir) override {
        ++reads[dir.id];
        if (failing.count(dir.id)) return fail(ErrorCategory::Io, "unreadable directory");
        std::vector<fs::DirEntry> out;
        for (auto id : nodes.at(dir.id).children) out.push_back({nodes.at(id).name, fs::Inode{id}, nodes.at(id).type});
        return out;
    }
    Expected<std::size_t> read(const fs::Inode&, std::uint64_t, std::span<std::byte>) override { return fail(ErrorCategory::Unsupported, "no file data"); }
    Expected<std::string> readlink(const fs::Inode&) override { return fail(ErrorCategory::InvalidArgument, "no symbolic links"); }
};

// The root holds the directories 2..5 ("A" "B" "C" "D", each with one file) and a file, 6.
struct FakeBrowser {
    FakeReader* reader = nullptr;   // owned by the browser
    Browser browser;
};

FakeBrowser fakeBrowser() {
    auto reader = std::make_unique<FakeReader>();
    FakeReader* raw = reader.get();
    for (std::uint64_t id = 2; id <= 5; ++id) {
        raw->add(1, id, std::string(1, static_cast<char>('A' + id - 2)), fs::FileType::Directory);
        raw->add(id, id * 10, "inside.txt", fs::FileType::File);
    }
    raw->add(1, 6, "file.txt", fs::FileType::File);
    auto browser = Browser::fromReader(std::move(reader), "fake", "fake");
    REQUIRE(browser);
    return {raw, std::move(*browser)};
}

} // namespace

TEST_CASE("columns: the root alone is one current column") {
    CHECK(planColumns({}, nullptr).empty());

    const std::vector<Crumb> root{crumb("/", 2)};
    auto plans = planColumns(root, nullptr);
    REQUIRE(plans.size() == 1);
    CHECK(plans[0].dir == fs::Inode{2});
    CHECK(plans[0].title == "/");
    CHECK(plans[0].crumb == 0);
    CHECK(plans[0].current);
    CHECK(!plans[0].peek);
    CHECK(plans[0].selectName.empty());

    // A file selected adds nothing; a folder adds the peek at the next crumb's place.
    const Entry file = entry("notes.txt", 12, fs::FileType::File);
    CHECK(planColumns(root, &file).size() == 1);
    const Entry folder = entry("Projects", 11, fs::FileType::Directory);
    plans = planColumns(root, &folder);
    REQUIRE(plans.size() == 2);
    CHECK(plans[0].current);
    CHECK(plans[0].selectName.empty());
    CHECK(plans[1].peek);
    CHECK(!plans[1].current);
    CHECK(plans[1].dir == fs::Inode{11});
    CHECK(plans[1].title == "Projects");
    CHECK(plans[1].crumb == 1);
    CHECK(plans[1].selectName.empty());
}

TEST_CASE("columns: a deep path, unicode names, and what a selection adds") {
    const std::vector<Crumb> crumbs{
        crumb("/", 2), crumb("Проєкти", 30), crumb("設計ノート", 31), crumb("🧪 lab", 32), crumb("v7", 33),
    };
    auto plans = planColumns(crumbs, nullptr);
    REQUIRE(plans.size() == 5);
    for (std::size_t k = 0; k < plans.size(); ++k) {
        CAPTURE(k);
        CHECK(plans[k].dir == crumbs[k].inode);
        CHECK(plans[k].title == crumbs[k].name);
        CHECK(plans[k].crumb == k);
        CHECK(!plans[k].peek);
        CHECK(plans[k].current == (k == 4));
        CHECK(plans[k].selectName == (k < 4 ? crumbs[k + 1].name : std::string()));
    }
    CHECK(plans[0].selectName == "Проєкти");
    CHECK(plans[2].selectName == "🧪 lab");

    const Entry file = entry("the deepest file in this whole tree.txt", 40, fs::FileType::File);
    CHECK(planColumns(crumbs, &file) == plans);
    // A symbolic link is not followed, even to a folder.
    const Entry link = entry("shortcut", 41, fs::FileType::Symlink);
    CHECK(planColumns(crumbs, &link) == plans);

    const Entry folder = entry("final — really final", 42, fs::FileType::Directory);
    const auto withPeek = planColumns(crumbs, &folder);
    REQUIRE(withPeek.size() == 6);
    CHECK(std::equal(plans.begin(), plans.end(), withPeek.begin()));
    CHECK(withPeek[5].peek);
    CHECK(withPeek[5].crumb == 5);
    CHECK(withPeek[5].title == "final — really final");
    CHECK(withPeek[5].dir == fs::Inode{42});
    CHECK(withPeek[4].current);
    CHECK(withPeek[4].selectName.empty());   // the current column's selection is the list's own
}

TEST_CASE("columns: which columns survive a step") {
    const Entry a = entry("A", 20, fs::FileType::Directory);
    const Entry b = entry("B", 21, fs::FileType::Directory);
    const Entry f = entry("f.txt", 22, fs::FileType::File);
    const Entry inner = entry("Inner", 30, fs::FileType::Directory);
    const std::vector<Crumb> atRoot{crumb("/", 2)};
    const std::vector<Crumb> inA{crumb("/", 2), crumb("A", 20)};

    const auto rootA = planColumns(atRoot, &a);      // [/ current] [A peek]
    const auto rootB = planColumns(atRoot, &b);
    const auto rootF = planColumns(atRoot, &f);
    const auto inANone = planColumns(inA, nullptr);  // [/ > A] [A current]
    const auto inAInner = planColumns(inA, &inner);  // ... [Inner peek]

    CHECK(keptColumns({}, rootA) == 0);
    CHECK(keptColumns(rootA, {}) == 0);
    CHECK(keptColumns(rootA, rootA) == 2);
    // Another folder selected: the peek goes; a file selected: the peek goes and none comes.
    CHECK(keptColumns(rootA, rootB) == 1);
    CHECK(keptColumns(rootA, rootF) == 1);
    CHECK(keptColumns(rootF, rootA) == 1);
    // Entering the peeked folder keeps both columns: the peek becomes the current one.
    CHECK(keptColumns(rootA, inANone) == 2);
    CHECK(keptColumns(rootA, inAInner) == 2);
    CHECK(rootA[1] != inANone[1]);
    // Up from A keeps the root and turns A back into the peek.
    CHECK(keptColumns(inAInner, rootA) == 2);
    // Up from deeper changes nothing to the left.
    const std::vector<Crumb> inInner{crumb("/", 2), crumb("A", 20), crumb("Inner", 30)};
    CHECK(keptColumns(planColumns(inInner, nullptr), inAInner) == 3);
    // A jump to the root from deep keeps the root and the folder that was left, peeked again.
    CHECK(keptColumns(planColumns(inInner, nullptr), rootA) == 2);
    // Only leading columns count: a different second column ends the run even if a later one matches.
    const std::vector<Crumb> other{crumb("/", 2), crumb("B", 21), crumb("Inner", 30)};
    CHECK(keptColumns(planColumns(inInner, nullptr), planColumns(other, nullptr)) == 1);
    // The directory decides, not the name: another "x" in the same place is another column, and a
    // path typed in another case (exFAT) names the same one.
    const std::vector<Crumb> x30{crumb("/", 2), crumb("a", 20), crumb("x", 30)};
    const std::vector<Crumb> x31{crumb("/", 2), crumb("a", 20), crumb("x", 31)};
    CHECK(keptColumns(planColumns(x30, nullptr), planColumns(x31, nullptr)) == 2);
    const std::vector<Crumb> typed{crumb("/", 2), crumb("A", 20), crumb("X", 30)};
    CHECK(keptColumns(planColumns(x30, nullptr), planColumns(typed, nullptr)) == 3);
}

TEST_CASE("columns: the peek follows the entry's stat, not the type its directory entry reports") {
    // ext4 without the filetype feature, among others, lists every entry as Unknown.
    const std::vector<Crumb> root{crumb("/", 2)};
    Entry folder = entry("Projects", 11, fs::FileType::Directory);
    folder.entry.type = fs::FileType::Unknown;
    const auto plans = planColumns(root, &folder);
    REQUIRE(plans.size() == 2);
    CHECK(plans[1].peek);
    CHECK(plans[1].dir == fs::Inode{11});

    Entry notFolder = entry("looks like a folder", 12, fs::FileType::File);
    notFolder.entry.type = fs::FileType::Directory;
    CHECK(planColumns(root, &notFolder).size() == 1);
}

TEST_CASE("columns: rowOfName finds the crumb's row, exactly first, case folded where the filesystem folds") {
    CHECK(rowOfName(Listing{}, "x", true) == -1);
    CHECK(rowOfName(Listing{}, "x", false) == -1);

    const auto l = listing(1, {"Docs", "docs", "Notes", "Документи", "日本語", "Ärger", "README"});
    CHECK(rowOfName(l, "Docs", true) == 0);
    CHECK(rowOfName(l, "docs", true) == 1);
    CHECK(rowOfName(l, "DOCS", true) == -1);
    CHECK(rowOfName(l, "notes", true) == -1);
    CHECK(rowOfName(l, "Notes", true) == 2);
    CHECK(rowOfName(l, "Документи", true) == 3);
    CHECK(rowOfName(l, "日本語", true) == 4);
    CHECK(rowOfName(l, "missing", true) == -1);
    CHECK(rowOfName(l, "", true) == -1);
    CHECK(rowOfName(l, "Doc", true) == -1);

    // An exact match wins over an earlier folded one.
    CHECK(rowOfName(l, "docs", false) == 1);
    CHECK(rowOfName(l, "DOCS", false) == 0);
    CHECK(rowOfName(l, "nOtEs", false) == 2);
    CHECK(rowOfName(l, "readme", false) == 6);
    CHECK(rowOfName(l, "Документи", false) == 3);
    CHECK(rowOfName(l, "日本語", false) == 4);
    // Only ASCII letters fold: the bytes of other letters are compared as they are.
    CHECK(rowOfName(l, "документи", false) == -1);
    CHECK(rowOfName(l, "ärger", false) == -1);
    CHECK(rowOfName(l, "ÄRGER", false) == 5);   // the Ä is exact, the rest folds
    CHECK(rowOfName(l, "missing", false) == -1);
}

TEST_CASE("columns: what a click means in each kind of column") {
    const std::vector<Crumb> crumbs{crumb("/", 2), crumb("a", 3), crumb("b", 4)};
    const Entry folder = entry("c", 5, fs::FileType::Directory);
    const auto plans = planColumns(crumbs, &folder);
    REQUIRE(plans.size() == 4);
    CHECK(classifyClick(0, plans) == ColumnClick::JumpThenSelect);
    CHECK(classifyClick(1, plans) == ColumnClick::JumpThenSelect);
    CHECK(classifyClick(2, plans) == ColumnClick::SelectInCurrent);
    CHECK(classifyClick(3, plans) == ColumnClick::EnterThenSelect);
    CHECK(classifyClick(4, plans) == ColumnClick::Ignore);
    CHECK(classifyClick(static_cast<std::size_t>(-1), plans) == ColumnClick::Ignore);
    CHECK(classifyClick(0, {}) == ColumnClick::Ignore);

    // Without a peek the current column is the last; the root alone is current.
    const auto noPeek = planColumns(crumbs, nullptr);
    CHECK(classifyClick(2, noPeek) == ColumnClick::SelectInCurrent);
    CHECK(classifyClick(3, noPeek) == ColumnClick::Ignore);
    CHECK(classifyClick(0, planColumns({crumb("/", 2)}, nullptr)) == ColumnClick::SelectInCurrent);
}

TEST_CASE("columns: the listing cache reads a directory once and forgets the least recently used") {
    auto fake = fakeBrowser();
    auto& reads = fake.reader->reads;
    ListingCache cache(3);
    CHECK(cache.capacity() == 3);
    CHECK(cache.size() == 0);
    CHECK(!cache.find(fs::Inode{2}));

    auto first = cache.get(fake.browser, fs::Inode{2});
    REQUIRE(first);
    REQUIRE(*first);
    CHECK((*first)->dir == fs::Inode{2});
    REQUIRE((*first)->entries.size() == 1);
    CHECK((*first)->entries[0].entry.name == "inside.txt");
    CHECK(reads[2] == 1);
    auto again = cache.get(fake.browser, fs::Inode{2});
    REQUIRE(again);
    CHECK(again->get() == first->get());
    CHECK(reads[2] == 1);
    CHECK(cache.find(fs::Inode{2}) == *first);

    // The root lists as Browser::list does: folders first, then by name.
    auto root = cache.get(fake.browser, fs::Inode{1});
    REQUIRE(root);
    REQUIRE((*root)->entries.size() == 5);
    CHECK((*root)->entries[0].entry.name == "A");
    CHECK((*root)->entries[4].entry.name == "file.txt");

    // The order, most recently used first, is 1 2. Reading 3: 3 1 2. Using 2: 2 3 1.
    REQUIRE(cache.get(fake.browser, fs::Inode{3}));
    CHECK(cache.size() == 3);
    REQUIRE(cache.get(fake.browser, fs::Inode{2}));
    CHECK(reads[2] == 1);
    // Reading 4 evicts 1: 4 2 3. Reading 1 again evicts 3: 1 4 2.
    REQUIRE(cache.get(fake.browser, fs::Inode{4}));
    CHECK(cache.size() == 3);
    CHECK(reads[1] == 1);
    REQUIRE(cache.get(fake.browser, fs::Inode{1}));
    CHECK(reads[1] == 2);
    // Using 2 and 4 reads neither: 4 2 1.
    REQUIRE(cache.get(fake.browser, fs::Inode{2}));
    REQUIRE(cache.get(fake.browser, fs::Inode{4}));
    CHECK(reads[2] == 1);
    CHECK(reads[4] == 1);
    CHECK(reads[3] == 1);
    // find() is a use too: 1 4 2, so reading 3 again evicts 2: 3 1 4.
    CHECK(cache.find(fs::Inode{1}));
    REQUIRE(cache.get(fake.browser, fs::Inode{3}));
    CHECK(reads[3] == 2);
    CHECK(!cache.find(fs::Inode{2}));
    CHECK(cache.find(fs::Inode{4}));
    CHECK(cache.find(fs::Inode{1}));

    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(!cache.find(fs::Inode{1}));
    REQUIRE(cache.get(fake.browser, fs::Inode{1}));
    CHECK(reads[1] == 3);
}

TEST_CASE("columns: put replaces a listing of the same directory; a capacity of 0 holds one") {
    ListingCache cache(0);
    CHECK(cache.capacity() == 1);
    cache.put(nullptr);
    CHECK(cache.size() == 0);
    auto a = std::make_shared<const Listing>(listing(7, {"one"}));
    auto b = std::make_shared<const Listing>(listing(7, {"one", "two"}));
    cache.put(a);
    cache.put(b);
    CHECK(cache.size() == 1);
    CHECK(cache.find(fs::Inode{7}) == b);
    cache.put(std::make_shared<const Listing>(listing(8, {})));
    CHECK(cache.size() == 1);
    CHECK(!cache.find(fs::Inode{7}));
    CHECK(cache.find(fs::Inode{8}));

    // A replaced listing takes no place of its own: the directories around it stay.
    ListingCache three(3);
    auto x = std::make_shared<const Listing>(listing(8, {"x"}));
    three.put(x);
    three.put(a);
    three.put(b);
    CHECK(three.size() == 2);
    auto y = std::make_shared<const Listing>(listing(9, {"y"}));
    three.put(y);
    CHECK(three.size() == 3);
    CHECK(three.find(fs::Inode{7}) == b);
    CHECK(three.find(fs::Inode{8}) == x);
    CHECK(three.find(fs::Inode{9}) == y);

    ListingCache wide;
    CHECK(wide.capacity() == 64);
    for (std::uint64_t d = 100; d < 200; ++d) wide.put(std::make_shared<const Listing>(listing(d, {})));
    CHECK(wide.size() == 64);
    CHECK(!wide.find(fs::Inode{135}));
    CHECK(wide.find(fs::Inode{136}));
    CHECK(wide.find(fs::Inode{199}));
}

TEST_CASE("columns: a directory that cannot be read is not cached") {
    auto fake = fakeBrowser();
    fake.reader->failing.insert(3);
    ListingCache cache;
    auto bad = cache.get(fake.browser, fs::Inode{3});
    REQUIRE(!bad);
    CHECK(bad.error().category() == ErrorCategory::Io);
    CHECK(bad.error().message() == "unreadable directory");
    CHECK(cache.size() == 0);
    CHECK(!cache.find(fs::Inode{3}));
    CHECK(!cache.get(fake.browser, fs::Inode{3}));
    CHECK(fake.reader->reads[3] == 2);

    // Its neighbours are unaffected, and once it reads it is cached like any other.
    REQUIRE(cache.get(fake.browser, fs::Inode{2}));
    fake.reader->failing.clear();
    auto good = cache.get(fake.browser, fs::Inode{3});
    REQUIRE(good);
    CHECK((*good)->entries.size() == 1);
    REQUIRE(cache.get(fake.browser, fs::Inode{3}));
    CHECK(fake.reader->reads[3] == 3);
    CHECK(cache.size() == 2);
}

TEST_CASE("columns: a real ext4 path plans, finds its rows and caches its ancestors") {
    auto disk = drstein::test::compositeDisk();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    auto browser = Browser::open(*tree, {0});
    REQUIRE(browser);
    REQUIRE(browser->caseSensitive());
    auto rootListing = browser->listCurrent();
    REQUIRE(rootListing);
    const Entry* dir = nullptr;
    for (const auto& e : rootListing->entries)
        if (!dir && e.isDir()) dir = &e;
    REQUIRE(dir);
    const std::string dirName = dir->entry.name;
    const fs::Inode dirInode = dir->entry.inode;

    // At the root with the folder selected: the root is current and the folder is peeked.
    auto plans = planColumns(browser->breadcrumbs(), dir);
    REQUIRE(plans.size() == 2);
    CHECK(plans[1].peek);
    CHECK(plans[1].dir == dirInode);
    CHECK(classifyClick(1, plans) == ColumnClick::EnterThenSelect);

    ListingCache cache;
    const auto rootShared = std::make_shared<const Listing>(*rootListing);
    cache.put(rootShared);
    auto peeked = cache.get(*browser, dirInode);
    REQUIRE(peeked);

    // Entered: the root column shows the folder selected, and comes from the cache.
    REQUIRE(browser->enter(dirName));
    const auto inside = planColumns(browser->breadcrumbs(), nullptr);
    REQUIRE(inside.size() == 2);
    CHECK(keptColumns(plans, inside) == 2);
    CHECK(inside[0].selectName == dirName);
    CHECK(inside[1].current);
    CHECK(inside[1].dir == dirInode);
    auto rootAgain = cache.get(*browser, inside[0].dir);
    REQUIRE(rootAgain);
    CHECK(*rootAgain == rootShared);
    const int row = rowOfName(**rootAgain, inside[0].selectName, browser->caseSensitive());
    REQUIRE(row >= 0);
    CHECK((*rootAgain)->entries[static_cast<std::size_t>(row)].entry.inode == dirInode);
    // The cached listing of the folder is what the browser lists there now.
    auto current = browser->listCurrent();
    REQUIRE(current);
    REQUIRE((*peeked)->entries.size() == current->entries.size());
    for (std::size_t i = 0; i < current->entries.size(); ++i) CHECK((*peeked)->entries[i].entry.name == current->entries[i].entry.name);
}

TEST_CASE("columns: on exFAT a path typed in another case still finds its rows") {
    auto disk = drstein::test::mbrComposite();
    auto tree = probe::probe(disk.device);
    REQUIRE(tree);
    NodePath exfatPath;
    for (std::size_t i = 0; i < tree->children.size(); ++i)
        if (tree->children[i].partition && tree->children[i].partition->index == 2) exfatPath = {static_cast<int>(i)};
    REQUIRE(!exfatPath.empty());
    auto browser = Browser::open(*tree, exfatPath);
    REQUIRE(browser);
    REQUIRE(!browser->caseSensitive());
    REQUIRE(browser->goTo("/DIR/Nested/deep"));
    const auto plans = planColumns(browser->breadcrumbs(), nullptr);
    REQUIRE(plans.size() == 4);
    CHECK(plans[0].selectName == "DIR");
    CHECK(plans[1].selectName == "Nested");

    // The crumbs carry the names as typed; the listings the names on disk.
    ListingCache cache;
    const std::string onDisk[] = {"dir", "nested", "deep"};
    for (std::size_t k = 0; k < 3; ++k) {
        CAPTURE(k);
        auto l = cache.get(*browser, plans[k].dir);
        REQUIRE(l);
        CHECK((rowOfName(**l, plans[k].selectName, true) >= 0) == (k == 2));
        const int row = rowOfName(**l, plans[k].selectName, browser->caseSensitive());
        REQUIRE(row >= 0);
        const Entry& e = (*l)->entries[static_cast<std::size_t>(row)];
        CHECK(e.entry.name == onDisk[k]);
        CHECK(e.entry.inode == plans[k + 1].dir);
    }
    CHECK(cache.size() == 3);
}
