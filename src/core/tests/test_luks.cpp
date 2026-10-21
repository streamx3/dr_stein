// SPDX-License-Identifier: MIT
// The case this app exists for on a Mac: a LUKS2 container with ext4 inside, a
// passphrase, files read back. libstein's LUKS fixtures hold a bare ext2, so the
// container is built here around the ext4 fixture through libstein's own
// encrypting payload device, reusing the fixture's header, key slot and
// passphrase. With DRSTEIN_EXPORT_LUKS_EXT4=<file> the image is also written
// out, so the application can be driven by hand.
#include "drstein/core/browse.hpp"
#include "drstein/core/mounts.hpp"
#include "drstein/core/topology.hpp"
#include "fixtures.hpp"
#include "stein/container/luks.hpp"
#include "stein/fs/reader.hpp"

#include <doctest.h>

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

using namespace drstein::core;
using namespace stein;

namespace {

constexpr const char* kPassphrase = "luks test passphrase";

// The fixture's LUKS2 header and key slots in front, the ext4 fixture encrypted
// into the payload behind them. The segment is "dynamic", so the payload is
// simply as large as the device allows.
std::shared_ptr<MemoryDevice> luksWithExt4() {
    auto fixture = drstein::test::loadFixture("luks/luks2.sparse");
    auto ext4 = drstein::test::loadFixture("extfs/ext4.sparse");
    auto header = container::Luks::open(fixture);
    REQUIRE(header);
    const ByteCount off = header->info().payloadOffset;
    auto dev = std::make_shared<MemoryDevice>(off + alignUp(ext4->size(), MiB), 512);
    std::vector<std::byte> head(static_cast<std::size_t>(off));
    REQUIRE(fixture->readAt(0, head));
    REQUIRE(dev->writeAt(0, head));
    auto luks = container::Luks::open(dev);
    REQUIRE(luks);
    REQUIRE(luks->info().payloadSize >= ext4->size());
    auto payload = luks->openPayload(std::string(kPassphrase), false);
    REQUIRE(payload);
    std::vector<std::byte> chunk(static_cast<std::size_t>(MiB));
    for (ByteCount at = 0; at < ext4->size(); at += MiB) {
        const ByteCount n = std::min<ByteCount>(MiB, ext4->size() - at);
        std::span<std::byte> window(chunk.data(), static_cast<std::size_t>(n));
        REQUIRE(ext4->readAt(at, window));
        REQUIRE((*payload)->writeAt(at, window));
    }
    REQUIRE((*payload)->flush());
    return dev;
}

// "f <size> <path>" and "sha256 <hex> <path>" lines of the ext4 fixture's oracle.
struct Ext4Oracle {
    std::map<std::string, std::uint64_t> sizes;
    std::map<std::string, std::string> sha256;
};
Ext4Oracle ext4Oracle() {
    Ext4Oracle o;
    std::ifstream in(std::string(STEIN_FIXTURE_DIR) + "/extfs/ext4.oracle.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("f ")) {
            const auto sp = line.find(' ', 2);
            o.sizes[line.substr(sp + 1)] = std::stoull(line.substr(2, sp - 2));
        } else if (line.starts_with("sha256 ") && line.size() > 72) {
            o.sha256[line.substr(72)] = line.substr(7, 64);
        }
    }
    return o;
}

const Entry* entryNamed(const Listing& listing, const std::string& name) {
    for (const auto& e : listing.entries)
        if (e.entry.name == name) return &e;
    return nullptr;
}

} // namespace

