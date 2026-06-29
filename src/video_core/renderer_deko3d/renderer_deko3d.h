// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "common/math_util.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_deko3d/dk_common.h"
#include "video_core/renderer_deko3d/dk_rasterizer.h"

namespace Core {
class System;
}

namespace Layout {
struct FramebufferLayout;
}

namespace Pica {
enum class PixelFormat : u32;
struct FramebufferConfig;
union ColorFill;
class PicaCore;
} // namespace Pica

namespace Deko3D {

/// Backing for one 3DS screen
struct ScreenTexture {
    DkMemBlock memblock = nullptr;
    DkImage image{};
    DkImageDescriptor descriptor{};
    u32 width = 0;
    u32 height = 0;
    bool valid = false;
};

/**
 * Native Deko3D backend
 * Currently has no rasterizer
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

    static constexpr unsigned NumFramebuffers = 2;
    // screens[0] = top-left eye, [1] = top-right eye, [2] = bottom.
    static constexpr unsigned NumScreens = 3;

private:
    /// Brings up the device/queue/swapchain/command-buffer/shaders.
    void InitDeko3D(void* native_window);
    /// Tears everything down in reverse order.
    void ExitDeko3D();
    /// Loads the precompiled present shaders from romfs.
    bool LoadShaders();
    /// Records the immutable sampler descriptor.
    void SetupSampler();

    /// Pulls the current framebuffers from guest memory into the screen textures.
    void PrepareScreens();
    /// (Re)allocates a screen texture's backing image for the given dimensions.
    void ConfigureScreenTexture(ScreenTexture& screen, u32 width, u32 height);
    /// Decodes one framebuffer from guest memory and copies it into the screen image.
    void UploadScreen(ScreenTexture& screen, const Pica::FramebufferConfig& framebuffer,
                      PAddr framebuffer_addr, const Pica::ColorFill& color_fill);

    /// Acquires a swapchain image, draws the screens, submits and presents.
    void Present();
    /// Records a single textured quad for one screen at its window rectangle.
    void DrawScreenQuad(u32 image_slot, const Common::Rectangle<u32>& rect);

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

    DkMemBlock code_memblock = nullptr;
    DkShader present_vsh{};
    DkShader present_fsh{};
    bool shaders_ok = false;

    DkMemBlock descriptor_memblock = nullptr;
    DkGpuAddr image_descriptor_set = 0;
    DkGpuAddr sampler_descriptor_set = 0;

    DkMemBlock staging_memblock = nullptr;
    u32 staging_offset = 0;
    DkMemBlock vertex_memblock = nullptr;
    u32 vertex_offset = 0;

    std::array<ScreenTexture, NumScreens> screens{};
};

} // namespace Deko3D
