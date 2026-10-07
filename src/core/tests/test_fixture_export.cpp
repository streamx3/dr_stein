// SPDX-License-Identifier: MIT
// Not a test of anything: with DRSTEIN_EXPORT_COMPOSITE=<file> set, writes the
// composite GPT disk (ext4 + FAT32 from libstein's fixtures) to that file so
// the application can be driven by hand or by screenshots without root.
#include "fixtures.hpp"

#include <doctest.h>

#include <cstdlib>

TEST_CASE("export the composite disk when asked") {
    const char* out = std::getenv("DRSTEIN_EXPORT_COMPOSITE");
    if (!out || !*out) return;
    auto disk = drstein::test::compositeDisk();
    drstein::test::writeDevice(*disk.device, out);
    MESSAGE("wrote " << out);
}
