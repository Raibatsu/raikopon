// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_paths.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <INIReader.h>

#include "citra_switch/config.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"

#include <fmt/format.h>

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;
constexpr float kDescriptionHeight = 32.0f;
constexpr float kDescriptionGap = 8.0f;
constexpr float kBrowseRowHeight = 44.0f;
constexpr float kBrowseRowPadding = 4.0f;
constexpr float kBrowseRowIconSize = 28.0f;
constexpr float kBrowsePanelMarginX = 120.0f;
constexpr float kBrowsePanelMarginY = 60.0f;
constexpr float kBrowsePanelPadding = 24.0f;
constexpr float kBrowseOutlineWidth = 2.0f;

FileUtil::UserPath UserPathFor(PathsBrowseTarget target) {
    switch (target) {
    case PathsBrowseTarget::Cheats:
        return FileUtil::UserPath::CheatsDir;
    case PathsBrowseTarget::Mods:
        return FileUtil::UserPath::LoadDir;
    default:
        return FileUtil::UserPath::SDMCDir;
    }
}

std::string PathsSettingsFile() {
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "paths_settings.ini";
}

struct PathOverrides {
    std::string sdmc_dir;
    std::string cheats_dir;
    std::string mods_dir;
    // Persisted alongside the folder overrides above but independent of them - toggleable even
    // while a target is still at its default folder. Library's feeds SwitchPaths::scan_recursive,
    // which ScanDirectory() genuinely walks. Cheats/Mods read fixed, non-recursive paths in core
    // code (e.g. cheats.cpp's SaveCheatFile builds an exact "<title_id>.txt" path, no directory
    // walk at all) - rather than touch that shared core code, ApplyRecursivePathsForTitle() below
    // searches for the title's subfolder ourselves and briefly redirects CheatsDir/LoadDir to it
    // for the session, so core's fixed-path join still lands on the right file. SD Card has no
    // subfolder concept (it's a single mount point, not a per-title lookup) - this toggle is
    // stored for it but there is nothing for it to change.
    bool sdmc_recursive = true;
    bool cheats_recursive = true;
    bool mods_recursive = true;
};

bool s_overrides_loaded = false;
PathOverrides s_overrides;

// Captured by ApplyRecursivePathsForTitle() right before it (maybe) redirects CheatsDir/LoadDir,
// so RestorePathsAfterGame() can put back exactly what was active - whether that was the true
// engine default or a manually-picked Paths tab override. Empty when no session is mid-redirect.
std::string s_base_cheats_dir;
std::string s_base_mods_dir;

bool IsHiddenName(const std::string& name) {
    return name.empty() || name.front() == '.';
}

// Depth-capped search for `dir`'s own or a nested subfolder containing this title's cheat file.
// Checks `dir` itself first (the common case - no subfolders in use, or the file already sits at
// the top level) before descending, so a typical setup pays only one stat call.
bool FindCheatsRoot(const std::string& dir, std::uint64_t title_id, int depth, std::string& out_root) {
    if (FileUtil::Exists(fmt::format("{}{:016X}.txt", dir, title_id))) {
        out_root = dir;
        return true;
    }
    if (depth >= 4) {
        return false;
    }
    bool found = false;
    FileUtil::ForeachDirectoryEntry(
        nullptr, dir, [&](u64*, const std::string& parent, const std::string& name) {
            if (found || IsHiddenName(name)) {
                return !found;
            }
            const std::string path = parent + name + "/";
            if (FileUtil::IsDirectory(path) && FindCheatsRoot(path, title_id, depth + 1, out_root)) {
                found = true;
                return false;
            }
            return true;
        });
    return found;
}

// Same idea for mods: looks for a "mods/{title_id}/" pair anywhere under `dir`, not just directly
// under it - `out_root` becomes whatever should stand in for LoadDir so that its own fixed
// "mods/{title_id}/" suffix (appended by core) lands on the match.
bool FindModsRoot(const std::string& dir, std::uint64_t title_id, int depth, std::string& out_root) {
    if (FileUtil::IsDirectory(fmt::format("{}mods/{:016X}/", dir, title_id))) {
        out_root = dir;
        return true;
    }
    if (depth >= 4) {
        return false;
    }
    bool found = false;
    FileUtil::ForeachDirectoryEntry(
        nullptr, dir, [&](u64*, const std::string& parent, const std::string& name) {
            if (found || IsHiddenName(name)) {
                return !found;
            }
            const std::string path = parent + name + "/";
            if (FileUtil::IsDirectory(path) && FindModsRoot(path, title_id, depth + 1, out_root)) {
                found = true;
                return false;
            }
            return true;
        });
    return found;
}

