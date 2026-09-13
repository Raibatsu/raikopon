// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// Per-frame draw/input for the install modal's file browser. Does not itself block - opening a
// directory just re-lists it.
void UpdateInstall(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas);

// Runs the actual (blocking) CIA install with its own live progress rendering - owns several
// Begin/EndFrame cycles itself via the progress callback, so it must be called directly from the
// launcher's own loop, never from inside another screen's single-frame Update function.
void RunInstall(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, const std::string& path);

} // namespace SwitchFrontend
