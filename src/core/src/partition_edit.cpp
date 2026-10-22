// SPDX-License-Identifier: MIT
#include "drstein/core/partition_edit.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/imaging.hpp"
#include "stein/core/guid.hpp"
#include "stein/core/strings.hpp"
#include "stein/platform/platform.hpp"
#include "stein/pt/mbr_table.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace drstein::core {

using namespace stein;

Expected<Lba> parseLba(std::string_view textIn, std::uint32_t sectorSize) {
    const std::string text(trim(textIn));
    if (text.empty()) return fail(ErrorCategory::InvalidArgument, "empty value");
    if (text.back() == 's' || text.back() == 'S') {
        char* end = nullptr;
        const auto v = std::strtoull(text.c_str(), &end, 10);
        if (end != text.c_str() + text.size() - 1) return fail(ErrorCategory::InvalidArgument, "bad sector value: " + text);
        return Lba{v};
    }
    auto bytes = parseSize(text);
    if (!bytes) return fail(bytes.error());
    if (*bytes % sectorSize) return fail(ErrorCategory::InvalidArgument, text + " is not a multiple of the sector size (" + std::to_string(sectorSize) + ")");
    return Lba{*bytes / sectorSize};
}

Expected<pt::PartitionType> parseType(pt::TableType scheme, std::string_view textIn) {
    const std::string text(trim(textIn));
    if (text.empty()) {
        if (auto t = pt::types::forRole(pt::Role::LinuxFilesystem, scheme)) return *t;
        return fail(ErrorCategory::InvalidArgument, "a partition type is required for this table scheme");
    }
    switch (scheme) {
    case pt::TableType::Gpt:
        if (auto t = pt::types::fromSgdiskCode(text)) return *t;
        if (auto g = Uuid::parse(text)) return pt::PartitionType::gpt(*g);
        return fail(ErrorCategory::InvalidArgument, "unknown GPT type (use an sgdisk code like 8300 or a GUID): " + text);
    case pt::TableType::Mbr: {
        char* end = nullptr;
        const auto v = std::strtoul(text.c_str(), &end, 16);
        if (*end || v > 0xFF) return fail(ErrorCategory::InvalidArgument, "MBR type must be a hex id like 83 or 0x07: " + text);
        return pt::PartitionType::mbr(static_cast<std::uint8_t>(v));
    }
    case pt::TableType::Apm:
        return pt::PartitionType::apm(text);
    default:
        return fail(ErrorCategory::Unsupported, "this table scheme cannot be edited");
    }
}

Expected<EditSession> EditSession::open(const SourceDescriptor& source, const OpenOptions& optionsIn) {
    OpenOptions options = optionsIn;
    options.writable = true;
    auto dev = openDevice(source, options);
    if (!dev) return fail(dev.error());
    EditSession s;
    s.m_source = source;
    s.m_device = *dev;
    s.m_stack = std::make_unique<ops::OperationStack>(s.m_device);
    return s;
}

Expected<void> EditSession::createTable(pt::TableType type) { return m_stack->push(std::make_unique<ops::CreateTable>(type)); }

