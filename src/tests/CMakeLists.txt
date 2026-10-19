# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(tests
    common/bit_field.cpp
    common/cityhash.cpp
    common/container_hash.cpp
    common/fibers.cpp
    common/host_memory.cpp
    common/param_package.cpp
    common/range_map.cpp
    common/ring_buffer.cpp
    common/scratch_buffer.cpp
    common/undefined_fix.cpp
    common/unique_function.cpp
    core/core_timing.cpp
    core/file_sys/integrity_storage.cpp
    core/internal_network/network.cpp
    video_core/pipeline_transitions.cpp
    video_core/vulkan_uploads.cpp
    video_core/descriptor_table.cpp
    video_core/memory_tracker.cpp
    input_common/calibration_configuration_job.cpp
)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core input_common video_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)

add_test(NAME tests COMMAND tests)

add_executable(memory_regressions memory_regressions.cpp ../common/memory_budget.cpp)
target_include_directories(memory_regressions PRIVATE ..)
target_compile_features(memory_regressions PRIVATE cxx_std_20)
add_test(NAME memory_regressions COMMAND memory_regressions)

# needed for vma
if (NOT MSVC)
    target_compile_options(tests PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>)
endif()

if (ENABLE_QT)
    add_executable(eden_transfer_tests qt/eden_transfer.cpp ../yuzu/eden_transfer.cpp)
    target_include_directories(eden_transfer_tests PRIVATE ..)
    target_link_libraries(eden_transfer_tests PRIVATE Qt6::Core)
    add_test(NAME eden_transfer COMMAND eden_transfer_tests)
endif()

if (ENABLE_QT AND UNIX)
    add_executable(startup_process_tests qt/startup_process.cpp ../yuzu/startup_process.cpp)
    target_include_directories(startup_process_tests PRIVATE ..)
    target_link_libraries(startup_process_tests PRIVATE Qt6::Core)
    add_test(NAME startup_process COMMAND startup_process_tests)
endif()

add_executable(descriptor_table_bench EXCLUDE_FROM_ALL video_core/descriptor_table_bench.cpp)
target_include_directories(descriptor_table_bench PRIVATE ..)
target_link_libraries(descriptor_table_bench PRIVATE video_core common ${PLATFORM_LIBRARIES})