std::string& OverrideRef(PathOverrides& overrides, PathsBrowseTarget target) {
    switch (target) {
    case PathsBrowseTarget::Cheats:
        return overrides.cheats_dir;
    case PathsBrowseTarget::Mods:
        return overrides.mods_dir;
    default:
        return overrides.sdmc_dir;
    }
}

void LoadOverrides() {
    s_overrides_loaded = true;
    s_overrides = {};
    const std::string path = PathsSettingsFile();
    if (!FileUtil::Exists(path)) {
        return;
    }
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, path, buffer)) {
        return;
    }
    INIReader ini{buffer.c_str(), buffer.size()};
    if (ini.ParseError() < 0) {
        LOG_ERROR(Config, "Malformed paths settings file '{}'", path);
        return;
    }
    s_overrides.sdmc_dir = ini.Get("Paths", "sdmc_dir", "");
    s_overrides.cheats_dir = ini.Get("Paths", "cheats_dir", "");
    s_overrides.mods_dir = ini.Get("Paths", "mods_dir", "");
    s_overrides.sdmc_recursive = ini.GetBoolean("Paths", "sdmc_recursive", true);
    s_overrides.cheats_recursive = ini.GetBoolean("Paths", "cheats_recursive", true);
    s_overrides.mods_recursive = ini.GetBoolean("Paths", "mods_recursive", true);
}

PathOverrides& GetOverrides() {
    if (!s_overrides_loaded) {
        LoadOverrides();
    }
    return s_overrides;
}

// The LoadDir-equivalent whose fixed "mods/{title_id}/" suffix (appended by core) resolves to
// this title's actual mods folder - the current LoadDir override itself, unless the Mods
// "include subfolders" toggle is on and the title's mods/ folder is actually nested somewhere
// under it (see FindModsRoot above).
std::string ResolveModsLoadDirForTitle(std::uint64_t title_id) {
    const PathOverrides& overrides = GetOverrides();
    const std::string base = FileUtil::GetUserPath(FileUtil::UserPath::LoadDir);
    if (!overrides.mods_recursive) {
        return base;
    }
    std::string root;
    return FindModsRoot(base, title_id, 0, root) ? root : base;
}

std::string ModTogglesFile(std::uint64_t title_id) {
    return fmt::format("{}mod_toggles/{:016X}.txt",
                       FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir), title_id);
}

std::string ModMergeRoot() {
    return FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + "mod_merge/";
}

// Merges each of `enabled_mods` (subfolders of `physical_mods_dir`, in priority order - earlier
// entries win file conflicts, matching FileUtil::CopyDir's own "skip if already present" rule)
// into a cached staging copy under CacheDir, reusing it unchanged if the exact enabled set
// already matches what was merged there last time (the common case - most boots don't follow a
// toggle change). Returns the LoadDir-equivalent to use for this boot so core's fixed
// "mods/{title_id}/" lookup resolves to the merged result.
std::string BuildModMergeStaging(std::uint64_t title_id, const std::string& physical_mods_dir,
                                 const std::vector<std::string>& enabled_mods) {
    const std::string merge_root = ModMergeRoot();
    const std::string staging_dir = fmt::format("{}mods/{:016X}/", merge_root, title_id);
    const std::string marker_path = staging_dir + ".merged_mods";

    std::string signature;
    for (const std::string& name : enabled_mods) {
        signature += name + "\n";
    }

    std::string existing_marker;
    if (FileUtil::IsDirectory(staging_dir) &&
        FileUtil::ReadFileToString(true, marker_path, existing_marker) &&
        existing_marker == signature) {
        return merge_root;
    }

    FileUtil::DeleteDirRecursively(staging_dir);
    FileUtil::CreateFullPath(staging_dir);
    for (const std::string& name : enabled_mods) {
        FileUtil::CopyDir(physical_mods_dir + name + "/", staging_dir);
    }
    FileUtil::WriteStringToFile(true, marker_path, signature);
    return merge_root;
}

