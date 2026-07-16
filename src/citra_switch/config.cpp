// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <sstream>
#include <string>
#include <INIReader.h>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "citra_switch/config.h"
#include "citra_switch/default_ini.h"
#include "core/hle/service/service.h"

namespace {

// Reads/Writes the SD-card config file
class Config {
public:
    Config() {
        config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "config.ini";
        std::string ini_buffer;
        FileUtil::ReadFileToString(true, config_loc, ini_buffer);
        if (!ini_buffer.empty()) {
            config = std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size());
        }
        Reload();
    }

    void Reload() {
        LoadINI(DefaultINI::sConfigFile);
        ReadValues();
    }

    void Save() {
        FileUtil::CreateFullPath(config_loc);
        FileUtil::WriteStringToFile(true, config_loc, BuildINI());
        LOG_INFO(Config, "Saved config to {}", config_loc);
    }

    int LaunchCount() const {
        return launch_count;
    }

private:
    std::unique_ptr<INIReader> config;
    std::string config_loc;
    int launch_count = 0;

    bool LoadINI(const std::string& default_contents, bool retry = true) {
        if (config == nullptr || config->ParseError() < 0) {
            if (retry) {
                LOG_WARNING(Config, "Failed to load {}. Creating file from defaults...", config_loc);
                FileUtil::CreateFullPath(config_loc);
                FileUtil::WriteStringToFile(true, config_loc, default_contents);
                std::string ini_buffer;
                FileUtil::ReadFileToString(true, config_loc, ini_buffer);
                config = std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size());
                return LoadINI(default_contents, false);
            }
            LOG_ERROR(Config, "Failed to load config from {}", config_loc);
            return false;
        }
        LOG_INFO(Config, "Successfully loaded {}", config_loc);
        return true;
    }

    template <typename Type, bool ranged>
    void ReadSetting(const std::string& group, Settings::Setting<Type, ranged>& setting) {
        if constexpr (std::is_same_v<Type, std::string>) {
            std::string value = config->Get(group, setting.GetLabel(), setting.GetDefault());
            setting = value.empty() ? setting.GetDefault() : std::move(value);
        } else if constexpr (std::is_same_v<Type, bool>) {
            setting = config->GetBoolean(group, setting.GetLabel(), setting.GetDefault());
        } else if constexpr (std::is_floating_point_v<Type>) {
            setting =
                static_cast<Type>(config->GetReal(group, setting.GetLabel(), setting.GetDefault()));
        } else {
            setting = static_cast<Type>(config->GetInteger(
                group, setting.GetLabel(), static_cast<long>(setting.GetDefault())));
        }
    }

    void ReadValues() {
        // Core
        ReadSetting("Core", Settings::values.use_cpu_jit);
        ReadSetting("Core", Settings::values.cpu_clock_percentage);
        Settings::values.is_new_3ds =
            config->GetBoolean("Core", Settings::values.is_new_3ds.GetLabel(), false);

        // Renderer
        ReadSetting("Renderer", Settings::values.graphics_api);
        ReadSetting("Renderer", Settings::values.use_gles);
        ReadSetting("Renderer", Settings::values.resolution_factor);
        ReadSetting("Renderer", Settings::values.use_vsync);
        ReadSetting("Renderer", Settings::values.async_shader_compilation);
        ReadSetting("Renderer", Settings::values.use_disk_shader_cache);
        ReadSetting("Renderer", Settings::values.show_fps);

        // Force the PICA vertex-shader JIT off on Switch. oaknut::CodeBlock does a libnx
        // jitCreate per shader and exhausts Horizon JIT memory fairly quickly, which ends up
        // killing many games during their startup.
        Settings::values.use_shader_jit = false;

        // System
        ReadSetting("System", Settings::values.region_value);

        // Miscellaneous
        ReadSetting("Miscellaneous", Settings::values.log_filter);

        // The core expects every known service module to have an explicit setting and crashes if not.
        for (const auto& service_module : Service::service_module_map) {
            Settings::values.lle_modules.emplace(service_module.name, false);
        }

        launch_count = config->GetInteger("Switch", "launch_count", 0) + 1;
    }

    std::string BuildINI() const {
        std::ostringstream ss;
        const auto& v = Settings::values;

        ss << "[Core]\n";
        ss << "use_cpu_jit = " << (v.use_cpu_jit.GetValue() ? "true" : "false") << '\n';
        ss << "cpu_clock_percentage = " << v.cpu_clock_percentage.GetValue() << '\n';
        ss << "is_new_3ds = " << (v.is_new_3ds.GetValue() ? "true" : "false") << "\n\n";

        ss << "[Renderer]\n";
        ss << "graphics_api = " << static_cast<int>(v.graphics_api.GetValue()) << '\n';
        ss << "use_gles = " << (v.use_gles.GetValue() ? "true" : "false") << '\n';
        ss << "resolution_factor = " << v.resolution_factor.GetValue() << '\n';
        ss << "use_vsync = " << (v.use_vsync.GetValue() ? "true" : "false") << '\n';
        ss << "async_shader_compilation = "
           << (v.async_shader_compilation.GetValue() ? "true" : "false") << '\n';
        ss << "use_disk_shader_cache = " << (v.use_disk_shader_cache.GetValue() ? "true" : "false")
           << '\n';
        ss << "show_fps = " << (v.show_fps.GetValue() ? "true" : "false") << "\n\n";

        ss << "[System]\n";
        ss << "region_value = " << v.region_value.GetValue() << "\n\n";

        ss << "[Miscellaneous]\n";
        ss << "log_filter = " << v.log_filter.GetValue() << "\n\n";

        ss << "[Switch]\n";
        ss << "launch_count = " << launch_count << '\n';

        return ss.str();
    }
};

// Kept alive past Bootstrap() so the menu can re-save settings while preserving launch_count.
std::unique_ptr<Config> s_config;

} // namespace

namespace SwitchFrontend {

int Bootstrap() {
    // Resolve sdmc:/switch/dekopon/ and create its standard subdirectories.
    FileUtil::SetUserPath();

    Common::Log::Initialize();
    Common::Log::Start();

    s_config = std::make_unique<Config>();

    // Apply the log filter the config just loaded.
    Common::Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(log_filter);

    // Persist the bumped launch count and any defaulted settings for next time.
    s_config->Save();

    LOG_INFO(Frontend, "Dekopon launch #{}", s_config->LaunchCount());
    LOG_INFO(Frontend, "User directory: {}", FileUtil::GetUserPath(FileUtil::UserPath::UserDir));
    LOG_INFO(Frontend, "Logging to: {}", FileUtil::GetUserPath(FileUtil::UserPath::LogDir));

    return s_config->LaunchCount();
}

void SaveConfig() {
    if (s_config) {
        s_config->Save();
    }
}

void Shutdown() {
    s_config.reset();
    Common::Log::Stop();
}

} // namespace SwitchFrontend
