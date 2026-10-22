// SPDX-License-Identifier: MIT
// Opening a source as a block device and probing it. The same resolution
// the CLI does: disks through the platform, .stein / qcow2 / VHD / VHDX /
// VMDK / VDI / E01 / DMG through the image readers, split raw sets, raw files.
#pragma once

#include "drstein/core/source.hpp"
#include "stein/block/block_device.hpp"
#include "stein/probe/topology.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace drstein::core {

// Master keys by LUKS UUID (LuksInfo::uuid, what blkid prints): what a session keeps from an unlock so
// the next probe of the same container skips the key derivation.
using MasterKeys = std::map<std::string, std::vector<std::uint8_t>>;

struct OpenOptions {
    bool writable = false;                   // probing and browsing never need this
    std::vector<std::string> passphrases;    // LUKS slots and VeraCrypt trial decryption (also tried on .stein images when imagePassphrases is empty)
    std::vector<std::string> imagePassphrases;   // the encrypted .stein file itself, tried in turn; kept apart so an image passphrase is not run through Argon2 on every LUKS node
    std::uint32_t pim = 0;
    // VeraCrypt/TrueCrypt have no signature: trial-decrypting every unrecognised region costs minutes
    // of PBKDF2 per probe, so it is off unless the user asks for one probe ("Try VeraCrypt…").
    bool tcrypt = false;
    // Keys of containers unlocked earlier in the session: a re-probe (hot-plug refresh, a repair, a Hex
    // write) opens the payload with the key instead of paying the KDF again. A key that no longer matches
    // its header is skipped in silence and the passphrases are tried. The caller owns the bytes and their
    // zeroing; openSource/reprobe wipe the copies they make.
    MasterKeys masterKeys;
    // Called on the probing thread for every container a passphrase opened (not a cached key), with its
    // UUID and master key, so the session can cache it. The probe keeps no copy.
    std::function<void(const std::string& uuid, const std::vector<std::uint8_t>& masterKey)> onUnlocked;
    // Polled between KDF slices, key slots and passphrases. Once it returns true the container is left as
    // found (no Decrypted child, no luks.locked note) and the probe finishes the rest of the device, so a
    // job's Cancel ends a slow unlock without losing the view of the disk.
    std::function<bool()> cancelled;
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

// A master key leaves memory overwritten, not merely freed: a released heap block keeps its bytes until
// something reuses it. Stores go through a volatile pointer so the compiler cannot drop them as dead.
void wipeMasterKey(std::vector<std::uint8_t>& key);
void wipeMasterKeys(MasterKeys& keys);

} // namespace drstein::core
