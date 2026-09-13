// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "citra_switch/input.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

enum class AppState {
    Splash,
    Library,
    Booting,
    InGame,
    Paused,
    Exiting,
};

enum class RailItem {
    Library,
    Settings,
    Credits,
    Exit,
};

inline constexpr int kNumRailItems = static_cast<int>(RailItem::Exit) + 1;

enum class LibraryPanel {
    List,
    Details,
};

enum class LibraryFocus {
    Rail,
    List,
};

enum class PathsBrowseTarget {
    Library,
    SdCard,
    Cheats,
    Mods,
};

// None: no popup showing. ResetToDefault: confirms the "Reset" hint chip's app-wide reset.
// RestartRequired: confirms a boolean/cyclable row whose new value only takes effect after a
// restart - since the frontend can't restart itself, "Yes" here exits so the user can relaunch.
// ResetControls: confirms the Controls tab's own Minus-bound reset, scoped to just the In-Game
// and UI Control bindings rather than every setting.
enum class SettingsConfirmKind {
    None,
    ResetToDefault,
    RestartRequired,
    ResetControls,
};

struct HintChip {
    const char* button = "";
    // std::string, not const char*, since most call sites now set this to a Tr() result (see
    // ui_strings.h) - a plain string literal still works fine via std::string's implicit
    // const char* constructor for the handful of call sites not yet localized.
    std::string label;
};

struct ScrollState {
    float offset = 0.0f;
    bool dragging = false;
    int last_selection = -1;
};

struct MenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
    bool cancel = false;
    bool tab_prev = false;
    bool tab_next = false;
    bool minus = false;
    bool plus = false;
    bool rail_prev = false;
    bool rail_next = false;
    bool reset_default = false;

    bool touch_tap = false;
    float touch_x = 0.0f;
    float touch_y = 0.0f;

    bool touch_held = false;
    bool touch_press_edge = false;
    float touch_held_x = 0.0f;
    float touch_held_y = 0.0f;
    float touch_delta_y = 0.0f;

    // Unfiltered physical-button state, in raw HidNpadButton_* bit positions - bypasses whatever
    // the fields above are currently remapped to. Only the Controls tab's rebind-capture flow
    // reads these; every other screen uses the (remappable) fields above instead.
    std::uint64_t raw_held = 0;
    std::uint64_t raw_pressed = 0;
    // The first newly-pressed physical button this frame, already translated to the frontend's
    // own InputButton enum (see citra_switch.cpp's button_map) - InputButton::None if none.
    InputButton raw_pressed_button = InputButton::None;
};

struct UiModel {
    AppState app_state = AppState::Splash;
    float splash_elapsed = 0.0f;
    float wave_elapsed = 0.0f;
    // Eased pop-out amount for the rail's active-item bookmark tab, animating between 0
    // (retracted) and ui_theme.h's kRailPopWidth (fully popped) - starts fully popped so a rail
    // that boots unfocused visibly retracts rather than starting there with no animation to see.
    // The rail itself never changes width; only this tab extension does (see DrawNavBarActiveTab).
    float rail_pop_anim = kRailPopWidth;
    // Whether the rail should be expanded (icon+label) right now - shared between Library (synced
    // every frame from its own `focus` below, which also drives its Up/Down-vs-list input
    // routing) and Settings (which has no separate focus concept of its own, so this is the only
    // thing driving its rail state), so the expanded/focused look carries seamlessly across a
    // rail-driven Library<->Settings screen switch.
    bool rail_focused = false;

    RailItem active_rail = RailItem::Library;
    LibraryFocus focus = LibraryFocus::List;

    bool page_transition_active = false;
    float page_transition_t = 0.0f;
    RailItem page_transition_from = RailItem::Library;
    RailItem page_transition_to = RailItem::Library;
    std::array<HintChip, 6> page_transition_hint_chips{};
    int page_transition_hint_chip_count = 0;

    std::vector<GameEntry> games;
    bool games_scanned = false;
    // Separate from games_scanned - see ui_library.cpp's UpdateLibrary for why icon reloading
    // needs its own trigger instead of piggybacking on the scan-once flag.
    bool game_icons_loaded = false;
    int selected_game = 0;
    ScrollState library_scroll{};

    LibraryPanel library_panel = LibraryPanel::List;
    int details_action = 0;
    ScrollState game_details_scroll{};
    // Whether Clear Cache/Cheats/Mods (everything below Edit Screen Layout) are expanded - starts
    // collapsed so the action list doesn't crowd out the cover art/description for the common
    // case where nobody needs them. Not per-game; carries across whichever title is being viewed.
    bool details_actions_expanded = false;

    std::string pending_rom;
    std::uint64_t pending_program_id = 0;
    bool exit_requested = false;

    std::string notice_text;
    bool notice_is_error = false;

