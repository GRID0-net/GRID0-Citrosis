// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace VideoCommon {
// Keep hot transitions cheap; cold transitions use the global pipeline hash table.
template <typename Pipeline, size_t Capacity = 16>
class PipelineTransitionCache {
    static_assert(Capacity > 0);
public:
    template <typename Predicate>
    Pipeline* Find(Predicate&& matches) {
        const auto end = entries.begin() + count;
        const auto it = std::find_if(entries.begin(), end, matches);
        if (it == end) return nullptr;
        Pipeline* result = *it;
        std::rotate(entries.begin(), it, it + 1);
        return result;
    }
    void Insert(Pipeline* value) {
        const auto end = entries.begin() + count;
        const auto it = std::find(entries.begin(), end, value);
        if (it != end) {
            std::rotate(entries.begin(), it, it + 1);
            return;
        }
        const size_t moved = std::min(count, Capacity - 1);
        std::move_backward(entries.begin(), entries.begin() + moved,
                           entries.begin() + moved + 1);
        entries[0] = value;
        count = std::min(count + 1, Capacity);
    }
    size_t Size() const { return count; }
private:
    std::array<Pipeline*, Capacity> entries{};
    size_t count{};
};
}
