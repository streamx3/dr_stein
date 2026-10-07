// SPDX-License-Identifier: MIT
#include "drstein/core/partition_edit.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/imaging.hpp"
#include "stein/core/guid.hpp"
#include "stein/core/strings.hpp"

#include <algorithm>
#include <map>

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
    // Default placement: the largest free region, start aligned to 1 MiB (what sgdisk does).
    const SectorCount align = std::max<SectorCount>(1, (1 * MiB) / ss);
    auto free = table.freeRegions(align);
    if (free.empty() && form.startText.empty()) return fail(ErrorCategory::OutOfRange, "no free space in the table");
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
        p.firstLba = ((target.firstLba + align - 1) / align) * align;
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
Expected<ops::OperationStack::ApplyResult> EditSession::apply(Progress& progress) { return m_stack->apply(progress); }
Expected<void> EditSession::refresh() { return m_stack->refresh(); }

std::vector<PendingRow> pendingRows(const ops::OperationStack& stack) {
    std::vector<PendingRow> out;
    int n = 1;
    for (const auto& e : stack.pending()) {
        PendingRow r;
        r.n = n++;
        r.title = e.description;
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

// Colour per partition index, in base order; new indices continue the sequence.
std::map<std::uint32_t, int> colourMap(const probe::Node& base, const probe::Node& preview) {
    std::map<std::uint32_t, int> colours;
    int next = 0;
    for (const auto& c : base.children)
        if (c.kind == probe::NodeKind::Partition && c.partition && !c.partition->isExtended) colours[c.partition->index] = next++;
    for (const auto& c : preview.children)
        if (c.kind == probe::NodeKind::Partition && c.partition && !c.partition->isExtended && !colours.count(c.partition->index)) colours[c.partition->index] = next++;
    return colours;
}

} // namespace

std::vector<Segment> baseSegments(const probe::Node& base) { return segments(base); }

std::vector<Segment> previewSegments(const probe::Node& base, const probe::Node& preview) {
    auto segs = segments(preview);
    const auto colours = colourMap(base, preview);
    for (auto& s : segs) {
        const probe::Node* n = nodeAt(preview, s.path);
        if (n && n->partition) s.colorIndex = colours.at(n->partition->index);
    }
    return segs;
}

std::vector<PreviewRow> previewRows(const probe::Node& base, const probe::Node& preview) {
    std::vector<PreviewRow> out;
    const auto colours = colourMap(base, preview);
    std::map<std::uint32_t, const pt::Partition*> before;
    if (base.table)
        for (const auto& p : base.table->partitions()) before[p.index] = &p;
    std::map<std::uint32_t, bool> seen;
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
        seen[p.index] = true;
        r.colorIndex = colours.count(p.index) ? colours.at(p.index) : -1;
        r.typeCode = typeCodeOf(p);
        r.name = displayName(c);
        r.sizeText = sizeBinaryOrDecimal(c.region.length);
        auto it = before.find(p.index);
        if (it == before.end()) {
            r.change = "new";
            r.changed = true;
        } else {
            const auto& b = *it->second;
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
            if (c.kind != probe::NodeKind::Partition || !c.partition || seen.count(c.partition->index)) continue;
            PreviewRow r;
            r.name = displayName(c);
            r.typeCode = typeCodeOf(*c.partition);
            r.sizeText = sizeBinaryOrDecimal(c.partition->sectors() * ss);
            r.change = "delete";
            r.changed = r.deleted = true;
            r.colorIndex = colours.count(c.partition->index) ? colours.at(c.partition->index) : -1;
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

} // namespace drstein::core
