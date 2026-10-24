# SPDX-FileCopyrightText: Copyright 2026 crueter
# SPDX-License-Identifier: LGPL-3.0-or-later

## DetectPlatform ##

# This is a small helper that sets platform variables for various
# operating systems and distributions. Note that Apple, Windows, Android, etc.
# are not covered, as CMake already does that for us.

# It also sets CXX_<compiler> for the C++ compiler.

# This module contains contributions from the Eden Emulator Project,
# notably from crueter and Lizzie.

if (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
    set(SOLARIS ON)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "OpenOrbis")
    set(OPENORBIS ON)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "managarm")
    set(MANAGARM ON)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Haiku")
    set(HAIKUOS ON)
endif()

# BSD
if (DEFINED BSD)
    if ("${BSD}" STREQUAL "DragonFlyBSD")
        set(DRAGONFLYBSD ON)
    elseif ("${BSD}" STREQUAL "FreeBSD")
        set(FREEBSD ON)
    elseif ("${BSD}" STREQUAL "OpenBSD")
        set(OPENBSD ON)
    elseif ("${BSD}" STREQUAL "NetBSD")
        set(NETBSD ON)
    endif()
endif()

# dumb heuristic to detect msys2
if (CMAKE_COMMAND MATCHES "msys64")
    set(MSYS2 ON)
endif()

# compiler checks
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CXX_CLANG ON)
    if (MSVC)
        set(CXX_CLANG_CL ON)
    endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CXX_GCC ON)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    set(CXX_ICC ON)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CXX_APPLE ON)
endif()

# https://gitlab.kitware.com/cmake/cmake/-/merge_requests/11112
# This works totally fine on MinGW64, but not CLANG{,ARM}64
if(MINGW AND CXX_CLANG)
    set(CMAKE_SYSTEM_VERSION 10.0.0)
endif()

# MSYS2 utilities

# Sometimes, PkgConfig modules will incorrectly reference / when CMake
# wants you to reference it as C:/msys64/. This function corrects that.
# Example in a Find module:
#[[
    if (MSYS2)
        FixMsysPath(PkgConfig::OPUS)
    endif()
]]

function(FixMsysPath target)
    # Find modules call this whether or not pkg-config found the package; with no target there
    # is nothing to fix (the caller then falls back to the bundled copy).
    if (NOT TARGET ${target})
        return()
    endif()

    get_target_property(include_dir ${target} INTERFACE_INCLUDE_DIRECTORIES)

    if (NOT include_dir OR NOT (include_dir MATCHES "^/"))
        return()
    endif()

    # MSYS2 is not always at C:/msys64 (GitHub's setup-msys2 uses D:/a/_temp/msys64). Prefer
    # MSYS2_LOCATION, else derive the root from the compiler (<root>/<prefix>/bin/clang.exe).
    # Quoted: an unquoted empty value would unset the variable instead.
    set(root_default "$ENV{MSYS2_LOCATION}")
    if (root_default STREQUAL "" AND CMAKE_C_COMPILER)
        get_filename_component(compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(compiler_prefix "${compiler_bin}" DIRECTORY)
        get_filename_component(root_default "${compiler_prefix}" DIRECTORY)
    endif()
    if (root_default STREQUAL "")
        set(root_default "C:/msys64")
    endif()

    # -DMSYS_ROOT_PATH=... overrides the detected root.
    if (MSYS_ROOT_PATH)
        set(msys_root "${MSYS_ROOT_PATH}")
    else()
        set(msys_root "${root_default}")
    endif()

    set(fixed_dirs "")
    foreach(dir IN LISTS include_dir)
        if (dir MATCHES "^/")
            list(APPEND fixed_dirs "${msys_root}${dir}")
        else()
            list(APPEND fixed_dirs "${dir}")
        endif()
    endforeach()
    set_target_properties(${target} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${fixed_dirs}")
endfunction()

# Saves linking time
if (MINGW)
    set(MINGW_FLAGS "-Wl,--strip-all -Wl,--gc-sections")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${MINGW_FLAGS}")
endif()
