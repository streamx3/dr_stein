// SPDX-License-Identifier: MIT
#include "drstein/core/structs.hpp"

#include "drstein/core/format.hpp"
#include "stein/block/sparse_file.hpp"
#include "stein/core/endian.hpp"
#include "stein/core/guid.hpp"
#include "stein/core/strings.hpp"
#include "stein/fs/filesystem.hpp"
#include "stein/pt/partition_table.hpp"

#include <algorithm>
#include <cstdio>

namespace drstein::core {

using namespace stein;
using layout::FieldType;
using layout::Validity;

namespace {

std::uint64_t loadLe(std::span<const std::byte> b) {
    std::uint64_t v = 0;
    for (std::size_t i = b.size(); i-- > 0;) v = (v << 8) | std::to_integer<std::uint64_t>(b[i]);
    return v;
}
std::uint64_t loadBe(std::span<const std::byte> b) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < b.size(); ++i) v = (v << 8) | std::to_integer<std::uint64_t>(b[i]);
    return v;
}
void storeLe(std::span<std::byte> b, std::uint64_t v) {
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
}
void storeBe(std::span<std::byte> b, std::uint64_t v) {
    for (std::size_t i = 0; i < b.size(); ++i) b[b.size() - 1 - i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
}

bool isInteger(FieldType t) {
    switch (t) {
    case FieldType::U8: case FieldType::U16: case FieldType::U32: case FieldType::U64:
    case FieldType::I8: case FieldType::I16: case FieldType::I32: case FieldType::I64:
    case FieldType::Bits: case FieldType::Crc32: case FieldType::Lba:
        return true;
    default:
        return false;
    }
}

bool isSigned(FieldType t) {
    return t == FieldType::I8 || t == FieldType::I16 || t == FieldType::I32 || t == FieldType::I64;
}

// A layout::Node does not record endianness; the decoded value text does. Whichever
// byte order reproduces the printed value is the one the format uses.
bool fieldIsBigEndian(const FieldRow& f, std::span<const std::byte> current) {
    if (current.size() > 8) return false;
    const std::uint64_t le = loadLe(current), be = loadBe(current);
    if (le == be) return false;
    std::uint64_t printed = 0;
    bool parsed = false;
    const std::string v = f.value;
    if (!v.empty()) {
        char* end = nullptr;
        if (v.starts_with("0x") || v.starts_with("0X")) {
            printed = std::strtoull(v.c_str() + 2, &end, 16);
            parsed = end && *end == 0;
        } else if (f.type == FieldType::Crc32) {
            printed = std::strtoull(v.c_str(), &end, 16);
            parsed = end && *end == 0;
        } else {
            if (isSigned(f.type)) printed = static_cast<std::uint64_t>(std::strtoll(v.c_str(), &end, 10));
            else printed = std::strtoull(v.c_str(), &end, 10);
            parsed = end && *end == 0;
        }
    }
    if (!parsed) return false;
    const std::uint64_t mask = current.size() == 8 ? ~0ull : ((1ull << (8 * current.size())) - 1);
    return (printed & mask) == be && (printed & mask) != le;
}

void shiftOffsets(layout::Node& n, ByteCount delta) {
    n.absOffset += delta;
    for (auto& c : n.children) shiftOffsets(c, delta);
}

std::string whereText(const StructRef& s) {
    const std::uint32_t ss = s.device ? s.device->sectorSize() : 512;
    char hex[32];
    std::snprintf(hex, sizeof hex, "0x%llX", static_cast<unsigned long long>(s.displayBase + s.offset));
    std::string w = "LBA " + grouped((s.displayBase + s.offset) / ss) + " · byte " + hex + " · " + sizeBinary(s.shownSize) + " shown";
    if (s.node.size && s.node.size != s.shownSize) w += " · " + sizeBinary(s.node.size) + " used";
    return w;
}

void countFields(const layout::Node& n, int& total, int& bad) {
    for (const auto& c : n.children) {
        if (c.isStruct) {
            countFields(c, total, bad);
            continue;
        }
        ++total;
        if (c.validity >= Validity::Warning) ++bad;
    }
}

std::vector<StructRef> refsFromTree(const layout::Node& tree, StructSource source, std::shared_ptr<BlockDevice> device, ByteCount displayBase,
                                    const std::string& prefix) {
    std::vector<StructRef> out;
    const std::uint32_t ss = device ? device->sectorSize() : 512;
    auto make = [&](const layout::Node& n) {
        StructRef r;
        r.id = prefix + "/" + n.name;
        r.label = n.name;
        r.source = source;
        r.device = device;
        r.offset = n.absOffset;
        r.displayBase = displayBase;
        r.shownOffset = alignDown(n.absOffset, ss);
        const ByteCount end = alignUp(std::max<ByteCount>(n.absOffset + n.size, n.absOffset + 1), ss);
        r.shownSize = end - r.shownOffset;
        r.node = n;
        r.validity = validitySummary(n);
        r.where = whereText(r);
        return r;
    };
    bool anyStruct = false;
    for (const auto& c : tree.children)
        if (c.isStruct && c.size) {
            anyStruct = true;
            out.push_back(make(c));
        }
    if (!anyStruct && tree.size) out.push_back(make(tree));
    return out;
}

} // namespace