namespace {

// Where a new partition may go by default. The table counts the extended container's interior as
// free (logicals fill it), so the candidates depend on what is being added: a primary may only
// use the outside, and regions that straddle the container are cut at its edges.
std::vector<pt::FreeRegion> primaryCandidates(const pt::PartitionTable& table, const pt::Partition* extended, SectorCount align) {
    auto free = table.freeRegions(align);
    if (!extended) return free;
    std::vector<pt::FreeRegion> out;
    auto keep = [&](Lba first, Lba last) {
        if (last >= first && last - first + 1 >= align) out.push_back({first, last});
    };
    for (const auto& f : free) {
        if (f.firstLba < extended->firstLba) keep(f.firstLba, std::min(f.lastLba, extended->firstLba - 1));
        if (f.lastLba > extended->lastLba) keep(std::max(f.firstLba, extended->lastLba + 1), f.lastLba);
    }
    return out;
}

// A logical may only use the inside, less what the EBRs need. freeRegions() knows nothing of EBRs
// (they are not partitions), so each gap is cut by hand: the new logical's own EBR takes
// kDefaultEbrGap sectors at the gap's start (where the library puts it), and the logical behind
// the gap keeps the sector its EBR is in, so the gap ends at ebrLba - 1. When that EBR is the
// chain's head (the container's first sector, which a newcomer in front takes over) or sits at
// the gap's start, the chain relocates it to the default gap before that logical's data, and the
// gap ends there instead. A gap too small for an EBR gap plus one aligned unit of data is no
// candidate: it used to win as "largest" and yield a one-sector logical on top of the next
// partition, and a gap that was not the container's tail used to end on the next EBR's sector,
// which the chain then had no room for.
std::vector<pt::FreeRegion> logicalCandidates(const pt::PartitionTable& table, const pt::Partition& extended, SectorCount align) {
    constexpr Lba gap = pt::MbrTable::kDefaultEbrGap;
    std::vector<pt::FreeRegion> out;
    for (const auto& f : table.freeRegions(1)) {
        const Lba first = std::max(f.firstLba, extended.firstLba);
        Lba last = std::min(f.lastLba, extended.lastLba);
        for (const auto& q : table.partitions()) {
            if (!q.isLogical || q.firstLba != f.lastLba + 1) continue;   // the logical right behind the gap
            const Lba cap = q.ebrLba && *q.ebrLba > first ? *q.ebrLba - 1 : (q.firstLba > gap ? q.firstLba - 1 - gap : 0);
            last = std::min(last, cap);
        }
        if (last >= first && last - first + 1 >= gap + align) out.push_back({first, last});
    }
    return out;
}

} // namespace

Expected<void> EditSession::addPartition(const AddPartitionForm& form) {
    const probe::Node& tree = preview();
    if (!tree.table || tree.table->type() == pt::TableType::None) return fail(ErrorCategory::NotFound, "no partition table; create one first");
    const pt::PartitionTable& table = *tree.table;
    const std::uint32_t ss = m_device->sectorSize();
    pt::Partition p;
    p.index = form.index.value_or(0);
    p.name = form.name;
    auto type = parseType(table.type(), form.typeText);
    if (!type) return fail(type.error());
    p.type = *type;
    const pt::Partition* extended = nullptr;
    for (const auto& q : table.partitions())
        if (q.isExtended) extended = &q;
    if (form.logical) {
        if (!table.limits().supportsLogical) return fail(ErrorCategory::Unsupported, "only MBR tables have logical partitions");
        if (!extended) return fail(ErrorCategory::NotFound, "no extended partition to hold a logical partition");
        p.isLogical = true;
    }
    // Default placement: the largest free region, start aligned to 1 MiB (what sgdisk does).
    const SectorCount align = std::max<SectorCount>(1, (1 * MiB) / ss);
    const auto free = form.logical ? logicalCandidates(table, *extended, align) : primaryCandidates(table, extended, align);
    if (free.empty() && form.startText.empty())
        return fail(ErrorCategory::OutOfRange, form.logical ? "no free space inside the extended partition" : "no free space in the table");
    pt::FreeRegion target;
    for (const auto& f : free)
        if (f.sectors() > target.sectors()) target = f;
    if (!form.startText.empty()) {
        auto s = parseLba(form.startText, ss);
        if (!s) return fail(s.error());
        p.firstLba = *s;
        for (const auto& f : free)
            if (*s >= f.firstLba && *s <= f.lastLba) target = f;
    } else {
        // A logical's EBR goes kDefaultEbrGap sectors before its data (the first one's at the
        // container's start), so its data begins that far into the region, not at its edge.
        const Lba earliest = form.logical ? target.firstLba + pt::MbrTable::kDefaultEbrGap : target.firstLba;
        p.firstLba = ((earliest + align - 1) / align) * align;
    }
    if (form.logical && p.index == 0) {
        // MBR numbers logicals by their place in the EBR chain, so this one's slot is 5 plus the
        // logicals in front of it. The table API takes that number only while it is free (an add at
        // the chain's end); inserting in front renumbers those behind, which it cannot express, so
        // the slot stays unnamed in the pending text rather than named wrongly. The preview, re-read
        // from the chain, shows the real one.
        std::uint32_t slot = 5;
        for (const auto& q : table.partitions())
            if (q.isLogical && q.firstLba < p.firstLba) ++slot;
        if (!table.find(slot)) p.index = slot;
    }
    if (!form.endText.empty()) {
        auto e = parseLba(form.endText, ss);
        if (!e) return fail(e.error());
        p.lastLba = *e;
    } else if (!form.sizeText.empty()) {
        auto n = parseLba(form.sizeText, ss);
        if (!n) return fail(n.error());
        if (*n == 0) return fail(ErrorCategory::InvalidArgument, "size must be positive");
        p.lastLba = p.firstLba + *n - 1;
    } else {
        p.lastLba = target.lastLba >= p.firstLba ? target.lastLba : p.firstLba;
        const Lba alignedEnd = ((p.lastLba + 1) / align) * align - 1;
        if (alignedEnd > p.firstLba) p.lastLba = alignedEnd;
    }
    return m_stack->push(std::make_unique<ops::AddPartition>(p, form.wipeSignatures));
}

