# SPDX-License-Identifier: GPL-2.0-or-later
# Run with the MSYS2 CLANG64 CMake, after building with build-citrosis-desktop.sh.
cmake_minimum_required(VERSION 3.31)
foreach(required BIN_DIR OUTPUT_DIR MINGW_PREFIX SOURCE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing -D${required}=...")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(executables)
foreach(name citrosis citrosis-cli citrosis-room)
    set(exe "${BIN_DIR}/${name}.exe")
    if(NOT EXISTS "${exe}")
        message(FATAL_ERROR "Missing ${exe}")
    endif()
    list(APPEND executables "${exe}")
    file(COPY "${exe}" DESTINATION "${OUTPUT_DIR}")
endforeach()
if(NOT EXISTS "${BIN_DIR}/plugins/platforms/qwindows.dll")
    message(FATAL_ERROR "windeployqt did not deploy the Windows platform plugin")
endif()
file(COPY "${BIN_DIR}/plugins" DESTINATION "${OUTPUT_DIR}")
file(GLOB_RECURSE plugins "${BIN_DIR}/plugins/*.dll")
find_program(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND NAMES llvm-objdump objdump REQUIRED)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${executables}
    LIBRARIES ${plugins}
    DIRECTORIES "${BIN_DIR}" "${MINGW_PREFIX}/bin"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
    POST_EXCLUDE_REGEXES [=[.*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*]=])
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime libraries: ${unresolved}")
endif()
foreach(dll IN LISTS resolved)
    file(COPY "${dll}" DESTINATION "${OUTPUT_DIR}")
endforeach()
file(WRITE "${OUTPUT_DIR}/qt.conf" "[Paths]\nPlugins=plugins\n")
file(COPY "${SOURCE_DIR}/README.md" "${SOURCE_DIR}/build.md"
          "${SOURCE_DIR}/LICENSE.txt" "${SOURCE_DIR}/LICENSES"
          "${SOURCE_DIR}/citrosis-desktop-port.json" DESTINATION "${OUTPUT_DIR}")
file(COPY "${MINGW_PREFIX}/share/licenses/" DESTINATION "${OUTPUT_DIR}/LICENSES/msys2")
file(COPY "${SOURCE_DIR}/docs/BUILDING-CITROSIS-DESKTOP.md" DESTINATION "${OUTPUT_DIR}")
file(COPY "${SOURCE_DIR}/dist/post_shaders" "${SOURCE_DIR}/dist/post_presets"
     DESTINATION "${OUTPUT_DIR}/extras")
# Only explicit build outputs are packaged: never copy an existing portable user/ directory.