std::string validitySummary(const layout::Node& node) {
    int total = 0, bad = 0;
    countFields(node, total, bad);
    if (bad == 0) return "all " + std::to_string(total) + " fields valid" + (node.message.empty() ? "" : " · " + node.message);
    return std::to_string(bad) + " of " + std::to_string(total) + " fields flagged" + (node.message.empty() ? "" : " · " + node.message);
}

Expected<std::vector<StructRef>> describeDevice(std::shared_ptr<BlockDevice> device, StructSource source, ByteCount displayBase) {
    if (!device) return fail(ErrorCategory::InvalidArgument, "no device");
    if (source == StructSource::Table) {
        auto table = pt::PartitionTable::read(device);
        if (!table) return fail(table.error());
        return refsFromTree((*table)->describe(), source, device, displayBase, "table");
    }
    auto probed = fs::probe(device);
    if (!probed) return fail(probed.error());
    if (!*probed) return fail(ErrorCategory::NotFound, "no recognised filesystem or container here");
    return refsFromTree((*probed)->describe(), source, device, displayBase, "content");
}

std::vector<StructRef> structuresFor(const probe::Node& root, const NodePath& path) {
    std::vector<StructRef> out;
    const probe::Node* node = nodeAt(root, path);
    if (!node || !node->device) return out;
    // Offsets of a slice map onto the root device; a decrypted or assembled device does not.
    ByteCount displayBase = 0;
    bool mappable = true;
    {
        const probe::Node* n = &root;
        for (int i : path) {
            n = &n->children[static_cast<std::size_t>(i)];
            if (n->kind == probe::NodeKind::Decrypted || n->kind == probe::NodeKind::Volume) mappable = false;
        }
    }
    if (mappable) displayBase = node->region.offset;
    if (node->table) {
        auto refs = refsFromTree(node->table->describe(), StructSource::Table, node->device, displayBase, "table");
        out.insert(out.end(), refs.begin(), refs.end());
    }
    if (node->content) {
        auto refs = refsFromTree(node->content->describe(), StructSource::Content, node->device, displayBase, "content");
        out.insert(out.end(), refs.begin(), refs.end());
    }
    return out;
}

namespace {

bool editableType(const layout::Node& n) {
    if (n.isStruct || n.size == 0 || n.size > 64) return false;
    switch (n.type) {
    case FieldType::Bytes: return n.size <= 16;
    default: return true;
    }
}

void flatten(const layout::Node& n, int depth, std::vector<FieldRow>& out) {
    for (const auto& c : n.children) {
        FieldRow r;
        r.name = c.name;
        r.typeName = c.isStruct ? "struct" : std::string(layout::toString(c.type));
        r.value = c.isStruct ? "{ … }" : c.value;
        r.pretty = c.pretty;
        r.doc = c.doc;
        r.message = c.message;
        r.offset = c.absOffset;
        r.size = static_cast<std::uint32_t>(c.size);
        r.type = c.type;
        r.validity = c.validity;
        r.depth = depth;
        r.isStruct = c.isStruct;
        r.editable = editableType(c);
        out.push_back(std::move(r));
        if (c.isStruct) flatten(c, depth + 1, out);
    }
}

} // namespace

std::vector<FieldRow> fieldRows(const StructRef& s) {
    std::vector<FieldRow> out;
    flatten(s.node, 0, out);
    return out;
}

std::optional<std::size_t> fieldAt(const std::vector<FieldRow>& rows, ByteCount offset) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (r.isStruct || r.size == 0) continue;
        if (offset < r.offset || offset >= r.offset + r.size) continue;
        if (!best || r.size < rows[*best].size) best = i;
    }
    return best;
}

