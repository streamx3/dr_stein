// SPDX-License-Identifier: MIT
#include "drstein/core/opened.hpp"

#include "stein/image/operations.hpp"
#include "stein/image/stein_format.hpp"

namespace drstein::core {

using namespace stein;

namespace {
void wipeBytes(void* p, std::size_t n) {
    volatile unsigned char* v = static_cast<volatile unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) v[i] = 0;
}
} // namespace

void wipeMasterKey(std::vector<std::uint8_t>& key) {
    wipeBytes(key.data(), key.size());
    key.clear();
    key.shrink_to_fit();
}

void wipeMasterKeys(MasterKeys& keys) {
    for (auto& [uuid, key] : keys) wipeMasterKey(key);
    keys.clear();
}

Expected<std::shared_ptr<BlockDevice>> openDevice(const SourceDescriptor& source, const OpenOptions& options, std::vector<std::string>* notes) {
    const auto mode = options.writable ? platform::OpenMode::ReadWrite : platform::OpenMode::ReadOnly;
    if (source.kind == SourceKind::Disk) return platform::current().open(source.path, mode);

    const std::filesystem::path path = source.path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return fail(ErrorCategory::NotFound, path.string() + " is not a regular file");

    auto fmt = image::detectVdiskFormat(path);
    if (!fmt) return fail(fmt.error());
    if (*fmt == image::VdiskFormat::Stein) {
        if (options.writable) return fail(ErrorCategory::Permission, "stein images are read-only in this version");
        // Every known passphrase gets a try; only a Permission answer (wrong or missing key) moves on
        // to the next one. An unencrypted image opens with the first attempt whatever it carries.
        const auto& list = options.imagePassphrases.empty() ? options.passphrases : options.imagePassphrases;
        if (list.empty()) return image::openImage(path, {});
        Expected<std::shared_ptr<BlockDevice>> last = fail(ErrorCategory::Permission, "no passphrase opens this image");
        for (const auto& pass : list) {
            last = image::openImage(path, pass);
            if (last || last.error().category() != ErrorCategory::Permission) return last;
        }
        return last;
    }
    if (*fmt != image::VdiskFormat::Raw) {
        if (options.writable)
            return fail(ErrorCategory::Permission, path.filename().string() + " is a " + std::string(image::toString(*fmt)) + " container; Dr Stein opens those read-only");
        image::VdiskInfo vi;
        auto dev = image::openVdisk(path, &vi);
        if (!dev) return dev;
        if (notes) notes->insert(notes->end(), vi.notes.begin(), vi.notes.end());
        return dev;
    }
    if (image::findSplitRaw(path)) return image::openSplitRaw(path, options.writable, options.fileSectorSize);
    return platform::openAny(path.string(), mode, options.fileSectorSize);
}

Expected<probe::Node> reprobe(const std::shared_ptr<BlockDevice>& device, const OpenOptions& options) {
    probe::Options po;
    po.passphrases = options.passphrases;
    po.pim = options.pim;
    po.tcrypt = options.tcrypt;
    po.masterKeys = options.masterKeys;
    po.onUnlocked = options.onUnlocked;
    po.cancelled = options.cancelled;
    auto tree = probe::probe(device, po);
    // The probe's Options took copies of the secrets; the caller keeps the originals and decides their end.
    wipeMasterKeys(po.masterKeys);
    for (auto& p : po.passphrases) wipeBytes(p.data(), p.size());
    return tree;
}

Expected<OpenedSource> openSource(const SourceDescriptor& source, const OpenOptions& options) {
    OpenedSource opened;
    opened.descriptor = source;
    auto dev = openDevice(source, options, &opened.notes);
    if (!dev) return fail(dev.error());
    opened.device = *dev;
    auto tree = reprobe(opened.device, options);
    if (!tree) return fail(tree.error());
    opened.tree = std::move(*tree);
    return opened;
}

} // namespace drstein::core
