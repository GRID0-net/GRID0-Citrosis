// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Citrosis additions to the Android frontend: the RAM budget and memory pressure monitor, and
// the hooks for network interface selection and GRID0+. JNI entry points live in the .cpp.
namespace Citrosis::Android {

/// Call before System::Load: sets the RAM budget the GPU caches size themselves from and starts
/// the memory pressure monitor.
void OnEmulationStarting();

/// Call after the emulated process has shut down: stops the monitor and clears the pressure.
void OnEmulationStopped();

} // namespace Citrosis::Android
