// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_deko3d/dk_rasterizer.h"

namespace Deko3D {

RasterizerDeko3D::RasterizerDeko3D(Memory::MemorySystem& memory, Pica::PicaCore& pica)
    : VideoCore::RasterizerAccelerated{memory, pica} {}

RasterizerDeko3D::~RasterizerDeko3D() = default;

void RasterizerDeko3D::DrawTriangles() {
    // Drop the accumulated batch until the real draw path exists.
    vertex_batch.clear();
}

void RasterizerDeko3D::FlushAll() {}

void RasterizerDeko3D::FlushRegion(PAddr, u32) {}

void RasterizerDeko3D::InvalidateRegion(PAddr, u32) {}

void RasterizerDeko3D::FlushAndInvalidateRegion(PAddr, u32) {}

void RasterizerDeko3D::ClearAll(bool) {}

} // namespace Deko3D
