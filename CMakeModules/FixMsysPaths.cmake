# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

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
