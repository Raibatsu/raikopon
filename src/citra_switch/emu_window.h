// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <EGL/egl.h>
#include "core/frontend/emu_window.h"

// EmuWindow backed by a native libnx nwindow through an EGL/GLES context.
// This is adapted from the Android way of doing this
class EmuWindow_Switch : public Frontend::EmuWindow {
public:
    explicit EmuWindow_Switch(void* native_window, bool is_secondary = false);
    ~EmuWindow_Switch() override;

    void MakeCurrent() override;
    void DoneCurrent() override;
    void PollEvents() override;
    void SwapBuffers() override;
    std::unique_ptr<GraphicsContext> CreateSharedContext() const override;

    bool IsGLES() override {
        return true;
    }

    bool IsValid() const {
        return is_valid;
    }

    /// Present the latest emulated frame from the renderer's mailbox to the window.
    void Present();

    /// Clear the default framebuffer to a solid colour and present.
    void PresentClear();

private:
    bool CreateEGLContext(void* native_window);
    void DestroyEGLContext();

    EGLDisplay egl_display{EGL_NO_DISPLAY};
    EGLSurface egl_surface{EGL_NO_SURFACE};
    EGLContext egl_context{EGL_NO_CONTEXT};
    EGLConfig egl_config{};

    // Shared context handed to the emulation thread via MakeCurrent().
    std::unique_ptr<GraphicsContext> core_context;

    int window_width{};
    int window_height{};
    bool is_valid{};
};

/// Returns the live frontend window
EmuWindow_Switch* GetEmuWindow();
