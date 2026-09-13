// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_controls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/settings_model.h"
#include "citra_switch/ui_badge.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 44.0f;
constexpr float kRowPadding = 6.0f;
constexpr float kBadgeSize = 24.0f;
constexpr float kBadgeGap = 6.0f;
constexpr float kIdleConfirmSeconds = 0.6f;
constexpr float kCountdownBarWidth = 120.0f;
constexpr float kCountdownBarHeight = 4.0f;

// Cancel is a two-button hold rather than any single button, so every physical button - including
// B - stays freely bindable through capture. ZL+ZR: otherwise-idle in the menu, easy to hold
// deliberately, and not a chord anyone is likely to want bound to a single control. Raw
// HidNpadButton_* bit positions, same numbering as MenuInput::raw_held.
constexpr std::uint64_t kRawBitZL = 1ULL << 8;
constexpr std::uint64_t kRawBitZR = 1ULL << 9;
constexpr std::uint64_t kCaptureCancelChord = kRawBitZL | kRawBitZR;

enum class ControlSection { InGame, Ui, TouchMotion };

struct ControlEntry {
    bool is_header;
    ControlSection section;
    int index;
    std::string label;
    // TouchMotion rows aren't remappable buttons - they cycle/adjust a MenuSettings field
    // directly instead of entering the capture flow below. `setting_item` is only meaningful
    // when this is true.
    bool is_setting_row = false;
    SettingRowIdx setting_item = SettingRowSectionHeader;
};

const char* ShortInGameLabel(InputButton button) {
    switch (button) {
    case InputButton::A:
        return "A";
    case InputButton::B:
        return "B";
    case InputButton::X:
        return "X";
    case InputButton::Y:
        return "Y";
    case InputButton::Up:
        return "Up";
    case InputButton::Down:
        return "Down";
    case InputButton::Left:
        return "Left";
    case InputButton::Right:
        return "Right";
    case InputButton::L:
        return "L";
    case InputButton::R:
        return "R";
    case InputButton::Start:
        return "+";
    case InputButton::Select:
        return "-";
    case InputButton::ZL:
        return "ZL";
    case InputButton::ZR:
        return "ZR";
    case InputButton::L3:
        return "L3";
    case InputButton::R3:
        return "R3";
    case InputButton::None:
        return "";
    }
    return "";
}

// TogglePointer/CycleLayout/TouchTap used to be MappableControls (single-bind, input.h) - all
// three are fully migrated now to real MenuAction entries (multi-bind, ui_input_bindings.h),
// listed automatically by the MenuAction loop below under "UI Controls" since they don't map to
// an actual 3DS button the way A/B/X/Y/... do. Their old MappableControl slots in input.h still
// exist but are unused dead data now - nothing reads them anymore.
bool IsMigratedToMenuAction(MappableControl control) {
    return control == MappableControl::TogglePointer || control == MappableControl::CycleLayout ||
          control == MappableControl::TouchTap;
}

std::vector<ControlEntry> BuildEntries() {
    std::vector<ControlEntry> entries;
    entries.push_back({true, ControlSection::InGame, 0, Tr("controls.header.ingame")});
    for (int i = 0; i < NumMappableControls; ++i) {
        const auto control = static_cast<MappableControl>(i);
        if (IsMigratedToMenuAction(control)) {
            continue;
        }
        entries.push_back({false, ControlSection::InGame, i, ControlName(control)});
    }
    entries.push_back({true, ControlSection::Ui, 0, Tr("controls.header.ui")});
    const int num_menu_actions = static_cast<int>(MenuAction::Count);
    for (int i = 0; i < num_menu_actions; ++i) {
        const auto action = static_cast<MenuAction>(i);
        // Directional navigation isn't offered here - it defaults to a combined d-pad+both-stick
        // mask, and rebinding it away from that default makes the menu hard to navigate at all,
        // possibly including the very Controls tab needed to fix it back.
        if (action == MenuAction::Up || action == MenuAction::Down || action == MenuAction::Left ||
            action == MenuAction::Right) {
            continue;
        }
        entries.push_back({false, ControlSection::Ui, i, MenuActionName(action)});
    }
    entries.push_back({true, ControlSection::TouchMotion, 0, Tr("header.touch_motion")});
    entries.push_back({false, ControlSection::TouchMotion, 0, Tr("settings.row.pointer_source"), true,
                       SettingRowPointerSource});
    entries.push_back(
        {false, ControlSection::TouchMotion, 0, Tr("settings.row.gyro_sensitivity"), true, SettingRowGyroSensitivity});
    return entries;
}

