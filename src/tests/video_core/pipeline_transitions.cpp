// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/pipeline_transition_cache.h"

TEST_CASE("Pipeline transitions are bounded and cold entries can be recovered", "[video_core][transitions]") {
    std::array<int, 64> pipelines{};
    VideoCommon::PipelineTransitionCache<int> cache;
    for (size_t i = 0; i < pipelines.size(); ++i) {
        pipelines[i] = static_cast<int>(i);
        cache.Insert(&pipelines[i]);
    }
    REQUIRE(cache.Size() == 16);
    CHECK(cache.Find([](int* p) { return *p == 0; }) == nullptr);
    // The caller's global pipeline table owns cold entries, so reinsertion is safe.
    cache.Insert(&pipelines[0]);
    CHECK(cache.Find([](int* p) { return *p == 0; }) == &pipelines[0]);
    cache.Insert(&pipelines[0]);
    CHECK(cache.Size() == 16);
    size_t comparisons{};
    CHECK(cache.Find([&](int* p) { ++comparisons; return *p == 0; }) == &pipelines[0]);
    CHECK(comparisons == 1);
    comparisons = 0;
    CHECK(cache.Find([&](int*) { ++comparisons; return false; }) == nullptr);
    CHECK(comparisons == 16);
    for (size_t i = 1; i < 16; ++i) cache.Insert(&pipelines[i]);
    REQUIRE(cache.Find([](int* p) { return *p == 0; }) != nullptr);
    cache.Insert(&pipelines[16]);
    CHECK(cache.Find([](int* p) { return *p == 0; }) == &pipelines[0]);
    CHECK(cache.Find([](int* p) { return *p == 1; }) == nullptr);
}
