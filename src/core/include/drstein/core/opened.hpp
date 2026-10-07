// SPDX-License-Identifier: MIT
// Opening a source as a block device and probing it. The same resolution
// the CLI does: disks through the platform, .stein / qcow2 / VHD / VHDX /
// VMDK / VDI / E01 / DMG through the image readers, split raw sets, raw files.
#pragma once

#include "drstein/core/source.hpp"
#include "stein/block/block_device.hpp"
#include "stein/probe/topology.hpp"

#include <memory>
#include <string>
#include <vector>

namespace drstein::core {

struct OpenOptions {
    bool writable = false;                   // probing and browsing never need this
    std::vector<std::string> passphrases;    // LUKS slots and VeraCrypt trial decryption; also unlocks .stein images
    std::uint32_t pim = 0;
    std::uint32_t fileSectorSize = 512;      // raw files have no geometry of their own
};

struct OpenedSource {
    SourceDescriptor descriptor;
    std::shared_ptr<stein::BlockDevice> device;
    stein::probe::Node tree;
    std::vector<std::string> notes;          // container notes worth showing once
};

// Just the device (used by edit sessions and jobs that need a writable device).
stein::Expected<std::shared_ptr<stein::BlockDevice>> openDevice(const SourceDescriptor& source, const OpenOptions& options,
                                                                 std::vector<std::string>* notes = nullptr);
// Device plus topology.
stein::Expected<OpenedSource> openSource(const SourceDescriptor& source, const OpenOptions& options);
// Re-run the probe on an already open device (after unlocking, after a write).
stein::Expected<stein::probe::Node> reprobe(const std::shared_ptr<stein::BlockDevice>& device, const OpenOptions& options);

} // namespace drstein::core
