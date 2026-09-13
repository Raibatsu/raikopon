// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace SwitchFrontend {

// Every action below except Up/Down/Left/Right is a required chord when bound to multiple
// buttons - every bound bit must be held together (checked with (held & mask) == mask, edge-
// fired on the frame the full mask newly becomes held), not "any one of them" (pressed & mask) !=
// 0. A single-button binding still behaves like a plain press either way, since a one-bit mask
// makes the two checks equivalent - this only matters once an action has 2+ bound buttons.
// Up/Down/Left/Right stay "any bound button" (plus repeat-while-held) since they're not offered
// as rebindable UI Controls rows at all (see ui_controls.cpp's BuildEntries) - requiring a chord
// to move a cursor would make the menu unusable if more than one button were ever bound to them.
enum class MenuAction {
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Cancel,
    TabPrev,
    TabNext,
    Minus,
    Plus,
    // Held chord to leave a running game for the library.
    ExitToLibrary,
    // Jump controller focus onto the left rail (Library/Settings) and select the previous/next
    // item - independent of TabPrev/TabNext, which switch Settings' sub-tabs instead.
    RailPrev,
    RailNext,
    // Resets every setting to its default value. Independent from Minus (used elsewhere, e.g.
    // Library's Install) even though both default to the same physical button, so remapping one
    // doesn't silently move the other.
    ResetToDefault,
    // In-game frontend behavior toggles - not 3DS buttons, so they live here (multi-bind) rather
    // than in input.h's single-bind MappableControl, even though citra_switch.cpp's RunGame loop
    // (not the launcher's menu loop) is what actually checks them, against raw held/pressed the
    // same way the menu actions above are.
    TogglePointer,
    CycleLayout,
    // Swaps which physical screen (top/bottom) shows which emulated screen.
    MirrorScreen,
    // The tap gesture while the touch pointer is active. Checked inside input.cpp's UpdateInput()
    // (not citra_switch.cpp like the two above) - it converts this raw-numbered mask to
    // InputButton numbering internally since that's what state.buttons uses. The chord is
    // evaluated against currently-held buttons each frame (not a press edge), since this drives a
    // continuous simulated-touch state for as long as the full chord stays held.
    TouchTap,
    Count,
};

void LoadMenuBindings();
void SaveMenuBindings();

std::uint64_t GetMenuActionButtons(MenuAction action);
void SetMenuActionButtons(MenuAction action, std::uint64_t buttons);
void ResetMenuBindingsToDefault();

// Display name of a menu action, for the Controls tab's "UI Controls" section.
std::string MenuActionName(MenuAction action);

// Short label ("A", "ZL", "+", ...) for a single physical-button bit (must be exactly one bit set,
// in the same raw HidNpadButton_* numbering GetMenuActionButtons()/MenuInput::raw_held use) - ""
// if the bit is unrecognized. Used to render binding chips without hardcoding button names.
const char* RawButtonBitLabel(std::uint64_t single_bit);

// The short label of the lowest-numbered button bound to `action`, "" if it's entirely unbound.
// Used so hint-bar chips always reflect the user's actual UI Controls rebinds instead of a
// hardcoded default letter.
const char* PrimaryButtonLabel(MenuAction action);

} // namespace SwitchFrontend
