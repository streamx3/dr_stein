// SPDX-License-Identifier: MIT
// Test fixtures: libstein's checked-in sparse images, and composite disks
// built in memory from them: a GPT one (ext4 in partition 1, FAT32 in
// partition 2, a 1 MiB gap and some free tail) and an MBR one (FAT16, exFAT,
// an extended container with an ext4 logical), optionally written to a file.
#pragma once

#include "stein/block/memory_device.hpp"
#include "stein/block/sparse_file.hpp"
#include "stein/ops/stack.hpp"
#include "stein/core/progress.hpp"
#include "stein/pt/mbr_table.hpp"

#include <doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace drstein::test {

inline std::shared_ptr<stein::MemoryDevice> loadFixture(const std::string& relativePath) {
    auto dev = stein::SparseFile::loadIntoMemory(std::string(STEIN_FIXTURE_DIR) + "/" + relativePath);
    REQUIRE_MESSAGE(dev.has_value(), relativePath);
    return *dev;
}

struct CompositeDisk {
    std::shared_ptr<stein::MemoryDevice> device;
    stein::ByteCount part1Offset = 0, part1Size = 0, part2Offset = 0, part2Size = 0;
};

inline CompositeDisk compositeDisk() {
    using namespace stein;
    auto ext = loadFixture("extfs/ext4.sparse");
    auto fat = loadFixture("fatfs/fat32.sparse");
    CompositeDisk d;
    d.part1Offset = 1 * MiB;
    d.part1Size = alignUp(ext->size(), MiB);
    d.part2Offset = d.part1Offset + d.part1Size + 1 * MiB;   // a 1 MiB free gap between them
    d.part2Size = alignUp(fat->size(), MiB);
    const ByteCount total = d.part2Offset + d.part2Size + 3 * MiB;
    d.device = std::make_shared<MemoryDevice>(total, 512);

    ops::OperationStack stack(d.device);
    REQUIRE(stack.push(std::make_unique<ops::CreateTable>(pt::TableType::Gpt)));
    pt::Partition p1;
    p1.firstLba = d.part1Offset / 512;
    p1.lastLba = (d.part1Offset + d.part1Size) / 512 - 1;
    p1.type = *pt::types::fromSgdiskCode("8300");
    p1.name = "root";
    REQUIRE(stack.push(std::make_unique<ops::AddPartition>(p1, false)));
    pt::Partition p2;
    p2.firstLba = d.part2Offset / 512;
    p2.lastLba = (d.part2Offset + d.part2Size) / 512 - 1;
    p2.type = *pt::types::fromSgdiskCode("0700");
    p2.name = "Data";
    REQUIRE(stack.push(std::make_unique<ops::AddPartition>(p2, false)));
    NullProgressSink sink;
    Progress progress(sink);
    auto applied = stack.apply(progress);
    REQUIRE(applied);
    REQUIRE(applied->postconditionOk);
    REQUIRE(d.device->writeAt(d.part1Offset, ext->bytes()));
    REQUIRE(d.device->writeAt(d.part2Offset, fat->bytes()));
    return d;
}

// An MBR disk, 1 MiB aligned: FAT16 in primary 1, exFAT in primary 2, a free MiB, then the
// extended container (3) whose first logical (5) carries ext4. The logical's data starts one
// MiB into the container because its EBR is the container's first sector; the container keeps
// 8 MiB free after it for another logical, and 2 MiB stay free after the container for a primary.
struct MbrCompositeDisk {
    std::shared_ptr<stein::MemoryDevice> device;
    stein::ByteCount fatOffset = 0, fatSize = 0, exfatOffset = 0, exfatSize = 0;
    stein::ByteCount extendedOffset = 0, extendedSize = 0, logicalOffset = 0, logicalSize = 0;
};

inline MbrCompositeDisk mbrComposite() {
    using namespace stein;
    auto fat = loadFixture("fatfs/fat16.sparse");
    auto exfat = loadFixture("exfatfs/exfat.sparse");
    auto ext = loadFixture("extfs/ext4.sparse");
    MbrCompositeDisk d;
    d.fatOffset = 1 * MiB;
    d.fatSize = alignUp(fat->size(), MiB);
    d.exfatOffset = d.fatOffset + d.fatSize;
    d.exfatSize = alignUp(exfat->size(), MiB);
    d.extendedOffset = d.exfatOffset + d.exfatSize + 1 * MiB;
    d.logicalOffset = d.extendedOffset + 1 * MiB;
    d.logicalSize = alignUp(ext->size(), MiB);
    d.extendedSize = 1 * MiB + d.logicalSize + 8 * MiB;
    const ByteCount total = d.extendedOffset + d.extendedSize + 2 * MiB;
    d.device = std::make_shared<MemoryDevice>(total, 512);

    auto table = pt::MbrTable::createEmpty(d.device->geometry());
    table->setDiskSignature(0x5EC7A11E);
    auto add = [&](ByteCount offset, ByteCount size, std::uint8_t type, bool logical) {
        pt::Partition p;
        p.firstLba = offset / 512;
        p.lastLba = (offset + size) / 512 - 1;
        p.type = pt::PartitionType::mbr(type);
        p.isLogical = logical;
        REQUIRE(table->addPartition(p));
    };
    add(d.fatOffset, d.fatSize, 0x06, false);
    add(d.exfatOffset, d.exfatSize, 0x07, false);
    add(d.extendedOffset, d.extendedSize, 0x05, false);
    add(d.logicalOffset, d.logicalSize, 0x83, true);
    REQUIRE(table->write(*d.device));
    REQUIRE(d.device->writeAt(d.fatOffset, fat->bytes()));
    REQUIRE(d.device->writeAt(d.exfatOffset, exfat->bytes()));
    REQUIRE(d.device->writeAt(d.logicalOffset, ext->bytes()));
    return d;
}

// A fresh temporary directory per test, removed at the end.
class TempDir {
public:
    TempDir() {
        std::error_code ec;
        const auto base = std::filesystem::temp_directory_path(ec);
        for (int n = 0;; ++n) {
            m_path = base / ("drstein-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000) + "-" + std::to_string(n));
            if (std::filesystem::create_directory(m_path, ec)) break;
        }
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    const std::filesystem::path& path() const { return m_path; }
    std::filesystem::path file(const std::string& name) const { return m_path / name; }

private:
    std::filesystem::path m_path;
};

inline std::filesystem::path writeDevice(const stein::MemoryDevice& device, const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const auto bytes = device.bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
    return file;
}

} // namespace drstein::test