void SaveOverrides() {
    const std::string path = PathsSettingsFile();
    const bool all_default = s_overrides.sdmc_dir.empty() && s_overrides.cheats_dir.empty() &&
                             s_overrides.mods_dir.empty() && s_overrides.sdmc_recursive &&
                             s_overrides.cheats_recursive && s_overrides.mods_recursive;
    if (all_default) {
        FileUtil::Delete(path);
        return;
    }
    FileUtil::CreateFullPath(path);
    std::string contents = "[Paths]\n";
    if (!s_overrides.sdmc_dir.empty()) {
        contents += "sdmc_dir = " + s_overrides.sdmc_dir + "\n";
    }
    if (!s_overrides.cheats_dir.empty()) {
        contents += "cheats_dir = " + s_overrides.cheats_dir + "\n";
    }
    if (!s_overrides.mods_dir.empty()) {
        contents += "mods_dir = " + s_overrides.mods_dir + "\n";
    }
    contents += std::string("sdmc_recursive = ") + (s_overrides.sdmc_recursive ? "true" : "false") + "\n";
    contents += std::string("cheats_recursive = ") + (s_overrides.cheats_recursive ? "true" : "false") + "\n";
    contents += std::string("mods_recursive = ") + (s_overrides.mods_recursive ? "true" : "false") + "\n";
    if (!FileUtil::WriteStringToFile(true, path, contents)) {
        LOG_ERROR(Config, "Failed to save paths settings to '{}'", path);
    }
}

void SetOverride(PathsBrowseTarget target, const std::string& dir) {
    PathOverrides& overrides = GetOverrides();
    OverrideRef(overrides, target) = dir;
    SaveOverrides();
}

void ApplyOverrideDir(PathsBrowseTarget target, const std::string& dir) {
    FileUtil::CreateFullPath(dir);
    FileUtil::UpdateUserPath(UserPathFor(target), dir);
}

void ResetTarget(UiModel& model, PathsBrowseTarget target) {
    if (target == PathsBrowseTarget::Library) {
        SwitchPaths paths = GetPaths();
        paths.roms_dir = GetDefaultRomsDir();
        paths.scan_recursive = true;
        SetPaths(paths);
        model.games_scanned = false;
        return;
    }
    const FileUtil::UserPath user_path = UserPathFor(target);
    FileUtil::UpdateUserPath(user_path, FileUtil::GetDefaultUserPath(user_path));
    PathOverrides& overrides = GetOverrides();
    OverrideRef(overrides, target).clear();
    switch (target) {
    case PathsBrowseTarget::Cheats:
        overrides.cheats_recursive = true;
        break;
    case PathsBrowseTarget::Mods:
        overrides.mods_recursive = true;
        break;
    default:
        overrides.sdmc_recursive = true;
        break;
    }
    SaveOverrides();
}

bool IncludeSubfoldersFor(const PathOverrides& overrides, PathsBrowseTarget target) {
    switch (target) {
    case PathsBrowseTarget::Library:
        return GetPaths().scan_recursive;
    case PathsBrowseTarget::Cheats:
        return overrides.cheats_recursive;
    case PathsBrowseTarget::Mods:
        return overrides.mods_recursive;
    default:
        return overrides.sdmc_recursive;
    }
}

void ToggleIncludeSubfolders(UiModel& model, PathsBrowseTarget target) {
    if (target == PathsBrowseTarget::Library) {
        SwitchPaths paths = GetPaths();
        paths.scan_recursive = !paths.scan_recursive;
        SetPaths(paths);
        model.games_scanned = false;
        return;
    }
    PathOverrides& overrides = GetOverrides();
    switch (target) {
    case PathsBrowseTarget::Cheats:
        overrides.cheats_recursive = !overrides.cheats_recursive;
        break;
    case PathsBrowseTarget::Mods:
        overrides.mods_recursive = !overrides.mods_recursive;
        break;
    default:
        overrides.sdmc_recursive = !overrides.sdmc_recursive;
        break;
    }
    SaveOverrides();
}

