// SPDX-License-Identifier: MIT
#include "drstein/core/topology.hpp"

#include "drstein/core/format.hpp"
#include "stein/container/luks.hpp"
#include "stein/core/strings.hpp"
#include "stein/mount/mount.hpp"
#include "stein/pt/gpt_table.hpp"

#include <algorithm>

namespace drstein::core {

using namespace stein;
using layout::Validity;
using probe::Node;
using probe::NodeKind;

const Node* nodeAt(const Node& root, const NodePath& path) {
    const Node* n = &root;
    for (int i : path) {
        if (i < 0 || static_cast<std::size_t>(i) >= n->children.size()) return nullptr;
        n = &n->children[static_cast<std::size_t>(i)];
    }
    return n;
}

NodePath parentOf(const NodePath& path) {
    if (path.empty()) return {};
    return NodePath(path.begin(), path.end() - 1);
}

namespace {

bool isLocked(const Node& node) {
    if (!node.content) return false;
    const auto t = node.content->type();
    if (t != fs::FsType::Luks1 && t != fs::FsType::Luks2) return false;
    for (const auto& c : node.children)
        if (c.kind == NodeKind::Decrypted) return false;
    return true;
}

bool readable(const Node& node) {
    return node.content && fs::has(node.content->capabilities(), fs::Capability::Read);
}

bool repairable(const Node& node) {
    if (!node.table) return false;
    for (const auto& d : node.table->diagnostics())
        if (d.repairable) return true;
    return false;
}

std::string quoted(const std::string& s) { return s.empty() ? "" : "\"" + s + "\""; }

std::string healthWords(const Node& node) {
    int warnings = 0, errors = 0;
    std::vector<const Node*> stack{&node};
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        for (const auto& note : n->notes) {
            if (note.severity == Validity::Warning) ++warnings;
            if (note.severity == Validity::Error) ++errors;
        }
        for (const auto& c : n->children) stack.push_back(&c);
    }
    if (errors) return std::to_string(errors) + (errors == 1 ? " error" : " errors");
    if (warnings) return std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
    return "OK";
}

const Node* firstChildOfKind(const Node& node, NodeKind kind) {
    for (const auto& c : node.children)
        if (c.kind == kind) return &c;
    return nullptr;
}

std::string shortFsName(const fs::FileSystem& f) {
    const auto t = f.type();
    switch (t) {
    case fs::FsType::Luks1: return "LUKS1";
    case fs::FsType::Luks2: return "LUKS2";
    case fs::FsType::Lvm2Pv: return "LVM2";
    case fs::FsType::MdRaidMember: return "mdraid";
    case fs::FsType::BitLocker: return "BitLocker";
    default: return std::string(fs::displayName(t));
    }
}

void appendUsed(std::vector<DetailRow>& rows, const fs::FsInfo& info) {
    if (info.usedBytes && info.totalBytes) rows.push_back({"used", sizeText(*info.usedBytes) + " of " + sizeText(*info.totalBytes), false});
    else if (info.totalBytes) rows.push_back({"size", sizeText(*info.totalBytes), false});
}

std::string capabilityText(std::uint32_t caps) {
    struct Entry { fs::Capability c; const char* name; };
    static const Entry entries[] = {
        {fs::Capability::Identify, "Identify"}, {fs::Capability::Label, "Label"}, {fs::Capability::Uuid, "Uuid"},
        {fs::Capability::UsedBlocks, "UsedBlocks"}, {fs::Capability::Read, "Read"}, {fs::Capability::Write, "Write"},
        {fs::Capability::SetLabel, "SetLabel"}, {fs::Capability::SetUuid, "SetUuid"}, {fs::Capability::Grow, "Grow"},
        {fs::Capability::Shrink, "Shrink"}, {fs::Capability::Check, "Check"}, {fs::Capability::Create, "Create"}};
    std::string out;
    for (const auto& e : entries)
        if (fs::has(caps, e.c)) out += (out.empty() ? "" : " ") + std::string(e.name);
    return out;
}

void appendContentRows(std::vector<DetailRow>& rows, const Node& node) {
    if (!node.content) return;
    const fs::FileSystem& f = *node.content;
    const auto& info = f.info();
    std::string line = shortFsName(f);
    if (!info.label.empty()) line += " · label " + info.label;
    if (!info.uuid.empty()) line += " · " + info.uuid;
    rows.push_back({"content", line});
    if (!info.version.empty()) rows.push_back({"version", info.version});
    if (info.blockSize) rows.push_back({"block size", sizeBinary(*info.blockSize)});
    appendUsed(rows, info);
    if (info.clean) rows.push_back({"state", *info.clean ? "clean" : "dirty: not cleanly unmounted", false});
    if (!info.extra.empty()) rows.push_back({"info", info.extra, false});
    if (!info.features.empty()) {
        std::string feats;
        for (const auto& x : info.features) feats += (feats.empty() ? "" : " ") + x;
        rows.push_back({"features", feats});
    }
    rows.push_back({"capabilities", capabilityText(f.capabilities())});
    const auto t = f.type();
    if (t == fs::FsType::Luks1 || t == fs::FsType::Luks2) {
        if (auto luks = container::Luks::open(node.device)) {
            const auto& li = luks->info();
            rows.push_back({"cipher", li.cipher + "-" + li.mode + " · " + std::to_string(li.keyBytes * 8) + "-bit"});
            std::string slots;
            for (const auto& s : li.slots) slots += (slots.empty() ? "" : ", ") + std::to_string(s.id) + " " + s.kdf + (s.active ? "" : " (inactive)");
            rows.push_back({"key slots", slots.empty() ? "none" : slots});
            rows.push_back({"payload", "at " + sizeBinary(li.payloadOffset) + " · " + sizeText(li.payloadSize)});
            if (!li.supported) rows.push_back({"note", "cannot open in-process: " + li.unsupportedWhy, false});
        }
    }
    for (const auto& d : f.diagnostics())
        if (d.severity >= Validity::Info) rows.push_back({validityText(d.severity), d.message + " (" + d.code + ")", false});
}

void appendTableRows(std::vector<DetailRow>& rows, const Node& node) {
    if (!node.table) return;
    const pt::PartitionTable& t = *node.table;
    const auto& g = t.geometry();
    std::string line = schemeName(t.type());
    if (auto* gpt = dynamic_cast<const pt::GptTable*>(&t)) {
        line += " · primary " + (gpt->primaryState().valid() ? "LBA " + grouped(gpt->primaryState().lba) : std::string("damaged"));
        line += " · backup " + (gpt->backupState().valid() ? "LBA " + grouped(gpt->backupState().lba) : std::string("damaged"));
        rows.push_back({"table", line});
        rows.push_back({"disk GUID", gpt->diskGuid().toString()});
        rows.push_back({"entries", std::to_string(gpt->entryCount()) + " × " + std::to_string(gpt->entrySize()) + " B" + (gpt->isHybridMbr() ? " · hybrid MBR" : gpt->hasProtectiveMbr() ? " · protective MBR" : "")});
    } else if (t.type() == pt::TableType::None) {
        rows.push_back({"table", "none (whole-device content)", false});
    } else {
        rows.push_back({"table", line + " · " + std::to_string(t.partitions().size()) + " partitions"});
    }
    rows.push_back({"sectors", std::to_string(g.logicalSectorSize) + " B logical · " + std::to_string(g.physicalSectorSize) + " B physical · " + grouped(g.sectors()) + " total"});
    if (t.type() != pt::TableType::None) {
        rows.push_back({"first usable", lbaText(t.firstUsableLba())});
        rows.push_back({"last usable", lbaText(t.lastUsableLba())});
    }
    for (const auto& d : t.diagnostics())
        if (d.severity >= Validity::Info) rows.push_back({validityText(d.severity), d.message + (d.repairable ? " · repairable" : "") + " (" + d.code + ")", false});
}

} // namespace

