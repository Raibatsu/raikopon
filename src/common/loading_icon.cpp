// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/loading_icon.h"

#include <mutex>

namespace Common::LoadingIcon {

namespace {

std::mutex s_mutex;
Icon s_icon;

} // namespace

void Set(const std::uint32_t* rgba_pixels, std::uint32_t size) {
    std::scoped_lock lock{s_mutex};
    if (rgba_pixels == nullptr || size == 0) {
        s_icon = Icon{};
        return;
    }
    s_icon.pixels.assign(rgba_pixels,
                         rgba_pixels + static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    s_icon.size = size;
}

void Clear() {
    std::scoped_lock lock{s_mutex};
    s_icon = Icon{};
}

Icon Get() {
    std::scoped_lock lock{s_mutex};
    return s_icon;
}

} // namespace Common::LoadingIcon
