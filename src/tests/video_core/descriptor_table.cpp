// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "video_core/texture_cache/descriptor_table.h"

namespace {
struct Memory {
    std::array<u64, 1024> values{};
    void ReadBlockUnsafe(GPUVAddr address, void* out, size_t size) const {
        std::memcpy(out, reinterpret_cast<const u8*>(values.data()) + address, size);
    }
};
}
TEST_CASE("Descriptor tables grow with demand and observe guest edits", "[video_core][descriptors]") {
    VideoCommon::DescriptorTable<u64> table;
    Memory memory;
    memory.values[0] = 42;
    REQUIRE(table.Synchronize(0, 0));
    REQUIRE(table.descriptors.size() <= 256);
    CHECK(table.Read(memory, 0) == std::pair<u64, bool>{42, true});
    CHECK(table.Read(memory, 0) == std::pair<u64, bool>{42, false});
    memory.values[0] = 43;
    CHECK(table.Read(memory, 0) == std::pair<u64, bool>{43, true});
    CHECK_FALSE(table.Synchronize(0, 0));
    table.Invalidate();
    CHECK(table.Read(memory, 0).second);
    REQUIRE(table.Synchronize(0, 512));
    REQUIRE(table.descriptors.size() >= 513);
    CHECK(table.descriptors.size() <= 1024);
    memory.values[512] = 71;
    CHECK(table.Read(memory, 512) == std::pair<u64, bool>{71, true});
    const auto* allocation = table.descriptors.data();
    REQUIRE(table.Synchronize(0, 10));
    REQUIRE(table.Synchronize(0, 512));
    CHECK(table.descriptors.data() == allocation);
    CHECK(table.Read(memory, 512).second);
    REQUIRE(table.Synchronize(sizeof(u64), 10));
    memory.values[1] = 90;
    CHECK(table.Read(memory, 0) == std::pair<u64, bool>{90, true});
}
