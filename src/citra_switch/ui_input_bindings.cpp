// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_input_bindings.h"

#include <array>
#include <cstddef>
#include <sstream>

#include <INIReader.h>

#include "citra_switch/ui_strings.h"
#include "common/file_util.h"
#include "common/logging/log.h"

// Deliberately avoids <switch.h> here (see common/shader_compile_stats.h) - its u128 typedef
// conflicts with common_types.h's, which common/file_util.h pulls in. The HidNpadButton_* bit
// positions below are copied from libnx's switch/services/hid.h rather than included from it.
namespace SwitchFrontend {

namespace {

constexpr std::uint64_t kBtnA = 1ULL << 0;
constexpr std::uint64_t kBtnB = 1ULL << 1;
constexpr std::uint64_t kBtnStickL = 1ULL << 4;
constexpr std::uint64_t kBtnStickR = 1ULL << 5;
constexpr std::uint64_t kBtnL = 1ULL << 6;
constexpr std::uint64_t kBtnR = 1ULL << 7;
constexpr std::uint64_t kBtnZL = 1ULL << 8;
constexpr std::uint64_t kBtnZR = 1ULL << 9;
constexpr std::uint64_t kBtnPlus = 1ULL << 10;
constexpr std::uint64_t kBtnMinus = 1ULL << 11;
constexpr std::uint64_t kBtnLeft = 1ULL << 12;
constexpr std::uint64_t kBtnUp = 1ULL << 13;
constexpr std::uint64_t kBtnRight = 1ULL << 14;
constexpr std::uint64_t kBtnDown = 1ULL << 15;
constexpr std::uint64_t kBtnStickLLeft = 1ULL << 16;
constexpr std::uint64_t kBtnStickLUp = 1ULL << 17;
constexpr std::uint64_t kBtnStickLRight = 1ULL << 18;
constexpr std::uint64_t kBtnStickLDown = 1ULL << 19;
constexpr std::uint64_t kBtnStickRLeft = 1ULL << 20;
constexpr std::uint64_t kBtnStickRUp = 1ULL << 21;
constexpr std::uint64_t kBtnStickRRight = 1ULL << 22;
constexpr std::uint64_t kBtnStickRDown = 1ULL << 23;

// Short labels for the 16 real button bits, indexed by bit position - used by
// RawButtonBitLabel()/PrimaryButtonLabel() to render binding chips.
constexpr std::array<const char*, 16> kBitLabels{{
    "A", "B", "X", "Y", "L3", "R3", "L", "R", "ZL", "ZR", "+", "-", "Left", "Up", "Right", "Down",
}};

constexpr std::uint64_t kBtnAnyUp = kBtnUp | kBtnStickLUp | kBtnStickRUp;
constexpr std::uint64_t kBtnAnyDown = kBtnDown | kBtnStickLDown | kBtnStickRDown;
constexpr std::uint64_t kBtnAnyLeft = kBtnLeft | kBtnStickLLeft | kBtnStickRLeft;
constexpr std::uint64_t kBtnAnyRight = kBtnRight | kBtnStickLRight | kBtnStickRRight;

constexpr int kNumMenuActions = static_cast<int>(MenuAction::Count);

struct DefaultBinding {
    MenuAction action;
    const char* key;
    std::uint64_t buttons;
};

constexpr std::array<DefaultBinding, kNumMenuActions> kDefaultBindings{{
    {MenuAction::Up, "up", kBtnAnyUp},
    {MenuAction::Down, "down", kBtnAnyDown},
    {MenuAction::Left, "left", kBtnAnyLeft},
    {MenuAction::Right, "right", kBtnAnyRight},
    {MenuAction::Confirm, "confirm", kBtnA},
    {MenuAction::Cancel, "cancel", kBtnB},
    {MenuAction::TabPrev, "tab_prev", kBtnL},
    {MenuAction::TabNext, "tab_next", kBtnR},
    {MenuAction::Minus, "minus", kBtnMinus},
    {MenuAction::Plus, "plus", kBtnPlus},
    {MenuAction::ExitToLibrary, "exit_to_library", kBtnPlus | kBtnMinus},
    {MenuAction::RailPrev, "rail_prev", kBtnZL},
    {MenuAction::RailNext, "rail_next", kBtnZR},
    {MenuAction::ResetToDefault, "reset_to_default", kBtnMinus},
    {MenuAction::TogglePointer, "toggle_pointer", kBtnStickL},
    {MenuAction::CycleLayout, "cycle_layout", kBtnStickR},
    {MenuAction::MirrorScreen, "mirror_screen", kBtnMinus | kBtnStickL},
    {MenuAction::TouchTap, "touch_tap", kBtnZR},
}};

std::array<std::uint64_t, kNumMenuActions> s_bindings = [] {
    std::array<std::uint64_t, kNumMenuActions> bindings{};
    for (const DefaultBinding& binding : kDefaultBindings) {
        bindings[static_cast<int>(binding.action)] = binding.buttons;
    }
    return bindings;
}();

std::string MenuControlsFile() {
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "menu_controls.ini";
}

} // namespace

void ResetMenuBindingsToDefault() {
    for (const DefaultBinding& binding : kDefaultBindings) {
        s_bindings[static_cast<int>(binding.action)] = binding.buttons;
    }
}

std::uint64_t GetMenuActionButtons(MenuAction action) {
    return s_bindings[static_cast<int>(action)];
}

void SetMenuActionButtons(MenuAction action, std::uint64_t buttons) {
    s_bindings[static_cast<int>(action)] = buttons;
    SaveMenuBindings();
}

void LoadMenuBindings() {
    ResetMenuBindingsToDefault();

    const std::string path = MenuControlsFile();
    if (!FileUtil::Exists(path)) {
        return;
    }
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, path, buffer)) {
        return;
    }
    INIReader ini{buffer.c_str(), buffer.size()};
    if (ini.ParseError() < 0) {
        LOG_ERROR(Config, "Malformed menu controls file '{}'", path);
        return;
    }

    static constexpr const char* kSection = "MenuControls";
    for (const DefaultBinding& binding : kDefaultBindings) {
        const std::int64_t value = ini.GetInteger64(kSection, binding.key,
                                                     static_cast<std::int64_t>(binding.buttons));
        s_bindings[static_cast<int>(binding.action)] = static_cast<std::uint64_t>(value);
    }
}

