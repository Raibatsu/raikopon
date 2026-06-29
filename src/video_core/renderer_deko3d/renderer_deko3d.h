// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "video_core/renderer_base.h"
#include "video_core/renderer_deko3d/dk_common.h"
#include "video_core/renderer_deko3d/dk_rasterizer.h"

namespace Core {
class System;
}

namespace Pica {
class PicaCore;
}

namespace Deko3D {

/**
 * Native Deko3D backend for the Nintendo Switch.
 * Currently does nothing.
 */
class RendererDeko3D : public VideoCore::RendererBase {
public:
    explicit RendererDeko3D(Core::System& system, Pica::PicaCore& pica, Frontend::EmuWindow& window,
                            Frontend::EmuWindow* secondary_window);
    ~RendererDeko3D() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override;

private:
    /// Brings up the device/queue/swapchain/command-buffer.
    void InitDeko3D(void* native_window);
    /// Tears everything down in reverse order.
    void ExitDeko3D();
    /// Records, per swapchain slot, the command list that binds and clears that framebuffer.
    void BuildClearCommandLists();
    /// Acquires a swapchain image, submits the current frame's commands, and presents it.
    void Present();

    static constexpr unsigned NumFramebuffers = 2;

private:
    Pica::PicaCore& pica;
    RasterizerDeko3D rasterizer;

    bool initialized = false;
    u32 fb_width = 1280;
    u32 fb_height = 720;

    DkDevice device = nullptr;
    DkQueue queue = nullptr;

    DkMemBlock fb_memblock = nullptr;
    std::array<DkImage, NumFramebuffers> framebuffers{};
    DkSwapchain swapchain = nullptr;

    DkMemBlock cmdbuf_memblock = nullptr;
    DkCmdBuf cmdbuf = nullptr;
    std::array<DkCmdList, NumFramebuffers> clear_cmdlists{};
};

} // namespace Deko3D
