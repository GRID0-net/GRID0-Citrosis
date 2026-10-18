// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Standalone so the memory policies can be checked without Android or renderer dependencies.
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "common/literals.h"
#include "common/memory_budget.h"
#include "shader_recompiler/object_pool.h"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

struct Object {
    explicit Object(int value_) : value{value_}, allocation{std::make_unique<int[]>(32)} {
        if (value < 0) {
            throw value;
        }
        ++live;
    }
    ~Object() {
        --live;
    }
    static inline int live{};
    int value;
    std::unique_ptr<int[]> allocation;
};

void CheckPool() {
    Shader::ObjectPool<Object> pool{4};
    const auto initial_bytes = pool.CapacityBytes();
    try {
        (void)pool.Create(-1);
        Check(false, "Expected the constructor to throw");
    } catch (int) {
        pool.ReleaseContents();
        Check(Object::live == 0, "A failed construction was counted as a live object");
    }
    auto* first = pool.Create(42);
    for (int i = 0; i < 100; ++i) {
        (void)pool.Create(i);
    }
    Check(first->value == 42, "Growing the pool invalidated an existing object");
    Check(Object::live == 101, "Incorrect live object count after growth");
    pool.ReleaseContents(initial_bytes * 2);
    Check(Object::live == 0, "Trimming failed to destroy every live object");
    Check(pool.CapacityBytes() == initial_bytes, "Oversized storage survived the retention cap");

    // Exercise the old merged-root path, then trim that already enlarged root.
    for (int i = 0; i < 17; ++i) {
        (void)pool.Create(i);
    }
    pool.ReleaseContents();
    Check(Object::live == 0, "Unbounded release leaked objects");
    Check(pool.CapacityBytes() > initial_bytes, "Unbounded reuse unexpectedly lost capacity");
    pool.ReleaseContents(initial_bytes);
    Check(pool.CapacityBytes() == initial_bytes, "Previously merged root was not trimmed");

    // Small shaders reuse allocations, including a partially occupied root.
    for (int pass = 0; pass < 100; ++pass) {
        Check(pool.Create(pass)->value == pass, "Pool could not be reused after trimming");
        pool.ReleaseContents(initial_bytes);
        Check(Object::live == 0, "Repeated reuse leaked objects");
        Check(pool.CapacityBytes() == initial_bytes, "Small shader storage was not retained");
    }
    (void)pool.Create(1);
}

void CheckBudgets() {
    using namespace Common::Literals;
    using namespace Common::MemoryBudget;
    SetPhysicalMemory(0);
    Check(!GetLimits(), "Desktop unexpectedly has a mobile budget");
    for (const u64 ram : {4_GiB, 6_GiB, 7233_MiB, 12_GiB, 16_GiB}) {
        SetPhysicalMemory(ram);
        SetPressure(Pressure::None);
        const auto normal = *GetLimits();
        SetPressure(Pressure::Moderate);
        const auto moderate = *GetLimits();
        SetPressure(Pressure::Critical);
        const auto critical = *GetLimits();
        Check(normal.texture_expected < normal.texture_critical, "Texture thresholds inverted");
        Check(normal.buffer_expected < normal.buffer_critical, "Buffer thresholds inverted");
        Check(critical.texture_critical < moderate.texture_critical &&
                  moderate.texture_critical < normal.texture_critical,
              "Texture budget did not decrease with pressure");
        Check(critical.buffer_critical < moderate.buffer_critical &&
                  moderate.buffer_critical < normal.buffer_critical,
              "Buffer budget did not decrease with pressure");
        Check(critical.staging_expected >= 16_MiB, "Staging target smaller than upload ring");
    }
    SetPressure(Pressure::None);
    SetPhysicalMemory(0);
}
} // namespace

int main() {
    CheckPool();
    Check(Object::live == 0, "Pool destructor leaked objects");
    CheckBudgets();
    std::puts("Memory regression checks passed");
}
