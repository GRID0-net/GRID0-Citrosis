// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <chrono>
#include <cstdio>
#include <vector>
#include "video_core/texture_cache/descriptor_table.h"

using Descriptor = std::array<u64, 4>;
using Current = VideoCommon::DescriptorTable<Descriptor>;
// Previous production allocation policy, retained here for a reproducible comparison.
struct Previous {
    std::vector<u64> read_descriptors;
    std::vector<Descriptor> descriptors;
    void Refresh(GPUVAddr, u32 limit) {
        const size_t count = ((limit + 0x80000) & (~0x7ffff)) + 1;
        const auto old_size = read_descriptors.size();
        read_descriptors.resize(Common::DivCeil(count, size_t{64}));
        std::fill_n(read_descriptors.begin(), std::min(old_size, read_descriptors.size()), 0);
        descriptors.resize(count);
    }
};
volatile size_t observed;
template <typename Table>
double Run(bool fresh) {
    constexpr u32 iterations = 1000;
    Table retained;
    const auto start = std::chrono::steady_clock::now();
    for (u32 i = 0; i < (fresh ? 32 : iterations); ++i) {
        if (fresh) {
            Table table;
            table.Refresh(i * 32, 127 + (i & 63));
            observed = table.descriptors.size() + table.descriptors.back()[0];
        } else {
            retained.Refresh(i * 32, 127 + (i & 63));
            observed = retained.descriptors.size() + retained.descriptors.back()[0];
        }
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
int main() {
    Previous previous; Current current;
    previous.Refresh(0, 127); current.Refresh(0, 127);
    std::printf("32-byte descriptors, limit=127: previous=%zu bytes, current=%zu bytes\n",
                previous.descriptors.size()*sizeof(Descriptor) + previous.read_descriptors.size()*sizeof(u64),
                current.descriptors.size()*sizeof(Descriptor) + current.read_descriptors.size()*sizeof(u64));
    for (bool fresh : {true, false}) {
        // Run twice so the second line can be compared after allocator warmup.
        for (int trial = 0; trial < 2; ++trial)
            std::printf("%s trial %d: previous=%.3f ms current=%.3f ms\n", fresh ? "32 initializations" : "1000 switches",
                        trial, Run<Previous>(fresh), Run<Current>(fresh));
    }
}
