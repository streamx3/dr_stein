// SPDX-License-Identifier: MIT
#include "drstein/core/imaging.hpp"

#include "drstein/core/format.hpp"
#include "drstein/core/topology.hpp"
#include "stein/pt/partition_table.hpp"
#include "stein/block/file_device.hpp"
#include "stein/core/strings.hpp"
#include "stein/image/vdisk.hpp"

#include <cstdlib>

namespace drstein::core {

using namespace stein;

Expected<ByteCount> parseSize(std::string_view textIn) {
    const std::string text(trim(textIn));
    if (text.empty()) return fail(ErrorCategory::InvalidArgument, "empty size");
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || v < 0) return fail(ErrorCategory::InvalidArgument, "not a size: " + text);
    std::string unit = toLower(trim(std::string_view(end)));
    ByteCount mult = 1;
    if (unit.empty() || unit == "b") mult = 1;
    else if (unit == "k" || unit == "kib" || unit == "kb") mult = KiB;
    else if (unit == "m" || unit == "mib" || unit == "mb") mult = MiB;
    else if (unit == "g" || unit == "gib" || unit == "gb") mult = GiB;
    else if (unit == "t" || unit == "tib" || unit == "tb") mult = TiB;
    else return fail(ErrorCategory::InvalidArgument, "unknown unit in " + text);
    return static_cast<ByteCount>(v * static_cast<double>(mult));
}

bool looksRaw(const std::filesystem::path& destination) {
    const std::string ext = toLower(destination.extension().string());
    return ext == ".img" || ext == ".raw" || ext == ".dd";
}

std::string usedBlocksNote(const probe::Node& tree) {
    std::vector<std::string> names;
    std::vector<const probe::Node*> stack{&tree};
    while (!stack.empty()) {
        const probe::Node* n = stack.back();
        stack.pop_back();
        if (n->content && fs::has(n->content->capabilities(), fs::Capability::UsedBlocks)) {
            const auto& info = n->content->info();
            std::string label = info.label.empty() ? displayName(*n) : info.label;
            if (label.empty()) label = n->name;
            names.push_back(label + " (" + std::string(fs::displayName(info.type)) + ")");
        }
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(&*it);
    }
    if (names.empty()) return "no filesystem here has a readable allocation map; every byte is stored";
    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) list += i + 1 == names.size() ? " and " : ", ";
        list += names[i];
    }
    return "free space of " + list + " becomes implicit zero chunks";
}

std::string PartitionProvenance::text() const {
    std::string s = "partition " + std::to_string(index);
    if (!name.empty()) s += " \"" + name + "\"";
    if (!parentName.empty()) s += " of " + parentName;
    s += " · " + lbaRangeText(firstLba, lastLba, sectorSize);
    if (!typeName.empty()) s += " · " + typeName;
    return s;
}

json::Value provenanceJson(const PartitionProvenance& p) {
    json::Value part;
    part.set("index", static_cast<std::uint64_t>(p.index));
    part.set("first_lba", p.firstLba);
    part.set("last_lba", p.lastLba);
    part.set("sector_size", static_cast<std::uint64_t>(p.sectorSize));
    if (!p.type.empty()) part.set("type", p.type);
    if (!p.typeName.empty()) part.set("type_name", p.typeName);
    if (!p.name.empty()) part.set("name", p.name);
    if (!p.uuid.empty()) part.set("uuid", p.uuid);
    json::Value parent;
    if (!p.parentName.empty()) parent.set("name", p.parentName);
    if (!p.parentIdentity.empty()) parent.set("identity", p.parentIdentity);
    if (!p.parentPath.empty()) parent.set("path", p.parentPath);
    if (!p.parentTable.empty()) parent.set("table", p.parentTable);
    if (p.parentSize) parent.set("size", p.parentSize);
    json::Value extra;
    extra.set("kind", std::string("partition"));
    extra.set("partition", std::move(part));
    extra.set("parent", std::move(parent));
    return extra;
}