Expected<std::vector<std::byte>> encodeFieldValue(const FieldRow& f, std::span<const std::byte> current, std::string_view textIn) {
    const std::string text(trim(textIn));
    std::vector<std::byte> out(current.begin(), current.end());
    if (out.size() != f.size) out.assign(f.size, std::byte{0});
    if (isInteger(f.type)) {
        if (text.empty()) return fail(ErrorCategory::InvalidArgument, "empty value");
        char* end = nullptr;
        std::uint64_t v = 0;
        const bool hexPrefixed = text.starts_with("0x") || text.starts_with("0X");
        if (hexPrefixed) v = std::strtoull(text.c_str() + 2, &end, 16);
        else if (f.type == FieldType::Crc32) v = std::strtoull(text.c_str(), &end, 16);
        else if (isSigned(f.type)) v = static_cast<std::uint64_t>(std::strtoll(text.c_str(), &end, 10));
        else v = std::strtoull(text.c_str(), &end, 10);
        if (!end || *end != 0) return fail(ErrorCategory::InvalidArgument, "not a number: " + text);
        if (f.size < 8 && !isSigned(f.type) && (v >> (8 * f.size)) != 0) return fail(ErrorCategory::InvalidArgument, text + " does not fit in " + std::to_string(f.size) + " bytes");
        if (fieldIsBigEndian(f, current)) storeBe(out, v);
        else storeLe(out, v);
        return out;
    }
    switch (f.type) {
    case FieldType::Ascii: {
        if (text.size() > f.size) return fail(ErrorCategory::InvalidArgument, "longer than " + std::to_string(f.size) + " bytes");
        // Keep the field's padding convention: spaces if it was space padded, NUL otherwise.
        std::byte pad{0};
        if (!current.empty() && current.back() == std::byte{' '}) pad = std::byte{' '};
        std::fill(out.begin(), out.end(), pad);
        std::copy(text.begin(), text.end(), reinterpret_cast<char*>(out.data()));
        return out;
    }
    case FieldType::Utf16le: {
        if (!utf8ToUtf16le(text, out)) return fail(ErrorCategory::InvalidArgument, "name does not fit or is not valid UTF-8");
        return out;
    }
    case FieldType::Guid:
    case FieldType::Uuid: {
        auto u = Uuid::parse(text);
        if (!u || f.size != 16) return fail(ErrorCategory::InvalidArgument, "not a GUID: " + text);
        std::span<std::byte, 16> dst(out.data(), 16);
        if (f.type == FieldType::Guid) u->toGptBytes(dst);
        else u->toRfcBytes(dst);
        return out;
    }
    case FieldType::Bytes:
    case FieldType::Chs: {
        std::string hex;
        for (char c : text)
            if (c != ' ' && c != ':' && c != '-') hex += c;
        auto bytes = fromHex(hex);
        if (!bytes || bytes->size() != f.size) return fail(ErrorCategory::InvalidArgument, "expected " + std::to_string(f.size) + " hex bytes");
        return *bytes;
    }
    default:
        return fail(ErrorCategory::Unsupported, "this field type cannot be edited as text");
    }
}

// ---- StructEditor -------------------------------------------------------------

StructEditor::StructEditor(std::shared_ptr<BlockDevice> device, StructRef ref)
    : m_device(std::move(device)), m_overlay(OverlayDevice::create(m_device, 4096)), m_ref(std::move(ref)) {}

Expected<void> StructEditor::load() {
    m_bytes.assign(m_ref.shownSize, std::byte{0});
    if (auto r = m_overlay->readAt(m_ref.shownOffset, m_bytes); !r) return r;
    m_original = m_bytes;
    return {};
}

bool StructEditor::isDirty(ByteCount offset) const {
    if (offset < m_ref.shownOffset || offset >= m_ref.shownOffset + m_bytes.size()) return false;
    const auto i = static_cast<std::size_t>(offset - m_ref.shownOffset);
    return i < m_original.size() && m_bytes[i] != m_original[i];
}

std::size_t StructEditor::dirtyBytes() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < m_bytes.size() && i < m_original.size(); ++i)
        if (m_bytes[i] != m_original[i]) ++n;
    return n;
}

std::vector<Region> StructEditor::dirtyRegions() const {
    std::vector<Region> out;
    for (std::size_t i = 0; i < m_bytes.size() && i < m_original.size(); ++i) {
        if (m_bytes[i] == m_original[i]) continue;
        const ByteCount off = m_ref.shownOffset + i;
        if (!out.empty() && out.back().end() == off) out.back().length++;
        else out.push_back({off, 1});
    }
    return out;
}

