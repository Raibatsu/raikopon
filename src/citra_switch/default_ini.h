// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace DefaultINI {

// Options will be added overtime for other things. I just need some basics now.
constexpr const char* sConfigFile = R"(
[Core]
# Whether to use the dynarmic JIT (1, default) or the dyncom interpreter (0).
use_cpu_jit =
# CPU clock speed as a percentage of the real 3DS (5 - 400, default 100).
cpu_clock_percentage =
# Disabled by default to reduce CPU overhead.
is_new_3ds = false

[Renderer]
# Renderer backend: 0: Software, 1: OpenGL, 2: Vulkan (default).
graphics_api =
# Use GLES instead of desktop GL (Forced 1 since we only have GLES).
use_gles =
# Internal resolution scale. 0: auto (window size), 1: native (default).
resolution_factor =
# Synchronise presentation to vblank (1, default).
use_vsync =
# Compile shaders on a background thread to reduce hitching (0, default).
async_shader_compilation =
# Persist compiled shaders to the SD card to cut post first-run stutter (1, default).
use_disk_shader_cache =
# Show an on-screen frame-rate counter (0, default).
show_fps = false

[System]
# Console region. -1: auto-select (default), 0: JPN, 1: USA, 2: EUR, 3: AUS, 4: CHN, 5: KOR, 6: TWN.
region_value =

[Miscellaneous]
# Log filter, e.g. "*:Info" (default) or "*:Debug Core.Cpu:Trace".
log_filter =
)";

} // namespace DefaultINI
