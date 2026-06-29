// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "video_core/renderer_deko3d/renderer_deko3d.h"

namespace Deko3D {

namespace {
// Size of the command-buffer memory block.
constexpr u32 CmdBufMemorySize = 64 * 1024;

// Orange again for testing
constexpr float ClearColor[4] = {1.0f, 0.45f, 0.1f, 1.0f};
} // namespace

RendererDeko3D::RendererDeko3D(Core::System& system, Pica::PicaCore& pica_,
                               Frontend::EmuWindow& window, Frontend::EmuWindow* secondary_window)
    : VideoCore::RendererBase{system, window, secondary_window}, pica{pica_},
      rasterizer{system.Memory(), pica} {
    // The deko3d-mode EmuWindow stores the opaque nwindow here.
    void* native_window = window.GetWindowInfo().render_surface;
    if (native_window == nullptr) {
        LOG_ERROR(Render, "Deko3d: no native window provided.");
        return;
    }
    InitDeko3D(native_window);
}

RendererDeko3D::~RendererDeko3D() {
    ExitDeko3D();
}

void RendererDeko3D::InitDeko3D(void* native_window) {
    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    device = dkDeviceCreate(&device_maker);

    // Lay out the swapchain framebuffers.
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.flags =
        DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = fb_width;
    layout_maker.dimensions[1] = fb_height;

    DkImageLayout framebuffer_layout;
    dkImageLayoutInitialize(&framebuffer_layout, &layout_maker);

    const u32 fb_size = AlignUp(static_cast<u32>(dkImageLayoutGetSize(&framebuffer_layout)),
                                dkImageLayoutGetAlignment(&framebuffer_layout));

    DkMemBlockMaker memblock_maker;
    dkMemBlockMakerDefaults(&memblock_maker, device, NumFramebuffers * fb_size);
    memblock_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    fb_memblock = dkMemBlockCreate(&memblock_maker);

    std::array<const DkImage*, NumFramebuffers> swapchain_images{};
    for (unsigned i = 0; i < NumFramebuffers; ++i) {
        dkImageInitialize(&framebuffers[i], &framebuffer_layout, fb_memblock, i * fb_size);
        swapchain_images[i] = &framebuffers[i];
    }

    DkSwapchainMaker swapchain_maker;
    dkSwapchainMakerDefaults(&swapchain_maker, device, native_window, swapchain_images.data(),
                             NumFramebuffers);
    swapchain = dkSwapchainCreate(&swapchain_maker);

    // Command buffer.
    dkMemBlockMakerDefaults(&memblock_maker, device, CmdBufMemorySize);
    memblock_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    cmdbuf_memblock = dkMemBlockCreate(&memblock_maker);

    DkCmdBufMaker cmdbuf_maker;
    dkCmdBufMakerDefaults(&cmdbuf_maker, device);
    cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    dkCmdBufAddMemory(cmdbuf, cmdbuf_memblock, 0, CmdBufMemorySize);

    BuildClearCommandLists();

    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, device);
    queue_maker.flags = DkQueueFlags_Graphics;
    queue = dkQueueCreate(&queue_maker);

    initialized = true;
    LOG_INFO(Render, "deko3d device initialized ({}x{}, {} framebuffers)", fb_width, fb_height,
             NumFramebuffers);
}

void RendererDeko3D::BuildClearCommandLists() {
    const DkViewport viewport = {
        0.0f, 0.0f, static_cast<float>(fb_width), static_cast<float>(fb_height), 0.0f, 1.0f};
    const DkScissor scissor = {0, 0, fb_width, fb_height};

    for (unsigned i = 0; i < NumFramebuffers; ++i) {
        DkImageView view;
        dkImageViewDefaults(&view, &framebuffers[i]);
        dkCmdBufBindRenderTarget(cmdbuf, &view, nullptr);
        dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
        dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
        dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, ClearColor[0], ClearColor[1],
                                ClearColor[2], ClearColor[3]);
        clear_cmdlists[i] = dkCmdBufFinishList(cmdbuf);
    }
}

void RendererDeko3D::ExitDeko3D() {
    if (queue != nullptr) {
        dkQueueWaitIdle(queue);
        dkQueueDestroy(queue);
    }
    if (cmdbuf != nullptr) {
        dkCmdBufDestroy(cmdbuf);
    }
    if (cmdbuf_memblock != nullptr) {
        dkMemBlockDestroy(cmdbuf_memblock);
    }
    if (swapchain != nullptr) {
        dkSwapchainDestroy(swapchain);
    }
    if (fb_memblock != nullptr) {
        dkMemBlockDestroy(fb_memblock);
    }
    if (device != nullptr) {
        dkDeviceDestroy(device);
    }
    initialized = false;
}

void RendererDeko3D::Present() {
    if (!initialized) {
        return;
    }
    const int slot = dkQueueAcquireImage(queue, swapchain);
    dkQueueSubmitCommands(queue, clear_cmdlists[slot]);
    dkQueuePresentImage(queue, swapchain, slot);
}

void RendererDeko3D::SwapBuffers() {
    // The guest signalled a frame. Obviously this does nothing yet.
    Present();
    EndFrame();
}

void RendererDeko3D::TryPresent(int, bool is_secondary) {
    if (is_secondary) {
        return;
    }
    Present();
}

} // namespace Deko3D
