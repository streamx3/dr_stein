// SPDX-License-Identifier: MIT
// The Image view: create / restore / verify forms validated into library
// options, and the jobs that run them with a Report the card can show.
#pragma once

#include "drstein/core/opened.hpp"
#include "drstein/core/topology.hpp"
#include "stein/core/progress.hpp"
#include "stein/core/report.hpp"
#include "stein/core/json.hpp"
#include "stein/image/operations.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace drstein::core {

// "4 MiB", "1M", "2G", "512k": bytes. Fails on garbage.
stein::Expected<stein::ByteCount> parseSize(std::string_view text);
bool looksRaw(const std::filesystem::path& destination);   // .img / .raw / .dd

struct CreateImageForm {
    NodePath sourcePath;                        // {}: the whole device; a partition node: just that partition
    std::filesystem::path destination;
    bool raw = false;                           // raw dd-style file instead of .stein
    stein::image::Compression compression = stein::image::Compression::Lz4;
    std::string chunkSizeText = "4 MiB";
    std::string splitSizeText;                  // empty: single file
    stein::image::BadSectorPolicy badSectors = stein::image::BadSectorPolicy::SkipZero;
    bool usedBlocksOnly = true;
    bool verifyAfter = true;
    bool encrypt = false;
    std::string passphrase;
    std::string notes;
};

struct CreatePlan {
    stein::image::CreateOptions options;
    std::shared_ptr<stein::BlockDevice> device; // what gets read: the device or the partition's slice
    std::string scopeText;                      // "whole device" / "partition 2 · Data · 64 MiB"
    bool partition = false;
    bool raw = false;
    bool verifyAfter = false;
    std::filesystem::path destination;
    std::vector<std::string> warnings;          // "destination exists and will be replaced"
};
// The rows the "Source" picker offers: the device first, then each partition.
struct SourceScope {
    NodePath path;
    std::string label;                          // "Whole device · 500.1 GB" / "Partition 2 · Data · FAT32 · 64 MiB"
    bool partition = false;
};
std::vector<SourceScope> sourceScopes(const OpenedSource& source);
// Provenance of a partition image, as stored in the manifest's "source" object.
struct PartitionProvenance {
    std::uint32_t index = 0;
    stein::Lba firstLba = 0, lastLba = 0;
    std::uint32_t sectorSize = 512;
    std::string type, typeName, name, uuid;
    std::string parentName, parentIdentity, parentPath, parentTable;
    stein::ByteCount parentSize = 0;
    std::string text() const;                   // "partition 2 \"Data\" of Samsung SSD 980 · LBA 2 048 – 1 050 623 · 0700"
};
stein::json::Value provenanceJson(const PartitionProvenance& p);
std::optional<PartitionProvenance> provenanceFromSource(const stein::json::Value& source);
stein::Expected<CreatePlan> validate(const CreateImageForm& form, const OpenedSource& source);
// "free space of EFI (FAT32), root (ext4) and Data (NTFS) becomes implicit zero chunks"
std::string usedBlocksNote(const stein::probe::Node& tree);

struct CreateOutcome {
    stein::image::CreateResult result;
    stein::image::CopyStats rawStats;           // when raw
    bool verified = false;
    stein::image::VerifyResult verify;
};
stein::Expected<CreateOutcome> runCreate(const OpenedSource& source, const CreatePlan& plan, stein::Progress& progress, stein::Report& report);

struct RestoreForm {
    std::filesystem::path image;
    bool verifyFirst = true;
    bool allowSmaller = false;
    bool skipZeroChunks = false;   // leave the target untouched where the image is all-zero (fast; the target keeps old bytes there)
    std::string passphrase;
};
// What an image expects to be restored onto. Whole-device images go to devices, partition
// images to a partition of matching size; mixing the two is refused unless `force`.
struct RestoreScope {
    bool partition = false;
    bool encrypted = false;      // .stein with a key area
    bool unlocked = true;        // false: encrypted and the passphrase was missing or wrong
    std::optional<PartitionProvenance> provenance;
    stein::ByteCount size = 0;
};
// True when `passphrase` opens the image's key area (always true for unencrypted images).
stein::Expected<bool> passphraseOpens(const std::filesystem::path& image, const std::string& passphrase);
stein::Expected<RestoreScope> restoreScopeOf(const std::filesystem::path& image, const std::string& passphrase = {});
stein::Expected<stein::image::RestoreResult> runRestore(const RestoreForm& form, stein::BlockDevice& target, stein::Progress& progress, stein::Report& report);

struct VerifyForm {
    std::filesystem::path image;
    int level = 3;                              // 1 structure, 2 stored CRCs, 3 full content
    std::string passphrase;
};
stein::Expected<stein::image::VerifyResult> runVerify(const VerifyForm& form, stein::Progress& progress, stein::Report& report);
std::string verifyText(const stein::image::VerifyResult& v);   // one sentence

struct KeySlotRow {
    int id = 0;
    std::string label, kdfText;
};
stein::Expected<std::vector<KeySlotRow>> listKeys(const std::filesystem::path& image);
stein::Expected<int> addKey(const std::filesystem::path& image, const std::string& current, const std::string& fresh, const std::string& label);
stein::Expected<void> removeKey(const std::filesystem::path& image, const std::string& current, int id);

} // namespace drstein::core