std::string MenuActionName(MenuAction action) {
    switch (action) {
    case MenuAction::Up:
        return Tr("action.nav_up");
    case MenuAction::Down:
        return Tr("action.nav_down");
    case MenuAction::Left:
        return Tr("action.nav_left");
    case MenuAction::Right:
        return Tr("action.nav_right");
    case MenuAction::Confirm:
        return Tr("action.confirm");
    case MenuAction::Cancel:
        return Tr("action.cancel_back");
    case MenuAction::TabPrev:
        return Tr("action.tab_prev");
    case MenuAction::TabNext:
        return Tr("action.tab_next");
    case MenuAction::Minus:
        return Tr("action.minus");
    case MenuAction::Plus:
        return Tr("action.plus");
    case MenuAction::ExitToLibrary:
        return Tr("action.exit_to_library");
    case MenuAction::RailPrev:
        return Tr("action.rail_prev");
    case MenuAction::RailNext:
        return Tr("action.rail_next");
    case MenuAction::ResetToDefault:
        return Tr("overlay.reset_to_default");
    case MenuAction::TogglePointer:
        return Tr("action.toggle_pointer");
    case MenuAction::CycleLayout:
        return Tr("action.cycle_layout");
    case MenuAction::MirrorScreen:
        return Tr("action.mirror_screen");
    case MenuAction::TouchTap:
        return Tr("action.touch_tap");
    case MenuAction::Count:
        break;
    }
    return "";
}

const char* RawButtonBitLabel(std::uint64_t single_bit) {
    for (int i = 0; i < static_cast<int>(kBitLabels.size()); ++i) {
        if (single_bit == (1ULL << i)) {
            return kBitLabels[static_cast<std::size_t>(i)];
        }
    }
    return "";
}

const char* PrimaryButtonLabel(MenuAction action) {
    const std::uint64_t mask = GetMenuActionButtons(action);
    for (int i = 0; i < static_cast<int>(kBitLabels.size()); ++i) {
        if ((mask & (1ULL << i)) != 0) {
            return kBitLabels[static_cast<std::size_t>(i)];
        }
    }
    return "";
}

void SaveMenuBindings() {
    std::ostringstream ss;
    ss << "[MenuControls]\n";
    for (const DefaultBinding& binding : kDefaultBindings) {
        ss << binding.key << " = " << s_bindings[static_cast<int>(binding.action)] << '\n';
    }

    const std::string path = MenuControlsFile();
    FileUtil::CreateFullPath(path);
    if (!FileUtil::WriteStringToFile(true, path, ss.str())) {
        LOG_ERROR(Config, "Failed to save menu controls to '{}'", path);
    }
}

} // namespace SwitchFrontend