    bool settings_loaded = false;
    MenuSettings settings{};
    MenuSettings settings_defaults{};
    int settings_tab_index = 0;
    int settings_row = 0;
    bool settings_row_armed = false;
    int settings_gyro_axis = 0;
    // Which row's description the auto-scroll marquee is currently animating - reset (and the
    // scroll restarted) whenever settings_row no longer matches this.
    int settings_hint_row = -1;
    float settings_hint_scroll = 0.0f;
    ScrollState settings_scroll{};
    // Eased 0..1 grow-in progress for the currently-selected row - reset to 0 whenever
    // settings_row changes so the newly-selected row animates into its highlighted/grown state
    // instead of popping. `_last` is what settings_row was compared against last frame.
    float settings_row_focus_t = 1.0f;
    int settings_row_focus_last = -1;
    // Eased 0..1 slide-in progress for the row list after a tab switch - reset to 0 (and `_dir`
    // set to the direction just moved) whenever settings_tab_index changes.
    float settings_tab_reveal_t = 1.0f;
    int settings_tab_reveal_dir = 0;

    // Modal confirmation popup (Reset to Default / restart-required row changes). `snapshot` is
    // the revert target on "No" - for RestartRequired it's taken right before the row's new value
    // is (already live-)applied, since ResetToDefault never pre-applies anything to revert.
    // `selected` defaults to 1 (No) for both kinds, matching the safe default a stray press lands on.
    SettingsConfirmKind settings_confirm_kind = SettingsConfirmKind::None;
    std::string settings_confirm_message;
    MenuSettings settings_confirm_snapshot{};
    int settings_confirm_selected = 1;

    // Modal progress popup for the "Download All Covers" row - mirrors ui_install.cpp's install
    // popup visually, but non-blocking (the actual download runs on gametdb.cpp's own background
    // thread; this just polls its progress each frame). Dismissing early (Cancel) only hides the
    // popup - the download keeps running and re-selecting the row reopens it.
    bool cover_download_open = false;

    bool layout_cycle_picker_open = false;
    int layout_cycle_picker_row = 0;

    bool install_open = false;
    bool install_listed = false;
    std::string install_dir;
    std::vector<DirEntry> install_dirs;
    std::vector<CiaEntry> install_files;
    int install_selected = 0;
    std::string pending_install_path;
    ScrollState install_scroll{};

    // Per-game settings, opened from Game Details - see ui_gamesettings.cpp. `before`/`current`
    // bookend a BeginGameOverrides(program_id) session so CommitMenuSettingsPerGame can diff them
    // on close; only guaranteed meaningful while game_settings_open is true.
    bool game_settings_open = false;
    std::uint64_t game_settings_program_id = 0;
    std::string game_settings_title;
    MenuSettings game_settings_before{};
    MenuSettings game_settings_current{};
    int game_settings_row = 0;
    bool game_settings_row_armed = false;
    ScrollState game_settings_scroll{};
    float game_settings_row_focus_t = 1.0f;
    int game_settings_row_focus_last = -1;
    // Eased 0..1 slide-in progress for the whole screen, reset to 0 whenever the screen transitions
    // from closed to open (tracked via `_was_open`, checked each frame at the top of
    // UpdateGameSettings since nothing else marks "just opened" explicitly).
    bool game_settings_was_open = false;
    float game_settings_open_t = 1.0f;

    bool layout_editor_open = false;

    // Cheats screen, opened from Game Details - see ui_cheats.cpp. File-based (library_cheats.h),
    // not the live-CheatEngine-backed ingame_cheats.h the in-game quick menu uses, since no
    // Core::System is running while browsing the library.
    bool cheats_open = false;
    std::uint64_t cheats_program_id = 0;
    std::string cheats_title;
    int cheats_row = 0;
    ScrollState cheats_scroll{};
    // PromptKeyboard() blocks across several frames of its own (system keyboard applet) - same
    // reason ui_install.cpp's RunInstall() has to run after canvas.EndFrame() rather than inside
    // a screen's single-frame Update function. -2 = no pending edit, -1 = add new, >=0 = edit
    // that cheat index. Checked once per main-loop iteration, outside any BeginFrame/EndFrame pair.
    int cheats_pending_edit_index = -2;

    // Mods screen, opened from Game Details - see ui_mods.cpp/library_mods.h. No blocking calls
    // (no add/edit/delete, just per-mod toggles), so unlike Cheats there's no deferred-edit field.
    bool mods_open = false;
    std::uint64_t mods_program_id = 0;
    std::string mods_title;
    int mods_row = 0;
    ScrollState mods_scroll{};

    int controls_row = 0;
    ScrollState controls_scroll{};
    // Armed state for the Touch & Motion rows (Pointer Source/Gyro Sensitivity) - distinct from
    // controls_capturing, which is specifically the button-rebind capture flow below.
    bool controls_setting_armed = false;
    bool controls_capturing = false;
    std::uint64_t controls_capture_mask = 0;
    InputButton controls_capture_button = InputButton::None;
    float controls_capture_idle = 0.0f;

    int updates_row = 0;
    bool pending_update_install = false;
    bool update_ready_to_close = false;
    bool changelog_popup_open = false;
    int changelog_scroll_line = 0;

    int credits_row = 0;
    ScrollState credits_scroll{};

    int paths_row = 0;
    bool paths_browse_open = false;
    PathsBrowseTarget paths_browse_target = PathsBrowseTarget::Library;
    bool paths_browse_listed = false;
    std::string paths_browse_dir;
    std::vector<DirEntry> paths_browse_dirs;
    int paths_browse_selected = 0;
    ScrollState paths_browse_scroll{};

    std::array<HintChip, 6> hint_chips{};
    int hint_chip_count = 0;
};

} // namespace SwitchFrontend