std::string ValueForTarget(const PathOverrides& overrides, PathsBrowseTarget target) {
    if (target == PathsBrowseTarget::Library) {
        return GetPaths().roms_dir;
    }
    const std::string& override_dir =
        target == PathsBrowseTarget::Cheats
            ? overrides.cheats_dir
            : (target == PathsBrowseTarget::Mods ? overrides.mods_dir : overrides.sdmc_dir);
    return override_dir.empty() ? Tr("paths.default_label")
                                : FileUtil::GetUserPath(UserPathFor(target));
}

std::string StartDirForTarget(const PathOverrides& overrides, PathsBrowseTarget target) {
    if (target == PathsBrowseTarget::Library) {
        return GetPaths().roms_dir;
    }
    return FileUtil::GetUserPath(UserPathFor(target));
}

void DrawScrim(GpuCanvas& canvas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});
}

bool HasParentDir(const UiModel& model) {
    return !ParentDirectory(model.paths_browse_dir).empty();
}

void RefreshBrowseListing(UiModel& model) {
    model.paths_browse_dirs = ListSubdirectories(model.paths_browse_dir);
    model.paths_browse_listed = true;
    model.paths_browse_selected = 0;
}

void ApplyChosenFolder(UiModel& model) {
    const std::string chosen = model.paths_browse_dir;
    if (model.paths_browse_target == PathsBrowseTarget::Library) {
        SwitchPaths paths = GetPaths();
        paths.roms_dir = chosen;
        SetPaths(paths);
        model.games_scanned = false;
    } else {
        ApplyOverrideDir(model.paths_browse_target, chosen);
        SetOverride(model.paths_browse_target, chosen);
    }
    model.paths_browse_open = false;
    model.paths_browse_listed = false;
}

