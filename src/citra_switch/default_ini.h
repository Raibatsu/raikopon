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
# Run PICA and renderer work on a dedicated host thread (0, default, restart required).
async_gpu_emulation = false
# Drain after each GPU trigger for compatibility testing (0, default).
strict_gpu_sync = false
# Compile shaders on a background thread to reduce hitching (0, default).
async_shader_compilation =
# Persist compiled shaders to the SD card to cut post first-run stutter (1, default).
use_disk_shader_cache =
# Use hardware-accelerated PICA shaders instead of the software shader interpreter (1, default).
use_hw_shader =
disable_pipeline_fast_path =
skip_slow_draw = false
skip_texture_copy = false
skip_cpu_write = false
# Raise the CPU clock (at the cost of dropping the GPU clock) while shaders/pipelines are
# compiling in-game, dramatically speeding up compile time (1, default). Boot-time loading screen
# compiles always use this regardless of the setting, since nothing is being rendered to lose GPU
# performance from yet.
enable_compile_boost = true
# Texture upscaling filter: 0: None (default), 1: Anime4K, 2: Bicubic, 3: ScaleForce, 4: xBRZ, 5: MMPX.
texture_filter =
# Scale the screen by whole-number factors only, avoiding uneven pixel stretching (0, default).
use_integer_scaling =
# Smooth (bilinear) filtering of the 3DS's low native resolution instead of a sharp/blocky look (1, default).
filter_mode = true
# Stereoscopic 3D mode. 0: Off (default), 1: Side by side, 2: Side by side (full), 3: Anaglyph,
# 4: Interlaced, 5: Reverse interlaced, 6: Nintendo Labo VR (side-by-side split scaled/aligned for
# a cardboard-style viewer held up to the screen).
render_3d =
# Depth of the 3D effect, 0-100 (0, default).
factor_3d =
# Swap which eye's image is shown on which side (0, default).
swap_eyes_3d = false
# Which eye's view is used when only one is rendered (e.g. 2D or Anaglyph). 0: Left eye (default),
# 1: Right eye.
mono_render_option =
# Labo VR viewport size as a percentage of the full layout, 30-100 (100, default).
cardboard_screen_size = 100
# Labo VR horizontal/vertical eye alignment, -100 to 100 (0, default -- centered).
cardboard_x_shift = 0
cardboard_y_shift = 0
# Skip rendering the 3D right-eye view entirely, since Switch never displays it anyway (1, default
# on Switch -- the shared upstream default is 0, since desktop/Android can still output real 3D).
disable_right_eye_render = true
# Independent top/bottom screen opacity for Custom Layout, 0-100 (100, default -- fully opaque).
top_screen_opacity = 100
bottom_screen_opacity = 100
# Show an on-screen frame-rate counter (0, default).
show_fps = false
# Compile PICA vertex shaders to native code instead of interpreting them (1, default).
# Only affects draws that fall back to the CPU shader engine.
# Has chance of crashing on some games, although should be safe.
use_shader_jit =

[Utility]
# Load a custom texture pack from load/textures/<TITLE_ID>/ (0, default).
# Can also be toggled live from the quick menu.
custom_textures = false
# Preload the whole pack at boot instead of streaming it in (0, default).
# Costs memory up front but avoids in-game hitches. Only matters with custom_textures on.
preload_textures = false
# Dump the game's textures to dump/textures/<TITLE_ID>/ to build a pack (0, default).
# Takes effect on the next launch.
dump_textures = false

[System]
# Console region. -1: auto-select (default), 0: JPN, 1: USA, 2: EUR, 3: AUS, 4: CHN, 5: KOR, 6: TWN.
region_value =

[Miscellaneous]
# Log filter, e.g. "*:Info" (default) or "*:Debug Core.Cpu:Trace".
log_filter =

[Switch]
# Directory scanned for titles. Defaults to "roms/" under the dekopon directory when unset.
# The dekopon directory itself is set from sdmc:/switch/dekopon/user_dir.txt
roms_dir =
# Descend into the ROM directory's subfolders when scanning (1, default).
scan_recursive =
# What drives the touch pointer. 0: left stick (default), 1: gyro, 2: right stick.
pointer_source =
# Gyro pointer sensitivity per axis, as a percentage of the default speed (100, default). 10-500.
gyro_sensitivity_x =
gyro_sensitivity_y =
# Bitmask of screen-layout presets the R3 button cycles through (bit 0 = the first preset).
# Defaults to every preset enabled. The quick menu always offers every layout.
layout_cycle_mask =
# Core Clock percentage used while a movie-library CRO is loaded, e.g. Pokémon cutscenes
# (45, default). Editable from the quick menu. 10-100.
movie_throttle_clock_percentage =

[Controls]
# Controller remapping, editable from Settings > Remap Controls.
# Each control stores the physical Switch button that drives it, by index:
#   0:A 1:B 2:X 3:Y 4:Up 5:Down 6:Left 7:Right 8:L 9:R 10:+ 11:- 12:ZL 13:ZR 14:L3 15:R3
#   16 leaves the control unbound.
# map_toggle_pointer/map_cycle_layout/map_touch_tap are the emulator actions.
map_a =
map_b =
map_x =
map_y =
map_up =
map_down =
map_left =
map_right =
map_l =
map_r =
map_start =
map_select =
map_zl =
map_zr =
map_toggle_pointer =
map_cycle_layout =
map_touch_tap =
)";

} // namespace DefaultINI
