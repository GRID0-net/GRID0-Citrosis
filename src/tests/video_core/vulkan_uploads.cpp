// SPDX-FileCopyrightText: 2026 Citrosis contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdlib>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"

TEST_CASE("Vulkan uploads remain visible without an extra post-copy barrier", "[vulkan][uploads]") {
    if (!std::getenv("CITROSIS_TEST_VULKAN")) {
        SKIP("Set CITROSIS_TEST_VULKAN=1 to run the headless GPU transfer regression");
    }
    auto library = Vulkan::OpenLibrary();
    Vulkan::vk::InstanceDispatch dispatch;
    auto instance = Vulkan::CreateInstance(*library, dispatch, VK_API_VERSION_1_1);
    const auto physicals = instance.EnumeratePhysicalDevices();
    REQUIRE_FALSE(physicals.empty());
    Vulkan::Device device(*instance, Vulkan::vk::PhysicalDevice(physicals[0], dispatch),
                          VK_NULL_HANDLE, dispatch);
    Vulkan::StateTracker state;
    Vulkan::Scheduler scheduler(device, state);
    Vulkan::MemoryAllocator allocator(device);
    Vulkan::StagingBufferPool staging(device, allocator, scheduler);
    Vulkan::GuestDescriptorQueue guest(device, Vulkan::UpdateDescriptorQueue::GUEST_FRAME_PAYLOAD_SIZE);
    Vulkan::ComputePassDescriptorQueue compute(device, Vulkan::UpdateDescriptorQueue::COMPUTE_FRAME_PAYLOAD_SIZE);
    Vulkan::DescriptorPool descriptors(device, scheduler);
    Vulkan::BufferCacheRuntime runtime(device, allocator, scheduler, staging, guest, compute, descriptors);
    constexpr size_t size = 4096;
    auto target = allocator.CreateBuffer({
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }, Vulkan::MemoryUsage::DeviceLocal);
    auto upload = staging.Request(size, Vulkan::MemoryUsage::Upload);
    auto download = staging.Request(size, Vulkan::MemoryUsage::Download);
    for (size_t i = 0; i < size; ++i) upload.mapped_span[i] = static_cast<u8>(i * 17 + 3);
    REQUIRE(upload.buffer == staging.StreamBuf());
    const std::array<VideoCommon::BufferCopy, 1> upload_copy{{{upload.offset, 0, size}}};
    const std::array<VideoCommon::BufferCopy, 1> download_copy{{{0, download.offset, size}}};
    SECTION("reordered upload prelude") {
        runtime.CopyBuffer(*target, upload.buffer, upload_copy, true, true);
    }
    SECTION("inline upload") {
        runtime.CopyBuffer(*target, upload.buffer, upload_copy, true, false);
    }
    // Intentionally no extra PostCopyBarrier: CopyBuffer/submission provide it.
    runtime.CopyBuffer(download.buffer, *target, download_copy, true, false);
    scheduler.Finish();
    CHECK(std::equal(upload.mapped_span.begin(), upload.mapped_span.end(), download.mapped_span.begin()));
}