std::optional<PartitionProvenance> provenanceFromSource(const json::Value& src) {
    if (src.get("kind").asString() != "partition") return std::nullopt;
    const auto& part = src.get("partition");
    const auto& parent = src.get("parent");
    PartitionProvenance p;
    p.index = static_cast<std::uint32_t>(part.get("index").asUInt());
    p.firstLba = part.get("first_lba").asUInt();
    p.lastLba = part.get("last_lba").asUInt();
    p.sectorSize = static_cast<std::uint32_t>(part.get("sector_size").asUInt(512));
    p.type = part.get("type").asString();
    p.typeName = part.get("type_name").asString();
    p.name = part.get("name").asString();
    p.uuid = part.get("uuid").asString();
    p.parentName = parent.get("name").asString();
    p.parentIdentity = parent.get("identity").asString();
    p.parentPath = parent.get("path").asString();
    p.parentTable = parent.get("table").asString();
    p.parentSize = parent.get("size").asUInt();
    return p;
}

std::vector<SourceScope> sourceScopes(const OpenedSource& source) {
    std::vector<SourceScope> out;
    out.push_back({{}, "Whole device · " + sizeText(source.device->size()), false});
    for (std::size_t i = 0; i < source.tree.children.size(); ++i) {
        const auto& c = source.tree.children[i];
        if (c.kind != probe::NodeKind::Partition || !c.partition || !c.device) continue;
        std::string label = "Partition " + std::to_string(c.partition->index);
        const std::string name = displayName(c);
        if (!name.empty()) label += " · " + name;
        const std::string content = contentChain(c);
        if (!content.empty()) label += " · " + content;
        label += " · " + sizeBinaryOrDecimal(c.region.length);
        out.push_back({{static_cast<int>(i)}, label, true});
    }
    return out;
}

Expected<RestoreScope> restoreScopeOf(const std::filesystem::path& image, const std::string& passphrase) {
    RestoreScope scope;
    auto fmt = image::detectVdiskFormat(image);
    if (!fmt) return fail(fmt.error());
    if (*fmt == image::VdiskFormat::Stein) {
        auto info = image::imageInfo(image, passphrase);
        if (!info) return fail(info.error());
        scope.size = info->header.totalSize;
        scope.encrypted = info->encrypted;
        scope.unlocked = !info->encrypted || info->unlocked;
        if (!scope.unlocked) return scope;   // provenance is in the encrypted manifest
        scope.provenance = provenanceFromSource(info->manifest.get("source"));
        scope.partition = scope.provenance.has_value();
        return scope;
    }
    std::error_code ec;
    if (*fmt == image::VdiskFormat::Raw) {
        if (auto split = image::findSplitRaw(image)) scope.size = split->totalBytes;
        else scope.size = std::filesystem::file_size(image, ec);
        return scope;
    }
    image::VdiskInfo vi;
    auto dev = image::openVdisk(image, &vi);
    if (!dev) return fail(dev.error());
    scope.size = vi.virtualSize;
    return scope;
}

Expected<bool> passphraseOpens(const std::filesystem::path& image, const std::string& passphrase) {
    auto fmt = image::detectVdiskFormat(image);
    if (!fmt) return fail(fmt.error());
    if (*fmt != image::VdiskFormat::Stein) return true;
    auto info = image::imageInfo(image, passphrase);
    if (!info) return fail(info.error());
    return !info->encrypted || info->unlocked;
}