Expected<void> EditSession::updatePartition(std::uint32_t index, const EditPartitionForm& form) {
    const probe::Node& tree = preview();
    if (!tree.table) return fail(ErrorCategory::NotFound, "no partition table");
    const auto* existing = tree.table->find(index);
    if (!existing) return fail(ErrorCategory::NotFound, "no partition " + std::to_string(index));
    const std::uint32_t ss = m_device->sectorSize();
    pt::Partition p = *existing;
    if (!form.typeText.empty()) {
        auto type = parseType(tree.table->type(), form.typeText);
        if (!type) return fail(type.error());
        p.type = *type;
    }
    if (form.name) p.name = *form.name;
    if (!form.startText.empty()) {
        auto s = parseLba(form.startText, ss);
        if (!s) return fail(s.error());
        p.firstLba = *s;
    }
    if (!form.endText.empty()) {
        auto e = parseLba(form.endText, ss);
        if (!e) return fail(e.error());
        p.lastLba = *e;
    } else if (!form.sizeText.empty()) {
        auto n = parseLba(form.sizeText, ss);
        if (!n) return fail(n.error());
        if (*n == 0) return fail(ErrorCategory::InvalidArgument, "size must be positive");
        p.lastLba = p.firstLba + *n - 1;
    }
    return m_stack->push(std::make_unique<ops::UpdatePartition>(p));
}

Expected<void> EditSession::deletePartition(std::uint32_t index) { return m_stack->push(std::make_unique<ops::DeletePartition>(index)); }
Expected<void> EditSession::repairTable() { return m_stack->push(std::make_unique<ops::RepairTable>()); }

Expected<void> EditSession::wipeSignatures(std::optional<std::uint32_t> index) {
    Region region{0, m_device->size()};
    if (index) {
        const probe::Node& tree = preview();
        if (!tree.table) return fail(ErrorCategory::NotFound, "no partition table");
        const auto* existing = tree.table->find(*index);
        if (!existing) return fail(ErrorCategory::NotFound, "no partition " + std::to_string(*index));
        region = existing->region(m_device->sectorSize());
    }
    return m_stack->push(std::make_unique<ops::WipeSignatures>(region));
}

Expected<void> EditSession::undoLast() { return m_stack->pop(); }
void EditSession::clear() { m_stack->clear(); }
Expected<ops::OperationStack::ApplyResult> EditSession::apply(Progress& progress) {
    auto r = m_stack->apply(progress);
    if (!r) return r;
    // The kernel keeps its own copy of a real disk's table; ask it to pick ours up, and tell the
    // user what that did (tableRereadNote): on macOS the call does nothing, the kernel re-reads
    // when this session's writable handle closes, so the report must not say it re-read now.
    if (m_source.kind == SourceKind::Disk) {
        auto& platform = platform::current();   // rereadPartitionTable is not const
        const auto note = tableRereadNote(platform.name(), platform.rereadPartitionTable(m_source.path));
        if (note.happened) r->report.addDetail("partition table re-read", note.text);
        else r->report.addLine("note: " + note.text);
    }
    return r;
}
Expected<void> EditSession::refresh() { return m_stack->refresh(); }

std::vector<PendingRow> pendingRows(const ops::OperationStack& stack) {
    std::vector<PendingRow> out;
    int n = 1;
    for (const auto& e : stack.pending()) {
        PendingRow r;
        r.n = n++;
        r.title = e.description;   // says primary or logical itself; the preview's table type is no guide to an earlier op
        for (const auto& j : e.jobTitles) r.detail += (r.detail.empty() ? "" : " · ") + j;
        r.destructive = e.destructive;
        out.push_back(std::move(r));
    }
    return out;
}