Expected<void> StructEditor::setField(const FieldRow& field, std::string_view text) {
    if (field.offset < m_ref.shownOffset || field.offset + field.size > m_ref.shownOffset + m_bytes.size())
        return fail(ErrorCategory::OutOfRange, "field lies outside the shown bytes");
    const auto start = static_cast<std::size_t>(field.offset - m_ref.shownOffset);
    std::span<const std::byte> current(m_bytes.data() + start, field.size);
    auto encoded = encodeFieldValue(field, current, text);
    if (!encoded) return fail(encoded.error());
    if (auto r = m_overlay->writeAt(field.offset, *encoded); !r) return r;
    std::copy(encoded->begin(), encoded->end(), m_bytes.begin() + static_cast<std::ptrdiff_t>(start));
    return refresh();
}

Expected<void> StructEditor::setByte(ByteCount offset, std::uint8_t value) {
    if (offset < m_ref.shownOffset || offset >= m_ref.shownOffset + m_bytes.size()) return fail(ErrorCategory::OutOfRange, "outside the shown bytes");
    const std::byte b{value};
    if (auto r = m_overlay->writeAt(offset, std::span<const std::byte>(&b, 1)); !r) return r;
    m_bytes[static_cast<std::size_t>(offset - m_ref.shownOffset)] = b;
    return refresh();
}

void StructEditor::revert() {
    m_overlay->discardChanges();
    m_bytes = m_original;
    (void)refresh();
}

Expected<void> StructEditor::refresh() {
    auto refs = describeDevice(m_overlay, m_ref.source, m_ref.displayBase);
    if (!refs) {
        // The edit made the structure unrecognisable; keep the bytes, flag the tree.
        m_ref.node.flag(Validity::Error, refs.error().message());
        m_ref.validity = "structure no longer parses: " + refs.error().message();
        return {};
    }
    for (auto& r : *refs)
        if (r.id == m_ref.id) {
            m_ref.node = std::move(r.node);
            m_ref.validity = r.validity;
            return {};
        }
    m_ref.validity = "structure no longer found after the edit";
    return {};
}

Expected<void> StructEditor::commitTo(BlockDevice& target) {
    if (target.isReadOnly()) return fail(ErrorCategory::Permission, "the device is opened read-only");
    const std::uint32_t ss = target.sectorSize();
    // Write whole sectors that contain a change, from the edited buffer.
    for (ByteCount off = m_ref.shownOffset; off < m_ref.shownOffset + m_bytes.size(); off += ss) {
        bool changed = false;
        for (ByteCount b = off; b < off + ss && b < m_ref.shownOffset + m_bytes.size(); ++b)
            if (isDirty(b)) {
                changed = true;
                break;
            }
        if (!changed) continue;
        const auto start = static_cast<std::size_t>(off - m_ref.shownOffset);
        const std::size_t len = std::min<std::size_t>(ss, m_bytes.size() - start);
        if (auto r = target.writeAt(off, std::span<const std::byte>(m_bytes.data() + start, len)); !r) return r;
    }
    if (auto r = target.flush(); !r) return r;
    m_original = m_bytes;
    m_overlay->discardChanges();
    return {};
}

Expected<void> StructEditor::saveTo(const std::filesystem::path& file) const {
    SparseFile::Contents c;
    c.totalSize = m_device->size();
    c.sectorSize = m_device->sectorSize();
    c.runs.push_back({m_ref.shownOffset, m_bytes});
    return SparseFile::write(file, c);
}

Expected<void> StructEditor::restoreFrom(const std::filesystem::path& file) {
    auto piece = SparseFile::read(file);
    if (!piece) return fail(piece.error());
    bool any = false;
    for (const auto& run : piece->runs) {
        const Region r{run.offset, run.bytes.size()};
        const Region shown{m_ref.shownOffset, m_bytes.size()};
        if (!r.overlaps(shown)) continue;
        const ByteCount from = std::max(r.offset, shown.offset), to = std::min(r.end(), shown.end());
        for (ByteCount b = from; b < to; ++b) m_bytes[static_cast<std::size_t>(b - shown.offset)] = run.bytes[static_cast<std::size_t>(b - r.offset)];
        if (auto w = m_overlay->writeAt(from, std::span<const std::byte>(m_bytes.data() + (from - shown.offset), to - from)); !w) return w;
        any = true;
    }
    if (!any) return fail(ErrorCategory::InvalidArgument, "the piece holds no bytes inside this structure");
    return refresh();
}

} // namespace drstein::core