void UpdateFolderBrowse(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    DrawScrim(canvas);

    if (!model.paths_browse_listed) {
        RefreshBrowseListing(model);
    }

    const bool has_parent = HasParentDir(model);
    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.open")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Minus), Tr("paths.select_folder")};
    model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Plus), Tr("hint.close")};
    model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::Cancel),
                           has_parent ? Tr("hint.up") : Tr("hint.close")};
    model.hint_chip_count = 4;

    const float panel_x = kBrowsePanelMarginX;
    const float panel_y = kBrowsePanelMarginY;
    const float panel_w = screen_w - kBrowsePanelMarginX * 2.0f;
    const float panel_h = screen_h - kBrowsePanelMarginY * 2.0f;
    canvas.DrawQuad(panel_x - kBrowseOutlineWidth, panel_y - kBrowseOutlineWidth,
                    panel_w + kBrowseOutlineWidth * 2.0f, panel_h + kBrowseOutlineWidth * 2.0f,
                    palette.accent);
    canvas.DrawQuad(panel_x, panel_y, panel_w, panel_h, palette.surface);

    const float content_x = panel_x + kBrowsePanelPadding;
    float y = panel_y + kBrowsePanelPadding;
    DrawText(canvas, atlas, content_x, y, Tr("paths.select_folder"), palette.text);
    y += atlas.LineHeight() + 4.0f;
    DrawText(canvas, atlas, content_x, y, model.paths_browse_dir, palette.text_dim);
    y += atlas.LineHeight() + kBrowsePanelPadding * 0.5f;

    const float list_top = y;
    const float list_bottom = panel_y + panel_h - kBrowsePanelPadding;
    const float row_w = panel_w - kBrowsePanelPadding * 2.0f;

    const int dirs_offset = has_parent ? 1 : 0;
    const int item_count = dirs_offset + static_cast<int>(model.paths_browse_dirs.size());

    if (item_count == 0) {
        DrawText(canvas, atlas, content_x, list_top, Tr("paths.browse_empty"), palette.text_dim);
    }

    if (model.paths_browse_selected >= item_count) {
        model.paths_browse_selected = item_count > 0 ? item_count - 1 : 0;
    }

    if (input.up && model.paths_browse_selected > 0) {
        --model.paths_browse_selected;
    }
    if (input.down && model.paths_browse_selected + 1 < item_count) {
        ++model.paths_browse_selected;
    }

    const float selected_top = list_top + static_cast<float>(model.paths_browse_selected) *
                                              (kBrowseRowHeight + kBrowseRowPadding);
    const float content_height =
        static_cast<float>(item_count) * (kBrowseRowHeight + kBrowseRowPadding);
    UpdateScroll(model.paths_browse_scroll, input, model.paths_browse_selected, selected_top,
                kBrowseRowHeight * kSelectedGrowth, content_x, list_top, row_w,
                list_bottom - list_top, content_height - (list_bottom - list_top));
    const float scroll_offset = model.paths_browse_scroll.offset;

    canvas.SetClipRect(content_x, list_top, row_w, list_bottom - list_top);

    const AtlasRegion folder_icon = GetIconRegion(Icon::Folder);
    const AtlasRegion updir_icon = GetIconRegion(Icon::UpDir);

    int tapped_item = -1;
    float row_y = list_top - scroll_offset;
    for (int i = 0; i < item_count; ++i) {
        const bool selected = i == model.paths_browse_selected;
        const float row_h = selected ? kBrowseRowHeight * kSelectedGrowth : kBrowseRowHeight;

        if (row_y + row_h >= list_top && row_y <= list_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }
            if (input.touch_tap && input.touch_x >= content_x && input.touch_x < content_x + row_w &&
                input.touch_y >= row_y && input.touch_y < row_y + row_h) {
                tapped_item = i;
            }

            const bool is_updir = has_parent && i == 0;
            const AtlasRegion& icon = is_updir ? updir_icon : folder_icon;
            const CanvasColor icon_color = selected ? palette.accent : palette.text_dim;
            if (icon.u1 > icon.u0) {
                const float icon_y = row_y + (row_h - kBrowseRowIconSize) * 0.5f;
                canvas.DrawTexturedQuad(content_x + 8.0f, icon_y, kBrowseRowIconSize,
                                        kBrowseRowIconSize, icon.u0, icon.v0, icon.u1, icon.v1,
                                        icon_color);
            }

            const std::string label =
                is_updir ? ".."
                        : model.paths_browse_dirs[static_cast<std::size_t>(i - dirs_offset)].name;
            const float text_x = content_x + 8.0f + kBrowseRowIconSize + 10.0f;
            DrawText(canvas, atlas, text_x, row_y + (row_h - atlas.LineHeight()) * 0.5f, label,
                    selected ? palette.text : palette.text_dim);
        }

        row_y += row_h + kBrowseRowPadding;
    }
    canvas.ClearClipRect();

    int activated = -1;
    if (input.confirm && item_count > 0) {
        activated = model.paths_browse_selected;
    }
    if (tapped_item >= 0) {
        model.paths_browse_selected = tapped_item;
        activated = tapped_item;
    }

    if (activated >= 0) {
        if (has_parent && activated == 0) {
            model.paths_browse_dir = ParentDirectory(model.paths_browse_dir);
            model.paths_browse_listed = false;
        } else {
            model.paths_browse_dir =
                model.paths_browse_dirs[static_cast<std::size_t>(activated - dirs_offset)].path;
            model.paths_browse_listed = false;
        }
        return;
    }

    if (input.minus) {
        ApplyChosenFolder(model);
        return;
    }

    // Always closes the browser outright, regardless of how many levels deep the user has
    // navigated - Cancel below still only steps up one level at a time, which was the whole
    // complaint this button exists to fix.
    if (input.plus) {
        model.paths_browse_open = false;
        model.paths_browse_listed = false;
        return;
    }

    if (input.cancel) {
        if (has_parent) {
            model.paths_browse_dir = ParentDirectory(model.paths_browse_dir);
            model.paths_browse_listed = false;
        } else {
            model.paths_browse_open = false;
            model.paths_browse_listed = false;
        }
    }
}

struct PathsRow {
    PathsBrowseTarget target;
    std::string label;
    std::string value;
    const char* description_key;
    bool include_subfolders;
};

std::vector<PathsRow> BuildPathsRows(const PathOverrides& overrides) {
    return {
        {PathsBrowseTarget::Library, Tr("paths.library_folder"),
         ValueForTarget(overrides, PathsBrowseTarget::Library), "paths.library_folder.desc",
         IncludeSubfoldersFor(overrides, PathsBrowseTarget::Library)},
        {PathsBrowseTarget::SdCard, Tr("paths.sdmc_folder"),
         ValueForTarget(overrides, PathsBrowseTarget::SdCard), "paths.sdmc_folder.desc",
         IncludeSubfoldersFor(overrides, PathsBrowseTarget::SdCard)},
        {PathsBrowseTarget::Cheats, Tr("paths.cheats_folder"),
         ValueForTarget(overrides, PathsBrowseTarget::Cheats), "paths.cheats_folder.desc",
         IncludeSubfoldersFor(overrides, PathsBrowseTarget::Cheats)},
        {PathsBrowseTarget::Mods, Tr("paths.mods_folder"),
         ValueForTarget(overrides, PathsBrowseTarget::Mods), "paths.mods_folder.desc",
         IncludeSubfoldersFor(overrides, PathsBrowseTarget::Mods)},
    };
}

} // namespace

void UpdatePathsTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                    float dt, float content_x, float content_top, float content_w,
                    float viewport_bottom) {
    (void)dt;
    const UiPalette& palette = CurrentPalette();

    if (model.paths_browse_open) {
        UpdateFolderBrowse(model, input, canvas, atlas);
        return;
    }

    const PathOverrides& overrides = GetOverrides();
    const std::vector<PathsRow> rows = BuildPathsRows(overrides);
    const int row_count = static_cast<int>(rows.size());

    model.paths_row = std::clamp(model.paths_row, 0, row_count - 1);

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.open")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Minus), Tr("paths.reset_row")};
    model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Plus), Tr("paths.toggle_subfolders")};
    model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::TabPrev), Tr("hint.prev_tab")};
    model.hint_chips[4] = {PrimaryButtonLabel(MenuAction::TabNext), Tr("hint.next_tab")};
    model.hint_chips[5] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
    model.hint_chip_count = 6;

    if (input.up && model.paths_row > 0) {
        --model.paths_row;
    }
    if (input.down && model.paths_row + 1 < row_count) {
        ++model.paths_row;
    }

    const float row_viewport_bottom = viewport_bottom - kDescriptionHeight - kDescriptionGap;

    int activated = -1;
    if (input.confirm) {
        activated = model.paths_row;
    }
    if (input.minus) {
        ResetTarget(model, rows[static_cast<std::size_t>(model.paths_row)].target);
    }
    if (input.plus) {
        ToggleIncludeSubfolders(model, rows[static_cast<std::size_t>(model.paths_row)].target);
    }

    int tapped_row = -1;
    float y = content_top;
    for (int i = 0; i < row_count; ++i) {
        const PathsRow& row = rows[static_cast<std::size_t>(i)];
        const bool selected = i == model.paths_row;
        const float row_h = selected ? kRowHeight * kSelectedGrowth : kRowHeight;
        if (y + row_h < content_top || y > row_viewport_bottom) {
            y += row_h + kRowPadding;
            continue;
        }
        if (selected) {
            canvas.DrawQuad(content_x, y, content_w, row_h, palette.surface_warm);
        }
        if (input.touch_tap && input.touch_x >= content_x && input.touch_x < content_x + content_w &&
            input.touch_y >= y && input.touch_y < y + row_h) {
            tapped_row = i;
        }

        const float label_y = y + (row_h - atlas.LineHeight()) * 0.5f;
        DrawText(canvas, atlas, content_x + kContentPadding, label_y, row.label,
                selected ? palette.text : palette.text_dim);
        const float value_width = MeasureText(atlas, canvas, row.value);
        DrawText(canvas, atlas, content_x + content_w - kContentPadding - value_width, label_y,
                row.value, selected ? palette.accent : palette.text_dim);

        y += row_h + kRowPadding;
    }

    const PathsRow& selected_row = rows[static_cast<std::size_t>(model.paths_row)];
    const std::string description =
        std::string("[") +
        (selected_row.include_subfolders ? Tr("paths.subfolders_on") : Tr("paths.subfolders_off")) +
        "] " + Tr(selected_row.description_key);
    canvas.DrawQuad(content_x, row_viewport_bottom + kDescriptionGap, content_w, kDescriptionHeight,
                    palette.surface);
    canvas.SetClipRect(content_x, row_viewport_bottom + kDescriptionGap, content_w,
                       kDescriptionHeight);
    DrawText(canvas, atlas, content_x, row_viewport_bottom + kDescriptionGap +
                                          (kDescriptionHeight - atlas.LineHeight()) * 0.5f,
            description, palette.text_dim);
    canvas.ClearClipRect();

    if (tapped_row >= 0) {
        model.paths_row = tapped_row;
        activated = tapped_row;
    }

    if (activated >= 0) {
        const PathsRow& row = rows[static_cast<std::size_t>(activated)];
        model.paths_browse_target = row.target;
        model.paths_browse_dir = StartDirForTarget(overrides, row.target);
        model.paths_browse_open = true;
        model.paths_browse_listed = false;
    }
}