Expected<CreatePlan> validate(const CreateImageForm& form, const OpenedSource& source) {
    CreatePlan plan;
    plan.device = source.device;
    plan.scopeText = "whole device";
    if (!form.sourcePath.empty()) {
        const probe::Node* node = nodeAt(source.tree, form.sourcePath);
        if (!node || node->kind != probe::NodeKind::Partition || !node->partition) return fail(ErrorCategory::InvalidArgument, "the source must be the device or one of its partitions");
        if (!node->device) return fail(ErrorCategory::Unsupported, "this partition cannot be read on its own");
        plan.device = node->device;
        plan.partition = true;
        plan.scopeText = "partition " + std::to_string(node->partition->index) + (displayName(*node).empty() ? "" : " · " + displayName(*node)) + " · " + sizeBinaryOrDecimal(node->region.length);
    }
    if (form.destination.empty()) return fail(ErrorCategory::InvalidArgument, "choose a destination file");
    std::error_code ec;
    const auto parent = form.destination.has_parent_path() ? form.destination.parent_path() : std::filesystem::current_path(ec);
    if (!std::filesystem::is_directory(parent, ec)) return fail(ErrorCategory::NotFound, "the destination folder does not exist: " + parent.string());
    if (std::filesystem::exists(form.destination, ec)) plan.warnings.push_back(form.destination.filename().string() + " exists and will be replaced");
    plan.destination = form.destination;
    plan.raw = form.raw || looksRaw(form.destination);
    plan.verifyAfter = form.verifyAfter && !plan.raw;
    if (form.verifyAfter && plan.raw) plan.warnings.push_back("raw images carry no checksums; verification applies to .stein images only");

    auto& o = plan.options;
    auto chunk = parseSize(form.chunkSizeText);
    if (!chunk) return fail(chunk.error());
    if (*chunk < 64 * KiB || *chunk > 256 * MiB) return fail(ErrorCategory::InvalidArgument, "chunk size must be between 64 KiB and 256 MiB");
    if (*chunk % source.device->sectorSize()) return fail(ErrorCategory::InvalidArgument, "chunk size must be a multiple of the sector size");
    o.chunkSize = static_cast<std::uint32_t>(*chunk);
    o.compression = form.compression;
    if (!trim(form.splitSizeText).empty()) {
        auto split = parseSize(form.splitSizeText);
        if (!split) return fail(split.error());
        if (*split && *split < o.chunkSize) return fail(ErrorCategory::InvalidArgument, "split size must be at least one chunk");
        o.splitSize = *split;
    }
    o.badSectors = form.badSectors;
    o.usedBlocksOnly = form.usedBlocksOnly;
    if (form.encrypt) {
        if (form.passphrase.empty()) return fail(ErrorCategory::InvalidArgument, "an encrypted image needs a passphrase");
        o.passphrase = form.passphrase;
    }
    o.notes = form.notes;
    const std::string parentName = source.descriptor.title.empty() ? source.descriptor.path : source.descriptor.title;
    const std::string parentIdentity = source.descriptor.disk ? source.descriptor.disk->identity() : std::string();
    if (plan.partition) {
        const probe::Node& node = *nodeAt(source.tree, form.sourcePath);
        const std::uint32_t ss = source.device->sectorSize();
        PartitionProvenance p;
        p.index = node.partition->index;
        p.firstLba = node.partition->firstLba;
        p.lastLba = node.partition->lastLba;
        p.sectorSize = ss;
        p.type = node.partition->type.code();
        p.typeName = pt::types::name(node.partition->type);
        p.name = node.partition->name;
        if (!node.partition->uuid.isNil()) p.uuid = node.partition->uuid.toString();
        p.parentName = parentName;
        p.parentIdentity = parentIdentity;
        p.parentPath = source.descriptor.path;
        p.parentTable = source.tree.table ? schemeName(source.tree.table->type()) : "";
        p.parentSize = source.device->size();
        o.sourceName = parentName + " · partition " + std::to_string(p.index) + (p.name.empty() ? "" : " (" + p.name + ")");
        o.sourceIdentity = parentIdentity.empty() ? "" : parentIdentity + "#" + std::to_string(p.index);
        o.sourceExtra = provenanceJson(p);
    } else {
        o.sourceName = parentName;
        o.sourceIdentity = parentIdentity;
        o.sourceExtra.set("kind", std::string("device"));
    }
    if (plan.raw && (form.compression != image::Compression::None || form.usedBlocksOnly || form.encrypt))
        plan.warnings.push_back("a raw image is a plain copy: compression, used-blocks-only and encryption do not apply");
    return plan;
}

namespace {

void addStats(Report& r, const image::CopyStats& s) {
    r.addDetail("read", sizeText(s.bytesRead));
    if (s.bytesWritten) r.addDetail("written", sizeText(s.bytesWritten));
    r.addDetail("chunks", std::to_string(s.chunks) + " (" + std::to_string(s.zeroChunks) + " all-zero)");
    if (s.freeBytesSkipped) r.addDetail("free space skipped", sizeText(s.freeBytesSkipped));
    if (s.unreadableSectors) r.addDetail("unreadable sectors", std::to_string(s.unreadableSectors) + " zero-filled");
}

} // namespace