namespace {

std::string typeCodeOf(const pt::Partition& p) {
    const auto* info = pt::types::find(p.type);
    if (p.type.scheme == pt::TableType::Gpt && info && info->sgdiskCode[0]) return info->sgdiskCode;
    return p.type.code();
}

// Which base partition each preview partition continues, preview index to base index. GPT and APM
// entries and MBR primaries keep their slot and pair by index. MBR numbers logicals by their place
// in the EBR chain on every read, so adding or deleting one in front renumbers those behind it: a
// logical pairs with the base logical its data overlaps (a resize keeps most of it), else with the
// one that kept its index (a move to a disjoint place), else it is new.
std::map<std::uint32_t, std::uint32_t> pairPartitions(const probe::Node& base, const probe::Node& preview) {
    std::map<std::uint32_t, std::uint32_t> pairs;
    if (!base.table || !preview.table) return pairs;
    std::set<std::uint32_t> taken;
    std::vector<const pt::Partition*> logicals;
    for (const auto& p : preview.table->partitions()) {
        if (p.isLogical) {
            logicals.push_back(&p);
            continue;
        }
        if (const auto* b = base.table->find(p.index); b && !b->isLogical) {
            pairs[p.index] = b->index;
            taken.insert(b->index);
        }
    }
    for (const auto* p : logicals) {
        const pt::Partition* best = nullptr;
        SectorCount overlap = 0;
        for (const auto& b : base.table->partitions()) {
            if (!b.isLogical || taken.count(b.index) || !b.overlaps(*p)) continue;
            const SectorCount o = std::min(b.lastLba, p->lastLba) - std::max(b.firstLba, p->firstLba) + 1;
            if (o > overlap) {
                best = &b;
                overlap = o;
            }
        }
        if (best) {
            pairs[p->index] = best->index;
            taken.insert(best->index);
        }
    }
    for (const auto* p : logicals) {
        if (pairs.count(p->index)) continue;
        if (const auto* b = base.table->find(p->index); b && b->isLogical && !taken.count(b->index)) {
            pairs[p->index] = b->index;
            taken.insert(b->index);
        }
    }
    return pairs;
}

// Colours per partition on either side: base partitions in base order (the order segments()
// colours the base bar in), a preview partition in its base partner's colour, a new one continuing
// the sequence. Keyed per side because a renumbered logical has a different index on each.
struct Colours {
    std::map<std::uint32_t, int> base, preview;
};
Colours colourMap(const probe::Node& base, const probe::Node& preview, const std::map<std::uint32_t, std::uint32_t>& pairs) {
    Colours c;
    int next = 0;
    for (const auto& n : base.children)
        if (n.kind == probe::NodeKind::Partition && n.partition && !n.partition->isExtended) c.base[n.partition->index] = next++;
    for (const auto& n : preview.children) {
        if (n.kind != probe::NodeKind::Partition || !n.partition || n.partition->isExtended) continue;
        const auto pair = pairs.find(n.partition->index);
        if (pair != pairs.end() && c.base.count(pair->second)) c.preview[n.partition->index] = c.base.at(pair->second);
        else c.preview[n.partition->index] = next++;
    }
    return c;
}

} // namespace

std::vector<Segment> baseSegments(const probe::Node& base) { return segments(base); }

std::vector<Segment> previewSegments(const probe::Node& base, const probe::Node& preview) {
    auto segs = segments(preview);
    const auto colours = colourMap(base, preview, pairPartitions(base, preview));
    for (auto& s : segs) {
        const probe::Node* n = nodeAt(preview, s.path);
        if (!n || !n->partition) continue;
        if (const auto it = colours.preview.find(n->partition->index); it != colours.preview.end()) s.colorIndex = it->second;
    }
    return segs;
}