TEST_CASE("LUKS2 with ext4 inside: locked without the passphrase, a browsable payload row with it") {
    auto dev = luksWithExt4();
    auto locked = probe::probe(dev);
    REQUIRE(locked);
    REQUIRE(locked->content);
    CHECK(locked->content->type() == fs::FsType::Luks2);
    CHECK(locked->children.empty());
    auto d = nodeDetails(*locked, {});
    CHECK(d.isLockedContainer);
    CHECK(!d.canBrowse);

    probe::Options wrong;
    wrong.passphrases = {"not it"};
    wrong.tcrypt = false;
    auto still = probe::probe(dev, wrong);
    REQUIRE(still);
    CHECK(still->children.empty());
    CHECK(nodeDetails(*still, {}).isLockedContainer);

    probe::Options right;
    right.passphrases = {"not it", kPassphrase};   // a wrong one first must not matter
    right.tcrypt = false;
    auto open = probe::probe(dev, right);
    REQUIRE(open);
    REQUIRE(open->children.size() == 1);
    const auto& payload = open->children[0];
    CHECK(payload.kind == probe::NodeKind::Decrypted);
    REQUIRE(payload.content);
    CHECK(payload.content->type() == fs::FsType::Ext4);
    CHECK(!nodeDetails(*open, {}).isLockedContainer);
    auto rows = topologyRows(*open, false);
    REQUIRE(rows.size() >= 2);
    auto pd = nodeDetails(*open, {0});
    CHECK(pd.canBrowse);
    CHECK(pd.canMount == MountManager::available());
}

TEST_CASE("LUKS2 with ext4 inside: files read back match the ext4 oracle") {
    auto dev = luksWithExt4();
    probe::Options o;
    o.passphrases = {kPassphrase};
    o.tcrypt = false;
    auto tree = probe::probe(dev, o);
    REQUIRE(tree);
    auto browser = Browser::open(*tree, {0});
    REQUIRE(browser);
    CHECK(browser->fsName() == "ext4");
    const auto oracle = ext4Oracle();
    REQUIRE(oracle.sizes.size() > 300);
    REQUIRE(oracle.sha256.size() > 300);

    auto listing = browser->listCurrent();
    REQUIRE(listing);
    std::size_t files = 0;
    for (const auto& e : listing->entries) {
        if (e.stat.type != fs::FileType::File) continue;
        ++files;
        auto it = oracle.sizes.find(e.entry.name);
        REQUIRE_MESSAGE(it != oracle.sizes.end(), e.entry.name);
        CHECK(e.stat.size == it->second);
    }
    CHECK(files >= 4);

    // Whole files hashed against the oracle: a tiny one, the 300 KiB one that spans
    // many encrypted sectors, and one three directories down.
    NullProgressSink sink;
    Progress progress(sink);
    for (const char* path : {"hello.txt", "big.bin", "medium.bin", "dir/nested/deep/leaf.txt"}) {
        CAPTURE(path);
        const std::string p(path);
        const auto slash = p.rfind('/');
        const std::string dir = slash == std::string::npos ? "/" : "/" + p.substr(0, slash);
        const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
        REQUIRE(browser->goTo(dir));
        auto l = browser->listCurrent();
        REQUIRE(l);
        const Entry* found = entryNamed(*l, name);
        REQUIRE(found);
        auto hash = sha256Hex(browser->reader(), found->entry.inode, found->stat, progress);
        REQUIRE(hash);
        CHECK(*hash == oracle.sha256.at(p));
    }
    REQUIRE(browser->goTo("/"));
    auto root = browser->listCurrent();
    REQUIRE(root);
    const Entry* hello = entryNamed(*root, "hello.txt");
    REQUIRE(hello);
    auto bytes = fs::readAll(browser->reader(), hello->entry.inode);
    REQUIRE(bytes);
    CHECK(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) == "hello from libstein\n");
}

TEST_CASE("export the LUKS2+ext4 image when asked") {
    const char* out = std::getenv("DRSTEIN_EXPORT_LUKS_EXT4");
    if (!out || !*out) return;
    auto dev = luksWithExt4();
    drstein::test::writeDevice(*dev, out);
    MESSAGE("wrote " << out << " (passphrase: " << kPassphrase << ")");
}
