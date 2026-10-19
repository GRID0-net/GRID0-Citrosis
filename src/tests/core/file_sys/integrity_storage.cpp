// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/file_sys/fssystem/fssystem_integrity_verification_storage.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace {
void Initialize(FileSys::IntegrityVerificationStorage& storage) {
    auto hashes = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>(32));
    auto data = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>(37, 0x42));
    storage.Initialize(hashes, data, 64, 32, true);
}
}

TEST_CASE("RomFS padding stays inside the requested read", "[filesystem][romfs]") {
    FileSys::IntegrityVerificationStorage storage;
    Initialize(storage);
    // Extra physical space lets the regression detect an overwrite safely.
    std::array<u8, 128> bytes;
    bytes.fill(0xA5);
    const auto count = storage.Read(bytes.data() + 16, 8, 33);
    CHECK(std::all_of(bytes.begin(), bytes.begin() + 16, [](u8 x) { return x == 0xA5; }));
    CHECK(std::all_of(bytes.begin() + 24, bytes.end(), [](u8 x) { return x == 0xA5; }));
    CHECK(std::all_of(bytes.begin() + 16, bytes.begin() + 20, [](u8 x) { return x == 0x42; }));
    CHECK(std::all_of(bytes.begin() + 20, bytes.begin() + 24, [](u8 x) { return x == 0; }));
    CHECK(count == 8);
}

TEST_CASE("RomFS final block accepts partial and padding-only reads", "[filesystem][romfs]") {
    FileSys::IntegrityVerificationStorage storage;
    Initialize(storage);
    std::array<u8, 64> bytes;
    bytes.fill(0xA5);
    SECTION("inside real data") {
        REQUIRE(storage.Read(bytes.data(), 8, 0) == 8);
        CHECK(std::all_of(bytes.begin(), bytes.begin() + 8, [](u8 x) { return x == 0x42; }));
    }
    SECTION("remainder of a final block") {
        REQUIRE(storage.Read(bytes.data(), 28, 36) == 28);
        CHECK(bytes[0] == 0x42);
        CHECK(std::all_of(bytes.begin() + 1, bytes.begin() + 28, [](u8 x) { return x == 0; }));
    }
    SECTION("starts exactly at EOF") {
        REQUIRE(storage.Read(bytes.data(), 8, 37) == 8);
        CHECK(std::all_of(bytes.begin(), bytes.begin() + 8, [](u8 x) { return x == 0; }));
    }
    SECTION("starts in padding") {
        REQUIRE(storage.Read(bytes.data(), 8, 48) == 8);
        CHECK(std::all_of(bytes.begin(), bytes.begin() + 8, [](u8 x) { return x == 0; }));
    }
    SECTION("zero bytes need no buffer") {
        CHECK(storage.Read(nullptr, 0, 64) == 0);
    }
}

TEST_CASE("RomFS rejects reads beyond the final block without touching memory", "[filesystem][romfs]") {
    FileSys::IntegrityVerificationStorage storage;
    Initialize(storage);
    std::array<u8, 64> bytes;
    bytes.fill(0xA5);
    CHECK(storage.Read(bytes.data(), 8, 60) == 0);
    CHECK(storage.Read(bytes.data(), 1, 65) == 0);
    CHECK(storage.Read(bytes.data(), 8, std::numeric_limits<size_t>::max() - 3) == 0);
    CHECK(std::all_of(bytes.begin(), bytes.end(), [](u8 x) { return x == 0xA5; }));
}