std::vector<PreviewRow> previewRows(const probe::Node& base, const probe::Node& preview) {
    std::vector<PreviewRow> out;
    const auto pairs = pairPartitions(base, preview);
    const auto colours = colourMap(base, preview, pairs);
    std::set<std::uint32_t> kept;   // base indices some preview partition continues
    for (std::size_t i = 0; i < preview.children.size(); ++i) {
        const auto& c = preview.children[i];
        if (c.kind != probe::NodeKind::Partition && c.kind != probe::NodeKind::Free) continue;
        PreviewRow r;
        r.path = {static_cast<int>(i)};
        if (c.kind == probe::NodeKind::Free) {
            r.name = "Free space";
            r.typeCode = "—";
            r.sizeText = sizeBinaryOrDecimal(c.region.length);
            r.isFree = true;
            out.push_back(std::move(r));
            continue;
        }
        if (!c.partition) continue;
        const auto& p = *c.partition;
        r.colorIndex = colours.preview.count(p.index) ? colours.preview.at(p.index) : -1;
        r.index = static_cast<int>(p.index);
        r.typeCode = typeCodeOf(p);
        r.name = displayName(c);
        r.sizeText = sizeBinaryOrDecimal(c.region.length);
        const auto pair = pairs.find(p.index);
        const pt::Partition* partner = pair != pairs.end() && base.table ? base.table->find(pair->second) : nullptr;
        if (!partner) {
            r.change = "new";
            r.changed = true;
        } else {
            const auto& b = *partner;
            kept.insert(b.index);
            std::vector<std::string> changes;
            if (b.name != p.name) {
                changes.push_back("rename");
                r.name = (b.name.empty() ? std::string("(unnamed)") : b.name) + " → " + (p.name.empty() ? std::string("(unnamed)") : p.name);
            }
            if (b.type != p.type) {
                changes.push_back("type");
                r.typeCode = typeCodeOf(b) + " → " + typeCodeOf(p);
            }
            if (b.firstLba != p.firstLba) changes.push_back("move");
            if (b.sectors() != p.sectors()) {
                changes.push_back(p.sectors() < b.sectors() ? "shrink" : "grow");
                const std::uint32_t ss = preview.table ? preview.table->geometry().logicalSectorSize : 512;
                r.sizeText = sizeBinaryOrDecimal(b.sectors() * ss) + " → " + sizeBinaryOrDecimal(p.sectors() * ss);
            }
            for (const auto& ch : changes) r.change += (r.change.empty() ? "" : " · ") + ch;
            r.changed = !changes.empty();
        }
        out.push_back(std::move(r));
    }
    // Deleted partitions stay visible, struck through.
    if (base.table) {
        const std::uint32_t ss = base.table->geometry().logicalSectorSize;
        for (const auto& c : base.children) {
            if (c.kind != probe::NodeKind::Partition || !c.partition || kept.count(c.partition->index)) continue;
            PreviewRow r;
            r.name = displayName(c);
            r.typeCode = typeCodeOf(*c.partition);
            r.sizeText = sizeBinaryOrDecimal(c.partition->sectors() * ss);
            r.change = "delete";
            r.index = static_cast<int>(c.partition->index);
            r.changed = r.deleted = true;
            r.colorIndex = colours.base.count(c.partition->index) ? colours.base.at(c.partition->index) : -1;
            out.push_back(std::move(r));
        }
    }
    return out;
}

std::string changedRangesText(const ops::OperationStack& stack) {
    std::string s;
    for (const auto& r : stack.changedRegions()) s += (s.empty() ? "" : " · ") + hexRangeText(r);
    return s.empty() ? "nothing yet" : s;
}

std::string stackSummary(const ops::OperationStack& stack) {
    const auto n = stack.pending().size();
    if (n == 0) return "nothing pending";
    std::size_t destructive = 0;
    for (const auto& e : stack.pending())
        if (e.destructive) ++destructive;
    std::string s = std::to_string(n) + " pending";
    if (destructive) s += " · " + std::to_string(destructive) + " destructive";
    return s;
}

std::vector<PartitionTypeChoice> partitionTypeChoices(pt::TableType scheme) {
    std::vector<PartitionTypeChoice> out;
    for (const auto& info : pt::types::all()) {
        if (info.type.scheme != scheme) continue;
        PartitionTypeChoice c;
        c.code = scheme == pt::TableType::Gpt && info.sgdiskCode[0] ? info.sgdiskCode : info.type.code();
        c.name = info.name;
        c.group = info.group;
        out.push_back(std::move(c));
    }
    std::stable_sort(out.begin(), out.end(), [](const PartitionTypeChoice& a, const PartitionTypeChoice& b) {
        if (a.group != b.group) return a.group < b.group;
        return a.name < b.name;
    });
    return out;
}

TableRereadNote tableRereadNote(std::string_view platformName, const Expected<void>& reread) {
    if (!reread) return {false, "the OS did not re-read the partition table: " + reread.error().message() + "; reconnect the disk or reboot before mounting"};
    if (platformName == "macos") return {false, "macOS re-reads the partition table when the editor closes the disk; until then the OS's partition list lags behind what is on it"};
    return {true, "asked the OS"};
}

} // namespace drstein::core
