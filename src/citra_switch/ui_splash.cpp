// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_splash.h"

#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {
constexpr float kSplashDuration = 1.2f;
} // namespace

void UpdateSplash(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                  float dt) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const char* title = "Raika Azahar";
    const float text_width = MeasureText(atlas, canvas, title);
    const float x = (screen_w - text_width) * 0.5f;
    const float y = (screen_h - atlas.LineHeight()) * 0.5f;
    DrawText(canvas, atlas, x, y, title, palette.text);

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("splash.skip")};
    model.hint_chip_count = 1;

    model.splash_elapsed += dt;
    if (model.splash_elapsed >= kSplashDuration || input.confirm || input.touch_tap) {
        model.app_state = AppState::Library;
    }
}

} // namespace SwitchFrontend