std::string schemeName(pt::TableType type) {
    switch (type) {
    case pt::TableType::None: return "no table";
    case pt::TableType::Unknown: return "unknown table";
    default: return toUpper(std::string(pt::toString(type)));
    }
}

std::string partitionTypeText(const pt::Partition& p) {
    const auto* info = pt::types::find(p.type);
    std::string code;
    if (p.type.scheme == pt::TableType::Gpt && info && info->sgdiskCode[0]) code = info->sgdiskCode;
    else code = p.type.code();
    return code + " · " + pt::types::name(p.type);
}

std::string kindLabel(const Node& node) {
    switch (node.kind) {
    case NodeKind::Device: return "Device";
    case NodeKind::Partition: return node.partition ? "Partition " + std::to_string(node.partition->index) : "Partition";
    case NodeKind::Free: return "Free";
    case NodeKind::Metadata: return "Metadata";
    case NodeKind::Decrypted: return "Decrypted";
    case NodeKind::Volume: return "Volume";
    }
    return "";
}

std::string displayName(const Node& node) {
    switch (node.kind) {
    case NodeKind::Device: return "";
    case NodeKind::Partition:
        if (node.partition && !node.partition->name.empty()) return node.partition->name;
        if (node.content && !node.content->info().label.empty()) return node.content->info().label;
        if (node.partition) return pt::types::name(node.partition->type);
        return node.name;
    case NodeKind::Free: return "Free space";
    case NodeKind::Metadata: return node.name;
    case NodeKind::Decrypted: {
        // The parent's container type is what was unlocked; the node name says which.
        return node.name == "decrypted" ? "Unlocked payload" : node.name;
    }
    case NodeKind::Volume: return node.name;
    }
    return node.name;
}

