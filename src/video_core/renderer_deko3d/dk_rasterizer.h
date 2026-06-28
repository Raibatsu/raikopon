// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/rasterizer_accelerated.h"

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace Deko3D {

/**
 * Placeholder rasterizer for the deko3d backend.
 * TODO: Complete this
 */
class RasterizerDeko3D : public VideoCore::RasterizerAccelerated {
public:
    explicit RasterizerDeko3D(Memory::MemorySystem& memory, Pica::PicaCore& pica);
    ~RasterizerDeko3D() override;

    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
};

} // namespace Deko3D
