// SPDX-License-Identifier: MIT
// The Partitions view: an ops::OperationStack with the UI's defaults around
// it (placement, type parsing), the pending list, the preview diff and the
// change summary. Nothing is written before apply().
#pragma once

#include "drstein/core/opened.hpp"
#include "drstein/core/topology.hpp"
#include "stein/ops/stack.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace drstein::core {

struct AddPartitionForm {
    std::string startText;            // "2048s" sectors or "1 MiB"; empty: first aligned LBA of the largest free region
    std::string sizeText;             // "512 MiB"; empty: to the end of that region
    std::string endText;              // "1050623s"; wins over size
    std::string typeText;             // "EF00", "8300", a GUID, "0x83", "Apple_HFS"; empty: Linux filesystem
    std::string name;
    std::optional<std::uint32_t> index;
    bool wipeSignatures = true;
};

struct EditPartitionForm {            // empty fields keep their value
    std::string startText, sizeText, endText, typeText;
    std::optional<std::string> name;
};

stein::Expected<stein::Lba> parseLba(std::string_view text, std::uint32_t sectorSize);
stein::Expected<stein::pt::PartitionType> parseType(stein::pt::TableType scheme, std::string_view text);

class EditSession {
public:
    static stein::Expected<EditSession> open(const SourceDescriptor& source, const OpenOptions& options);
    EditSession(EditSession&&) noexcept = default;
    EditSession& operator=(EditSession&&) noexcept = default;

    const stein::probe::Node& base() const { return m_stack->base(); }
    const stein::probe::Node& preview() const { return m_stack->preview(); }
    stein::ops::OperationStack& stack() { return *m_stack; }
    const stein::ops::OperationStack& stack() const { return *m_stack; }
    const std::shared_ptr<stein::BlockDevice>& device() const { return m_device; }
    bool isRealDevice() const { return m_source.kind == SourceKind::Disk; }

    stein::Expected<void> createTable(stein::pt::TableType type);
    stein::Expected<void> addPartition(const AddPartitionForm& form);
    stein::Expected<void> updatePartition(std::uint32_t index, const EditPartitionForm& form);
    stein::Expected<void> deletePartition(std::uint32_t index);
    stein::Expected<void> repairTable();
    stein::Expected<void> wipeSignatures(std::optional<std::uint32_t> index);
    stein::Expected<void> undoLast();
    void clear();
    stein::Expected<stein::ops::OperationStack::ApplyResult> apply(stein::Progress& progress);
    stein::Expected<void> refresh();

private:
    EditSession() = default;
    SourceDescriptor m_source;
    std::shared_ptr<stein::BlockDevice> m_device;
    std::unique_ptr<stein::ops::OperationStack> m_stack;
};

struct PendingRow {
    int n = 0;
    std::string title, detail;
    bool destructive = false;
};
std::vector<PendingRow> pendingRows(const stein::ops::OperationStack& stack);

struct PreviewRow {
    NodePath path;                    // in the preview tree ({} for a deleted partition)
    std::string name, typeCode, sizeText, change;   // change: "", "new", "rename", "shrink", "grow", "move", "type", "delete"
    int colorIndex = -1;              // stable across base and preview; -1 for free space
    int index = -1;                   // partition index; -1 for free space
    bool isFree = false, changed = false, deleted = false;
};
std::vector<PreviewRow> previewRows(const stein::probe::Node& base, const stein::probe::Node& preview);
// Segments for the two bars, with colours consistent between them.
std::vector<Segment> baseSegments(const stein::probe::Node& base);
std::vector<Segment> previewSegments(const stein::probe::Node& base, const stein::probe::Node& preview);
std::string changedRangesText(const stein::ops::OperationStack& stack);   // "0x200–0x400 · 0x400–0x4400"
std::string stackSummary(const stein::ops::OperationStack& stack);        // "3 pending · 1 destructive"

struct PartitionTypeChoice {
    std::string code;                 // "EF00", "0x83", "Apple_HFS"
    std::string name;                 // "EFI System"
    std::string group;                // "Linux"
};
std::vector<PartitionTypeChoice> partitionTypeChoices(stein::pt::TableType scheme);

} // namespace drstein::core
