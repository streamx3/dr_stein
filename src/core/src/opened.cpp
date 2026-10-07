// SPDX-License-Identifier: MIT
#include "drstein/core/opened.hpp"

#include "stein/image/operations.hpp"
#include "stein/image/stein_format.hpp"

namespace drstein::core {

using namespace stein;

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
        return image::openImage(path, options.passphrases.empty() ? std::string{} : options.passphrases.front());
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
    return probe::probe(device, po);
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