std::string contentSummary(const Node& node) {
    if (!node.content) return "";
    const auto& info = node.content->info();
    std::string s = shortFsName(*node.content);
    if (!info.label.empty()) s += " " + quoted(info.label);
    if (info.clean) s += *info.clean ? " · clean" : " · dirty";
    return s;
}

std::string contentChain(const Node& node) {
    std::vector<std::string> parts;
    const Node* n = &node;
    for (int hops = 0; n && hops < 6; ++hops) {
        if (n->content) parts.push_back(shortFsName(*n->content));
        const Node* next = firstChildOfKind(*n, NodeKind::Decrypted);
        if (!next) next = firstChildOfKind(*n, NodeKind::Volume);
        if (!next && n->table && n->kind != NodeKind::Device && !n->children.empty()) {
            parts.push_back(schemeName(n->table->type()) + " table");
            break;
        }
        n = next;
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : " → ") + p;
    return out;
}

namespace {

std::string contentColumn(const Node& node) {
    if (node.kind == NodeKind::Device) {
        if (node.table && node.table->type() != pt::TableType::None) {
            std::string s = schemeName(node.table->type());
            if (auto* gpt = dynamic_cast<const pt::GptTable*>(node.table.get())) s += " · " + std::to_string(gpt->entryCount()) + " entries";
            else s += " · " + std::to_string(node.table->partitions().size()) + " partitions";
            return s;
        }
        if (node.content) return contentSummary(node);
        return "no partition table";
    }
    if (node.kind == NodeKind::Free) return "—";
    const Node* dec = firstChildOfKind(node, NodeKind::Decrypted);
    const Node* vol = firstChildOfKind(node, NodeKind::Volume);
    if (dec || vol) return contentChain(node);
    if (node.content) {
        const auto t = node.content->type();
        if (t == fs::FsType::Luks1 || t == fs::FsType::Luks2) return shortFsName(*node.content) + " · locked";
        if (t == fs::FsType::Lvm2Pv) return "LVM2 PV" + (node.content->info().label.empty() ? "" : " · VG " + node.content->info().label);
        return contentSummary(node);
    }
    if (node.table) return schemeName(node.table->type()) + " table";
    if (node.kind == NodeKind::Partition && node.partition && node.partition->isExtended) return "extended container";
    return "unrecognised";
}

double usedFractionOf(const Node& node) {
    const Node* n = &node;
    for (int hops = 0; n && hops < 6; ++hops) {
        if (n->content) {
            const auto& info = n->content->info();
            if (info.usedBytes && info.totalBytes && *info.totalBytes) return static_cast<double>(*info.usedBytes) / static_cast<double>(*info.totalBytes);
        }
        const Node* next = firstChildOfKind(*n, NodeKind::Decrypted);
        if (!next) next = firstChildOfKind(*n, NodeKind::Volume);
        n = next;
    }
    return -1;
}

std::string segmentLabel(const Node& node) {
    if (node.kind == NodeKind::Free) return "";
    std::string s = displayName(node);
    const std::string chain = contentChain(node);
    if (!chain.empty()) s += " · " + chain;
    s += " · " + sizeText(node.region.length);
    return s;
}

void collectRows(const Node& node, const NodePath& path, int depth, int segment, std::vector<TopologyRow>& out) {
    TopologyRow r;
    r.path = path;
    r.depth = depth;
    r.kindLabel = kindLabel(node);
    r.name = displayName(node);
    r.content = contentColumn(node);
    r.sizeBytes = node.region.length;
    r.sizeText = node.kind == NodeKind::Partition || node.kind == NodeKind::Free ? sizeBinaryOrDecimal(node.region.length) : sizeText(node.region.length);
    r.region = node.region;
    r.health = node.health();
    r.healthText = node.kind == NodeKind::Free ? "" : healthWords(node);
    r.segment = segment;
    r.canBrowse = readable(node);
    r.canInspect = node.table || node.content;
    r.canRepair = repairable(node);
    r.canMount = r.canBrowse && mount::Mount::available();
    r.isLockedContainer = isLocked(node);
    r.usedFraction = usedFractionOf(node);
    out.push_back(std::move(r));
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        NodePath child = path;
        child.push_back(static_cast<int>(i));
        collectRows(node.children[i], child, depth + 1, segment, out);
    }
}

} // namespace

