// SPDX-License-Identifier: MIT
// The Hex view: one on-disk structure at a time, its bytes next to its parsed
// fields, edits previewed on an overlay until written. Built on the
// layout::Node trees that PartitionTable::describe() and FileSystem::describe()
// return.
#pragma once

#include "drstein/core/topology.hpp"
#include "stein/block/block_device.hpp"
#include "stein/block/overlay_device.hpp"
#include "stein/layout/node.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace drstein::core {

enum class StructSource { Table, Content };

struct StructRef {
    std::string id;                               // "table/primary header", "content/boot sector"
    std::string label;                            // "GPT header (primary)"
    std::string where;                            // "LBA 1 · byte 0x200 · 512 B shown · 92 B used"
    StructSource source = StructSource::Table;
    std::shared_ptr<stein::BlockDevice> device;   // the device the offsets are relative to
    stein::ByteCount offset = 0;                  // struct start on `device`
    stein::ByteCount displayBase = 0;             // added to offsets for display (root-absolute when mappable)
    stein::ByteCount shownOffset = 0;             // sector-aligned start of the bytes shown
    stein::ByteCount shownSize = 0;               // whole sectors covering the struct
    stein::layout::Node node;                     // the parsed tree (offsets relative to `device`)
    std::string validity;                         // "all 14 fields valid · CRCs match" / "2 fields invalid"
};

// Every structure selectable for a node: the table's pieces for the device
// node (and a nested table), the filesystem's or container's for content nodes.
std::vector<StructRef> structuresFor(const stein::probe::Node& root, const NodePath& path);
// Re-describe a device's structures (used after an edit on an overlay).
stein::Expected<std::vector<StructRef>> describeDevice(std::shared_ptr<stein::BlockDevice> device, StructSource source,
                                                       stein::ByteCount displayBase);

struct FieldRow {
    std::string name, typeName, value, pretty, doc, message;
    stein::ByteCount offset = 0;                  // on the struct's device
    std::uint32_t size = 0;
    stein::layout::FieldType type = stein::layout::FieldType::Bytes;
    stein::layout::Validity validity = stein::layout::Validity::Ok;
    int depth = 0;
    bool isStruct = false;
    bool editable = false;
};
std::vector<FieldRow> fieldRows(const StructRef& s);
// Innermost non-struct field covering the offset, if any.
std::optional<std::size_t> fieldAt(const std::vector<FieldRow>& rows, stein::ByteCount offset);
std::string validitySummary(const stein::layout::Node& node);

// Bytes and edits of one structure on a copy-on-write overlay.
class StructEditor {
public:
    StructEditor(std::shared_ptr<stein::BlockDevice> device, StructRef ref);

    stein::Expected<void> load();
    const StructRef& current() const { return m_ref; }
    std::span<const std::byte> bytes() const { return m_bytes; }
    std::span<const std::byte> original() const { return m_original; }
    bool isDirty(stein::ByteCount offset) const;          // offset on the device
    std::size_t dirtyBytes() const;
    std::vector<stein::Region> dirtyRegions() const;       // coalesced, byte-precise

    // Parse `text` the way the field's type wants and write it into the overlay;
    // integers accept decimal or 0x hex, ascii is padded, bytes are hex pairs.
    stein::Expected<void> setField(const FieldRow& field, std::string_view text);
    stein::Expected<void> setByte(stein::ByteCount offset, std::uint8_t value);
    void revert();

    // Write every dirty sector to `target` (the same device opened writable).
    stein::Expected<void> commitTo(stein::BlockDevice& target);
    // The shown bytes as a STEINSPARSE1 piece, restorable with restoreFrom().
    stein::Expected<void> saveTo(const std::filesystem::path& file) const;
    stein::Expected<void> restoreFrom(const std::filesystem::path& file);

private:
    stein::Expected<void> refresh();   // re-describe from the overlay

    std::shared_ptr<stein::BlockDevice> m_device;
    std::shared_ptr<stein::OverlayDevice> m_overlay;
    StructRef m_ref;
    std::vector<std::byte> m_bytes, m_original;
};

// Parsing helpers exposed for tests.
stein::Expected<std::vector<std::byte>> encodeFieldValue(const FieldRow& field, std::span<const std::byte> currentBytes, std::string_view text);

} // namespace drstein::core