Expected<CreateOutcome> runCreate(const OpenedSource& source, const CreatePlan& plan, Progress& progress, Report& report) {
    CreateOutcome out;
    report.start();
    if (plan.raw) {
        Report& step = report.addChild("Copy to raw image");
        step.start();
        auto target = FileDevice::create(plan.destination, plan.device->size(), plan.device->sectorSize());
        if (!target) {
            step.addLine(target.error().toString());
            step.finish(ReportStatus::Error);
            report.finish(ReportStatus::Error);
            return fail(target.error());
        }
        image::CopyOptions co;
        co.chunkSize = plan.options.chunkSize;
        co.badSectors = plan.options.badSectors;
        auto r = image::copyDevice(*plan.device, **target, co, progress);
        if (!r) {
            step.addLine(r.error().toString());
            step.finish(ReportStatus::Error);
            report.finish(ReportStatus::Error);
            return fail(r.error());
        }
        out.rawStats = *r;
        addStats(step, *r);
        step.finish(r->unreadableSectors ? ReportStatus::Warning : ReportStatus::Success);
        report.finish(step.status());
        return out;
    }
    Report& step = report.addChild("Create image");
    step.start();
    step.addDetail("destination", plan.destination.string());
    step.addDetail("source", plan.scopeText);
    step.addDetail("compression", std::string(image::toString(plan.options.compression)));
    step.addDetail("chunk", sizeBinary(plan.options.chunkSize));
    if (plan.options.splitSize) step.addDetail("split", sizeBinary(plan.options.splitSize));
    if (plan.options.usedBlocksOnly) step.addDetail("used blocks only", plan.partition ? "free space of the partition's filesystem becomes implicit zero chunks" : usedBlocksNote(source.tree));
    if (!plan.options.passphrase.empty()) step.addDetail("encryption", "ChaCha20-Poly1305 · " + plan.options.kdf.describe());
    auto created = image::createImage(plan.device, plan.destination, plan.options, progress);
    if (!created) {
        step.addLine(created.error().toString());
        step.finish(ReportStatus::Error);
        report.finish(ReportStatus::Error);
        return fail(created.error());
    }
    out.result = *created;
    addStats(step, created->stats);
    step.addDetail("stored", sizeText(created->storedBytes) + " in " + std::to_string(created->files.size()) + (created->files.size() == 1 ? " file" : " files"));
    step.addDetail("uuid", created->imageUuid.toString(false));
    if (!created->imageHashHex.empty()) step.addDetail("sha256", created->imageHashHex);
    for (const auto& n : created->allocationNotes) step.addLine("allocation: " + n);
    step.finish(created->stats.unreadableSectors ? ReportStatus::Warning : ReportStatus::Success);
    ReportStatus overall = step.status();
    if (plan.verifyAfter) {
        Report& v = report.addChild("Verify level 3");
        v.start();
        auto verified = image::verifyImage(plan.destination, 3, progress, plan.options.passphrase);
        if (!verified) {
            v.addLine(verified.error().toString());
            v.finish(ReportStatus::Error);
            report.finish(ReportStatus::Error);
            return fail(verified.error());
        }
        out.verified = true;
        out.verify = *verified;
        v.addLine(verifyText(*verified));
        const bool ok = verified->structureOk && verified->complete && verified->chunksBad == 0 && (!verified->imageHashChecked || verified->imageHashOk);
        v.finish(ok ? ReportStatus::Success : ReportStatus::Error);
        if (!ok) overall = ReportStatus::Error;
    }
    report.finish(overall);
    return out;
}