void ApplySavedStorageOverride() {
    const PathOverrides& overrides = GetOverrides();
    if (!overrides.sdmc_dir.empty() && FileUtil::IsDirectory(overrides.sdmc_dir)) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::SDMCDir, overrides.sdmc_dir);
    }
    if (!overrides.cheats_dir.empty() && FileUtil::IsDirectory(overrides.cheats_dir)) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::CheatsDir, overrides.cheats_dir);
    }
    if (!overrides.mods_dir.empty() && FileUtil::IsDirectory(overrides.mods_dir)) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::LoadDir, overrides.mods_dir);
    }
}

void ApplyRecursivePathsForTitle(std::uint64_t title_id) {
    const PathOverrides& overrides = GetOverrides();

    s_base_cheats_dir = FileUtil::GetUserPath(FileUtil::UserPath::CheatsDir);
    if (overrides.cheats_recursive) {
        std::string root;
        if (FindCheatsRoot(s_base_cheats_dir, title_id, 0, root) && root != s_base_cheats_dir) {
            FileUtil::UpdateUserPath(FileUtil::UserPath::CheatsDir, root);
        }
    }

    s_base_mods_dir = FileUtil::GetUserPath(FileUtil::UserPath::LoadDir);
    const std::string mods_base = ResolveModsLoadDirForTitle(title_id);
    const std::string physical_mods_dir = fmt::format("{}mods/{:016X}/", mods_base, title_id);
    std::vector<std::string> mod_names;
    for (const DirEntry& entry : ListSubdirectories(physical_mods_dir)) {
        mod_names.push_back(entry.name);
    }
    if (!mod_names.empty()) {
        const std::vector<std::string> disabled = GetDisabledMods(title_id);
        std::vector<std::string> enabled;
        for (const std::string& name : mod_names) {
            if (std::find(disabled.begin(), disabled.end(), name) == disabled.end()) {
                enabled.push_back(name);
            }
        }
        FileUtil::UpdateUserPath(FileUtil::UserPath::LoadDir,
                                 BuildModMergeStaging(title_id, physical_mods_dir, enabled));
    } else if (mods_base != s_base_mods_dir) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::LoadDir, mods_base);
    }
}

void RestorePathsAfterGame() {
    if (!s_base_cheats_dir.empty()) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::CheatsDir, s_base_cheats_dir);
        s_base_cheats_dir.clear();
    }
    if (!s_base_mods_dir.empty()) {
        FileUtil::UpdateUserPath(FileUtil::UserPath::LoadDir, s_base_mods_dir);
        s_base_mods_dir.clear();
    }
}

std::vector<std::string> DiscoverTitleMods(std::uint64_t title_id) {
    const std::string mods_base = ResolveModsLoadDirForTitle(title_id);
    const std::string physical_mods_dir = fmt::format("{}mods/{:016X}/", mods_base, title_id);
    std::vector<std::string> names;
    for (const DirEntry& entry : ListSubdirectories(physical_mods_dir)) {
        names.push_back(entry.name);
    }
    return names;
}

std::vector<std::string> GetDisabledMods(std::uint64_t title_id) {
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, ModTogglesFile(title_id), buffer)) {
        return {};
    }
    std::vector<std::string> disabled;
    for (const std::string& raw_line : Common::SplitString(buffer, '\n')) {
        const std::string line = Common::StripSpaces(raw_line);
        if (!line.empty()) {
            disabled.push_back(line);
        }
    }
    return disabled;
}

void SetDisabledMods(std::uint64_t title_id, const std::vector<std::string>& disabled) {
    const std::string path = ModTogglesFile(title_id);
    if (disabled.empty()) {
        FileUtil::Delete(path);
        return;
    }
    FileUtil::CreateFullPath(path);
    std::string contents;
    for (const std::string& name : disabled) {
        contents += name + "\n";
    }
    if (!FileUtil::WriteStringToFile(true, path, contents)) {
        LOG_ERROR(Config, "Failed to save mod toggles to '{}'", path);
    }
}

} // namespace SwitchFrontend