// Partitions are sized in binary units when they are round there ("512 MiB"),
// decimal otherwise ("400 GB"), which matches how people name them.
std::string sizeBinaryOrDecimal(ByteCount bytes) {
    if (bytes && bytes % MiB == 0) {
        const ByteCount mib = bytes / MiB;
        if (mib < 1024 || mib % 1024 != 0) return sizeBinary(bytes);
        const ByteCount gib = mib / 1024;
        if (gib < 64) return sizeBinary(bytes);
    }
    return sizeText(bytes);
}

std::vector<Segment> segments(const Node& root) {
    std::vector<Segment> out;
    int color = 0;
    for (std::size_t i = 0; i < root.children.size(); ++i) {
        const Node& c = root.children[i];
        if (c.kind != NodeKind::Partition && c.kind != NodeKind::Free) continue;
        if (c.partition && c.partition->isExtended) continue;   // logicals inside it are listed on their own
        Segment s;
        s.path = {static_cast<int>(i)};
        s.region = c.region;
        s.isFree = c.kind == NodeKind::Free;
        s.label = segmentLabel(c);
        s.colorIndex = s.isFree ? -1 : color++;
        out.push_back(std::move(s));
    }
    if (out.empty() && root.content) {
        Segment s;
        s.path = {};
        s.region = root.region;
        s.label = contentSummary(root) + " · " + sizeText(root.region.length);
        s.colorIndex = 0;
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<TopologyRow> topologyRows(const Node& root, bool expertMode) {
    std::vector<TopologyRow> out;
    const auto segs = segments(root);
    auto segmentFor = [&](const NodePath& path) {
        for (std::size_t i = 0; i < segs.size(); ++i)
            if (!segs[i].path.empty() && segs[i].path == path) return static_cast<int>(i);
        return -1;
    };
    collectRows(root, {}, 0, segs.size() == 1 && segs[0].path.empty() ? 0 : -1, out);
    // Top-level partitions get their segment; descendants inherit it.
    for (auto& r : out) {
        if (r.path.empty()) continue;
        r.segment = segmentFor(NodePath{r.path.front()});
    }
    if (expertMode && root.table && root.table->type() != pt::TableType::None) {
        const auto regions = root.table->metadataRegions();
        const std::uint32_t ss = root.table->geometry().logicalSectorSize;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            TopologyRow r;
            r.path = {};
            r.depth = 1;
            r.kindLabel = "Metadata";
            r.isMetadata = true;
            r.metadataIndex = static_cast<int>(i);
            r.region = regions[i];
            r.sizeBytes = regions[i].length;
            r.sizeText = sizeBinary(regions[i].length);
            const std::string scheme = schemeName(root.table->type());
            const Region& reg = regions[i];
            const ByteCount total = root.region.length;
            if (reg.offset == 0) r.name = scheme == "GPT" ? "Protective MBR" : scheme + " sector";
            else if (scheme != "GPT") r.name = scheme + " metadata";
            else if (reg.end() >= total) r.name = "GPT backup header";
            else if (reg.end() + 34 * ss >= total) r.name = "GPT backup entries";
            else if (reg.length <= ss) r.name = "GPT primary header";
            else r.name = "GPT primary entries";
            r.content = "LBA " + grouped(regions[i].offset / ss) + " – " + grouped((regions[i].end() - 1) / ss);
            r.health = Validity::Ok;
            r.healthText = "OK";
            r.canInspect = true;
            out.push_back(std::move(r));
        }
    }
    return out;
}

NodeDetails nodeDetails(const Node& root, const NodePath& path) {
    NodeDetails d;
    const Node* n = nodeAt(root, path);
    if (!n) return d;
    const Node& node = *n;
    d.kindLabel = kindLabel(node);
    d.title = displayName(node);
    if (d.title.empty()) d.title = node.device ? node.device->name() : node.name;
    const std::uint32_t ss = root.table ? root.table->geometry().logicalSectorSize : (node.device ? node.device->sectorSize() : 512);
    if (node.kind == NodeKind::Device) d.regionText = byteRangeText(node.region);
    else if (node.kind == NodeKind::Partition || node.kind == NodeKind::Free)
        d.regionText = lbaRangeText(node.region.offset / ss, node.region.length ? (node.region.end() - 1) / ss : node.region.offset / ss, ss);
    else {
        const Node* parent = nodeAt(root, parentOf(path));
        d.regionText = "inside " + (parent ? toLower(kindLabel(*parent)) : std::string("parent")) + " · " + sizeText(node.region.length);
    }

    if (node.kind == NodeKind::Partition && node.partition) {
        const auto& p = *node.partition;
        d.rows.push_back({"type", partitionTypeText(p)});
        if (!p.uuid.isNil()) d.rows.push_back({"uuid", p.uuid.toString()});
        std::string flags;
        if (p.type.scheme == pt::TableType::Mbr && (p.attributes & pt::Partition::kMbrBootable)) flags += "bootable ";
        if (p.isExtended) flags += "extended ";
        if (p.isLogical) flags += "logical ";
        if (p.type.scheme == pt::TableType::Gpt && p.attributes) flags += "attributes " + toHex(p.attributes) + " ";
        if (!flags.empty()) d.rows.push_back({"flags", flags});
    }
    if (node.kind == NodeKind::Free) {
        const SectorCount sectors = node.region.length / ss;
        d.rows.push_back({"usable", "yes · " + grouped(sectors) + " sectors", false});
    }
    appendTableRows(d.rows, node);
    appendContentRows(d.rows, node);
    if (node.kind == NodeKind::Decrypted && node.device) d.rows.push_back({"device", node.device->name()});
    if (node.kind == NodeKind::Volume) d.rows.push_back({"mappable", node.device ? "yes" : "no: a physical volume is missing", false});

    // Usage bar from the superblock, through containers.
    const double used = usedFractionOf(node);
    if (used >= 0) {
        d.usedFraction = used;
        const Node* fsNode = &node;
        while (fsNode && !(fsNode->content && fsNode->content->info().usedBytes)) {
            const Node* next = firstChildOfKind(*fsNode, NodeKind::Decrypted);
            if (!next) next = firstChildOfKind(*fsNode, NodeKind::Volume);
            fsNode = next;
        }
        if (fsNode) {
            d.usedLabel = shortFsName(*fsNode->content) + " used";
            d.usedText = sizeText(*fsNode->content->info().usedBytes) + " of " + sizeText(*fsNode->content->info().totalBytes);
        }
    }
    d.notes = node.notes;
    if (node.content) d.subvolumes = node.content->subvolumes();
    d.canBrowse = readable(node);
    d.canInspect = node.table || node.content;
    d.canRepair = repairable(node);
    d.canMount = d.canBrowse && mount::Mount::available();
    d.isLockedContainer = isLocked(node);
    return d;
}

NodeDetails metadataDetails(const Node& root, int metadataIndex) {
    NodeDetails d;
    d.kindLabel = "Metadata";
    if (!root.table) return d;
    const auto regions = root.table->metadataRegions();
    if (metadataIndex < 0 || static_cast<std::size_t>(metadataIndex) >= regions.size()) return d;
    const Region r = regions[static_cast<std::size_t>(metadataIndex)];
    const std::uint32_t ss = root.table->geometry().logicalSectorSize;
    d.title = schemeName(root.table->type()) + " metadata";
    d.regionText = lbaRangeText(r.offset / ss, (r.end() - 1) / ss, ss);
    d.rows.push_back({"bytes", hexRangeText(r)});
    d.rows.push_back({"size", sizeBinary(r.length)});
    appendTableRows(d.rows, root);
    d.canInspect = true;
    return d;
}

Expected<Usage> usageOf(const Node& node) {
    if (!node.content) return fail(ErrorCategory::NotFound, "no filesystem here");
    const auto& info = node.content->info();
    Usage u;
    if (info.usedBytes && info.totalBytes) {
        u.usedBytes = *info.usedBytes;
        u.totalBytes = *info.totalBytes;
        return u;
    }
    if (!fs::has(node.content->capabilities(), fs::Capability::UsedBlocks))
        return fail(ErrorCategory::Unsupported, std::string(fs::displayName(info.type)) + " has no readable allocation map");
    auto map = node.content->allocationMap();
    if (!map) return fail(map.error());
    u.usedBytes = map->usedBytes();
    u.totalBytes = info.totalBytes ? *info.totalBytes : map->blocks() * map->blockSize();
    return u;
}

} // namespace drstein::core