int NextSelectable(const std::vector<ControlEntry>& entries, int index, int dir) {
    const int size = static_cast<int>(entries.size());
    int i = std::clamp(index, 0, size - 1);
    while (i >= 0 && i < size && entries[static_cast<std::size_t>(i)].is_header) {
        i += dir;
    }
    if (i < 0 || i >= size) {
        // Only reachable walking up past the very first header - every header is immediately
        // followed by at least one row, so walking forward from the top always finds one.
        i = 0;
        while (entries[static_cast<std::size_t>(i)].is_header) {
            ++i;
        }
    }
    return i;
}

void DrawBoundBadges(GpuCanvas& canvas, GlyphAtlas& atlas, float right_edge, float center_y,
                    const std::vector<const char*>& labels) {
    if (labels.empty()) {
        const std::string unbound = Tr("controls.unbound");
        const float width = MeasureText(atlas, canvas, unbound);
        DrawText(canvas, atlas, right_edge - width, center_y - atlas.LineHeight() * 0.5f, unbound,
                CurrentPalette().text_dim);
        return;
    }
    float x = right_edge;
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        x -= kBadgeSize;
        DrawButtonBadge(canvas, atlas, x, center_y, kBadgeSize, *it);
        x -= kBadgeGap;
    }
}

} // namespace

void UpdateControlsTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                       GlyphAtlas& atlas, float dt, float content_x, float content_top,
                       float content_w, float viewport_bottom) {
    const UiPalette& palette = CurrentPalette();

    const std::vector<ControlEntry> entries = BuildEntries();
    model.controls_row = std::clamp(model.controls_row, 0, static_cast<int>(entries.size()) - 1);
    if (entries[static_cast<std::size_t>(model.controls_row)].is_header) {
        model.controls_row = NextSelectable(entries, model.controls_row, 1);
    }

    if (model.controls_setting_armed) {
        const ControlEntry& target = entries[static_cast<std::size_t>(model.controls_row)];
        if (input.cancel || input.confirm) {
            model.controls_setting_armed = false;
            // Persist here, once, rather than on every held-repeat tick above (ApplyMenuSettings
            // only applies live) - see menu_data.h's ApplyMenuSettings comment for why.
            SaveConfig();
        } else if (target.setting_item == SettingRowGyroSensitivity) {
            if (input.up) {
                model.settings_gyro_axis = 0;
            }
            if (input.down) {
                model.settings_gyro_axis = 1;
            }
            if (input.left) {
                AdjustGyroAxis(model.settings, model.settings_gyro_axis == 1, -1);
                ApplyMenuSettings(model.settings);
            }
            if (input.right) {
                AdjustGyroAxis(model.settings, model.settings_gyro_axis == 1, 1);
                ApplyMenuSettings(model.settings);
            }
        } else {
            if (input.left) {
                CycleSetting(model.settings, target.setting_item, -1);
                ApplyMenuSettings(model.settings);
            }
            if (input.right) {
                CycleSetting(model.settings, target.setting_item, 1);
                ApplyMenuSettings(model.settings);
            }
        }
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.done")};
        model.hint_chip_count = 1;
    } else if (model.controls_capturing) {
        const ControlEntry& target = entries[static_cast<std::size_t>(model.controls_row)];

        if ((input.raw_held & kCaptureCancelChord) == kCaptureCancelChord) {
            model.controls_capturing = false;
        } else if (target.section == ControlSection::InGame) {
            if (input.raw_pressed_button != InputButton::None) {
                model.controls_capture_button = input.raw_pressed_button;
                model.controls_capture_idle = 0.0f;
            } else if (model.controls_capture_button != InputButton::None) {
                model.controls_capture_idle += dt;
                if (model.controls_capture_idle >= kIdleConfirmSeconds) {
                    SetMapping(static_cast<MappableControl>(target.index),
                              model.controls_capture_button);
                    model.controls_capturing = false;
                }
            }
        } else {
            if (input.raw_pressed != 0) {
                model.controls_capture_mask |= input.raw_pressed;
                model.controls_capture_idle = 0.0f;
            } else if (model.controls_capture_mask != 0) {
                model.controls_capture_idle += dt;
                if (model.controls_capture_idle >= kIdleConfirmSeconds) {
                    SetMenuActionButtons(static_cast<MenuAction>(target.index),
                                         model.controls_capture_mask);
                    model.controls_capturing = false;
                }
            }
        }

        model.hint_chip_count = 0;
    } else {
        if (input.up) {
            model.controls_row = NextSelectable(entries, model.controls_row - 1, -1);
        }
        if (input.down) {
            model.controls_row = NextSelectable(entries, model.controls_row + 1, 1);
        }
        const ControlEntry& current = entries[static_cast<std::size_t>(model.controls_row)];
        if (input.confirm) {
            if (current.is_setting_row) {
                model.controls_setting_armed = true;
                model.settings_gyro_axis = 0;
            } else {
                model.controls_capturing = true;
                model.controls_capture_mask = 0;
                model.controls_capture_button = InputButton::None;
                model.controls_capture_idle = 0.0f;
            }
        }
        // Permanently available (not gated behind a specific row) so it's always one press away,
        // matching the rest of Settings' own app-wide reset - but scoped to just Controls (see
        // ResetAllControlsToDefault) rather than every setting.
        if (input.reset_default) {
            model.settings_confirm_kind = SettingsConfirmKind::ResetControls;
            model.settings_confirm_message = Tr("confirm.reset_controls");
            model.settings_confirm_selected = 1;
        }

        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm),
                               current.is_setting_row ? Tr("hint.adjust") : Tr("controls.hint.rebind")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::TabPrev), Tr("hint.prev_tab")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::TabNext), Tr("hint.next_tab")};
        model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chips[4] = {PrimaryButtonLabel(MenuAction::ResetToDefault), Tr("hint.reset")};
        model.hint_chip_count = 5;
    }

    const float selected_top =
        content_top + static_cast<float>(model.controls_row) * (kRowHeight + kRowPadding);
    const float content_height = static_cast<float>(entries.size()) * (kRowHeight + kRowPadding);
    UpdateScroll(model.controls_scroll, input, model.controls_row, selected_top, kRowHeight,
                content_x, content_top, content_w, viewport_bottom - content_top,
                content_height - (viewport_bottom - content_top));
    const float scroll_offset = model.controls_scroll.offset;

    canvas.SetClipRect(content_x, content_top, content_w, viewport_bottom - content_top);

    int tapped_row = -1;
    float y = content_top - scroll_offset;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ControlEntry& entry = entries[i];
        const bool selected = static_cast<int>(i) == model.controls_row;
        const bool capturing_this = selected && model.controls_capturing;
        const bool setting_armed_this = selected && model.controls_setting_armed;

        if (y + kRowHeight < content_top || y > viewport_bottom) {
            y += kRowHeight + kRowPadding;
            continue;
        }

        if (entry.is_header) {
            DrawText(canvas, atlas, content_x, y + (kRowHeight - atlas.LineHeight()) * 0.5f,
                    entry.label, palette.accent);
            canvas.DrawQuad(content_x, y + kRowHeight - kHeaderUnderlineHeight, content_w,
                            kHeaderUnderlineHeight, palette.accent);
            y += kRowHeight + kRowPadding;
            continue;
        }

        if (input.touch_tap && input.touch_x >= content_x && input.touch_x < content_x + content_w &&
            input.touch_y >= y && input.touch_y < y + kRowHeight) {
            tapped_row = static_cast<int>(i);
        }

        if (selected) {
            CanvasColor row_color = palette.surface_warm;
            if (capturing_this || setting_armed_this) {
                const float pulse = (std::sin(model.wave_elapsed * 2.4f) + 1.0f) * 0.5f;
                row_color = CanvasColor{
                    palette.surface_warm.r + (palette.accent_dim.r - palette.surface_warm.r) * pulse,
                    palette.surface_warm.g + (palette.accent_dim.g - palette.surface_warm.g) * pulse,
                    palette.surface_warm.b + (palette.accent_dim.b - palette.surface_warm.b) * pulse,
                    1.0f,
                };
            }
            canvas.DrawQuad(content_x, y, content_w, kRowHeight, row_color);
        }

        const float label_y = y + (kRowHeight - atlas.LineHeight()) * 0.5f;
        DrawText(canvas, atlas, content_x + kContentPadding, label_y, entry.label,
                selected ? palette.text : palette.text_dim);

        if (entry.is_setting_row) {
            const std::string value =
                entry.setting_item == SettingRowGyroSensitivity
                    ? (setting_armed_this
                          ? GyroSensitivityArmedText(model.settings, model.settings_gyro_axis == 1)
                          : GyroSensitivityText(model.settings))
                    : std::string(
                          PointerSourceName(static_cast<PointerSource>(model.settings.pointer_source)));
            const float value_width = MeasureText(atlas, canvas, value);
            const CanvasColor value_color =
                setting_armed_this ? palette.accent : (selected ? palette.text : palette.text_dim);
            DrawText(canvas, atlas, content_x + content_w - kContentPadding - value_width, label_y,
                    value, value_color);
        } else if (capturing_this) {
            const bool is_ui = entry.section == ControlSection::Ui;
            const std::string prompt =
                is_ui ? Tr("controls.prompt.ui") : Tr("controls.prompt.ingame");
            const float prompt_width = MeasureText(atlas, canvas, prompt);
            DrawText(canvas, atlas, content_x + content_w - kContentPadding - prompt_width, label_y,
                    prompt, palette.accent);

            const bool has_capture = is_ui ? model.controls_capture_mask != 0
                                           : model.controls_capture_button != InputButton::None;
            if (has_capture) {
                const float bar_x = content_x + content_w - kContentPadding - kCountdownBarWidth;
                const float bar_y = y + kRowHeight - kCountdownBarHeight - 4.0f;
                canvas.DrawQuad(bar_x, bar_y, kCountdownBarWidth, kCountdownBarHeight,
                                palette.surface);
                const float fraction =
                    std::clamp(1.0f - model.controls_capture_idle / kIdleConfirmSeconds, 0.0f, 1.0f);
                canvas.DrawQuad(bar_x, bar_y, kCountdownBarWidth * fraction, kCountdownBarHeight,
                                palette.accent);
            }
        } else {
            std::vector<const char*> labels;
            if (entry.section == ControlSection::InGame) {
                const InputButton bound = GetMapping(static_cast<MappableControl>(entry.index));
                if (bound != InputButton::None) {
                    labels.push_back(ShortInGameLabel(bound));
                }
            } else {
                const std::uint64_t mask = GetMenuActionButtons(static_cast<MenuAction>(entry.index));
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1ULL << bit)) != 0) {
                        labels.push_back(RawButtonBitLabel(1ULL << bit));
                    }
                }
            }
            DrawBoundBadges(canvas, atlas, content_x + content_w - kContentPadding,
                            y + kRowHeight * 0.5f, labels);
        }

        y += kRowHeight + kRowPadding;
    }

    canvas.ClearClipRect();

    if (tapped_row >= 0) {
        if (model.controls_row != tapped_row) {
            model.controls_row = tapped_row;
            model.controls_capturing = false;
            model.controls_setting_armed = false;
        } else if (entries[static_cast<std::size_t>(tapped_row)].is_setting_row) {
            model.controls_setting_armed = !model.controls_setting_armed;
            model.settings_gyro_axis = 0;
        } else {
            model.controls_capturing = !model.controls_capturing;
            model.controls_capture_mask = 0;
            model.controls_capture_button = InputButton::None;
            model.controls_capture_idle = 0.0f;
        }
    }
}

void ResetAllControlsToDefault() {
    for (int i = 0; i < NumMappableControls; ++i) {
        const auto control = static_cast<MappableControl>(i);
        if (IsMigratedToMenuAction(control)) {
            continue;
        }
        SetMapping(control, DefaultMapping(control));
    }
    ApplyButtonMappings();
    SaveConfig();

    ResetMenuBindingsToDefault();
    SaveMenuBindings();
}

} // namespace SwitchFrontend
