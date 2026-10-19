// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/div_ceil.h"
#include "common/assert.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace VideoCommon {

template <typename T>
class DescriptorTable {
public:
    [[nodiscard]] bool Synchronize(GPUVAddr gpu_addr, u32 limit) noexcept {
        const bool ret = descriptors.empty() || current_gpu_addr != gpu_addr || current_limit != limit;
        if (ret) {
            Refresh(gpu_addr, limit);
        }
        return ret;
    }

    void Invalidate() noexcept {
        std::ranges::fill(read_descriptors, 0);
    }

    template <typename Memory>
    [[nodiscard]] std::pair<T, bool> Read(const Memory& gpu_memory, u32 index) noexcept {
        DEBUG_ASSERT(index <= current_limit);
        const GPUVAddr gpu_addr = current_gpu_addr + index * sizeof(T);
        std::pair<T, bool> result;
        gpu_memory.ReadBlockUnsafe(gpu_addr, std::addressof(result.first), sizeof(T));
        if ((read_descriptors[index / 64] & (1ULL << (index % 64))) != 0) {
            result.second = result.first != descriptors[index];
        } else {
            read_descriptors[index / 64] |= 1ULL << (index % 64);
            result.second = true;
        }
        if (result.second) {
            descriptors[index] = result.first;
        }
        return result;
    }

    void Refresh(GPUVAddr gpu_addr, u32 limit) noexcept {
        current_gpu_addr = gpu_addr;
        current_limit = limit;
        // Keep capacity across table switches, but grow geometrically from the
        // actual inclusive limit rather than allocating 524289 entries per table.
        const size_t required = static_cast<size_t>(limit) + 1;
        if (required > descriptors.size()) {
            const size_t count = std::bit_ceil(std::max(required, size_t{256}));
            descriptors.resize(count);
            read_descriptors.resize(Common::DivCeil(count, size_t{64}));
        }
        Invalidate();
    }

    std::vector<u64> read_descriptors;
    std::vector<T> descriptors;
    GPUVAddr current_gpu_addr{};
    u32 current_limit{};
};

} // namespace VideoCommon
