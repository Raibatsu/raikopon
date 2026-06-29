// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/rasterizer_cache/rasterizer_cache.h"
#include "video_core/renderer_deko3d/dk_texture_runtime.h"

namespace VideoCore {
template class RasterizerCache<Deko3D::Traits>;
} // namespace VideoCore
