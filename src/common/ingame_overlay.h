// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

// Carries the in-game quick settings overlay's current visual state from citra_switch.cpp's
// RunGame() (main thread, which owns all navigation/settings logic since it's the only side with
// access to settings_model.h/game_settings.h) to RendererVulkan's per-frame overlay pass (emu
// thread, which only draws). One-directional and read-only on the render side - mirrors
// common/loading_icon.h's shape for the same reason: producer and consumer run on different
// threads with no other shared call path.
namespace Common::IngameOverlay {

struct Row {
    std::string label;
    std::string value;
    // Non-selectable group divider (accent label + underline) - mirrors
    // SwitchFrontend::SettingRow::is_header. Only `label` is meaningful when true.
    bool is_header = false;
};

struct Hint {
    std::string button;
    std::string label;
};

struct State {
    bool open = false;
    std::string title;
    std::string description;
    std::vector<Row> rows;
    int selected = 0;
    bool armed = false;
    std::vector<Hint> hints;
};

void SetState(State state);
State GetState();

// Cheap, lock-free mirror of State::open - safe to call every frame from any thread (e.g. the
// renderer's skip-duplicate-frames gate, which otherwise stops repainting entirely once the game
// itself is paused and stops producing new frames), unlike GetState() which copies the full row
// list under a mutex.
bool IsOpen();

// 0..1 progress of the Plus+Minus hold-to-open gesture, published by citra_switch.cpp's RunGame()
// every frame while the chord is held (and reset to 0 the instant it's released or the overlay
// itself opens) so the renderer can draw a fill-up ring during the hold. Independent of
// SetState/State - the ring needs to animate before `open` ever becomes true.
void SetHoldProgress(float progress);
float GetHoldProgress();

} // namespace Common::IngameOverlay