Expected<image::RestoreResult> runRestore(const RestoreForm& form, BlockDevice& target, Progress& progress, Report& report) {
    report.start();
    Report& step = report.addChild("Restore image");
    step.start();
    step.addDetail("image", form.image.string());
    step.addDetail("target", target.name() + " · " + sizeText(target.size()));
    auto fmt = image::detectVdiskFormat(form.image);
    if (!fmt) {
        step.finish(ReportStatus::Error);
        report.finish(ReportStatus::Error);
        return fail(fmt.error());
    }
    auto finishError = [&](const Error& e) {
        step.addLine(e.toString());
        step.finish(ReportStatus::Error);
        report.finish(ReportStatus::Error);
        return fail(e);
    };
    if (*fmt != image::VdiskFormat::Stein) {
        // Raw, split raw or another container: a device-to-device copy.
        std::shared_ptr<BlockDevice> src;
        if (*fmt == image::VdiskFormat::Raw) {
            auto s = image::findSplitRaw(form.image) ? image::openSplitRaw(form.image, false) : platform::openAny(form.image.string(), platform::OpenMode::ReadOnly);
            if (!s) return finishError(s.error());
            src = *s;
        } else {
            auto s = image::openVdisk(form.image);
            if (!s) return finishError(s.error());
            src = *s;
        }
        if (target.size() < src->size() && !form.allowSmaller)
            return finishError(Error(ErrorCategory::InvalidArgument, "target is smaller than the image (" + sizeText(target.size()) + " < " + sizeText(src->size()) + ")"));
        image::CopyOptions co;
        co.skipZeroChunksOnWrite = form.skipZeroChunks;
        if (target.size() < src->size()) co.limit = target.size();
        step.addDetail("zero ranges", form.skipZeroChunks ? "skipped: the target keeps whatever it held there" : "written");
        auto r = image::copyDevice(*src, target, co, progress);
        if (!r) return finishError(r.error());
        image::RestoreResult out;
        out.stats = *r;
        out.targetLarger = target.size() > src->size();
        out.targetSmaller = target.size() < src->size();
        addStats(step, *r);
        step.finish(ReportStatus::Success);
        report.finish(ReportStatus::Success);
        return out;
    }
    image::RestoreOptions ro;
    ro.verifyPayloadFirst = form.verifyFirst;
    ro.allowSmallerTarget = form.allowSmaller;
    ro.writeZeroChunks = !form.skipZeroChunks;
    step.addDetail("zero ranges", form.skipZeroChunks ? "skipped: the target keeps whatever it held there" : "written");
    auto r = image::restoreImage(form.image, target, ro, progress, form.passphrase);
    if (!r) return finishError(r.error());
    addStats(step, r->stats);
    if (r->targetLarger) step.addLine("the target is larger than the image; the GPT backup may need relocating (Repair table)");
    if (r->targetSmaller) step.addLine("the target was smaller than the image; the image was truncated");
    step.finish(r->targetSmaller ? ReportStatus::Warning : ReportStatus::Success);
    report.finish(step.status());
    return r;
}

std::string verifyText(const image::VerifyResult& v) {
    std::string s = std::string("structure ") + (v.structureOk ? "ok" : "BAD") + ", " + (v.complete ? "complete" : "INCOMPLETE") + ", " +
                    std::to_string(v.chunksStored) + "/" + std::to_string(v.chunksTotal) + " chunks stored, " + std::to_string(v.chunksChecked) + " checked, " +
                    std::to_string(v.chunksBad) + " bad";
    if (v.imageHashChecked) s += std::string(", SHA-256 ") + (v.imageHashOk ? "ok" : "MISMATCH");
    return s;
}

Expected<image::VerifyResult> runVerify(const VerifyForm& form, Progress& progress, Report& report) {
    report.start();
    Report& step = report.addChild("Verify image (level " + std::to_string(form.level) + ")");
    step.start();
    step.addDetail("image", form.image.string());
    auto v = image::verifyImage(form.image, form.level, progress, form.passphrase);
    if (!v) {
        step.addLine(v.error().toString());
        step.finish(ReportStatus::Error);
        report.finish(ReportStatus::Error);
        return fail(v.error());
    }
    step.addLine(verifyText(*v));
    for (auto c : v->badChunks) step.addLine("bad chunk " + std::to_string(c));
    const bool ok = v->structureOk && v->chunksBad == 0 && (!v->imageHashChecked || v->imageHashOk);
    step.finish(!ok ? ReportStatus::Error : v->complete ? ReportStatus::Success : ReportStatus::Warning);
    report.finish(step.status());
    return v;
}

Expected<std::vector<KeySlotRow>> listKeys(const std::filesystem::path& image) {
    auto keys = image::imageKeys(image);
    if (!keys) return fail(keys.error());
    std::vector<KeySlotRow> out;
    for (const auto& s : keys->slots()) out.push_back({s.id, s.label, s.kdf.describe()});
    return out;
}

Expected<int> addKey(const std::filesystem::path& image, const std::string& current, const std::string& fresh, const std::string& label) {
    if (fresh.empty()) return fail(ErrorCategory::InvalidArgument, "empty new passphrase");
    return image::addImageKey(image, current, fresh, KdfParams{}, label);
}

Expected<void> removeKey(const std::filesystem::path& image, const std::string& current, int id) {
    return image::removeImageKey(image, current, id);
}

} // namespace drstein::core
