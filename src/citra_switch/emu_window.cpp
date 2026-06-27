// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

#include <glad/glad.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "citra_switch/config.h"
#include "citra_switch/emu_window.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/settings.h"

namespace {

constexpr int kSwitchScreenWidth = 1280;
constexpr int kSwitchScreenHeight = 720;

constexpr std::array<EGLint, 17> config_attribs{
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_DEPTH_SIZE,      0,
    EGL_STENCIL_SIZE,    0,
    EGL_NONE,
};
constexpr std::array<EGLint, 3> context_attribs{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
constexpr std::array<EGLint, 5> pbuffer_attribs{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

// Background work shared context
class SharedContext_Switch : public Frontend::GraphicsContext {
public:
    SharedContext_Switch(EGLDisplay display, EGLConfig config, EGLContext share)
        : egl_display{display},
          egl_surface{eglCreatePbufferSurface(display, config, pbuffer_attribs.data())},
          egl_context{eglCreateContext(display, config, share, context_attribs.data())} {
        ASSERT_MSG(egl_surface != EGL_NO_SURFACE, "eglCreatePbufferSurface() failed: 0x{:x}",
                   eglGetError());
        ASSERT_MSG(egl_context != EGL_NO_CONTEXT, "eglCreateContext() failed: 0x{:x}",
                   eglGetError());
    }

    ~SharedContext_Switch() override {
        eglDestroySurface(egl_display, egl_surface);
        eglDestroyContext(egl_display, egl_context);
    }

    void MakeCurrent() override {
        eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    }

    void DoneCurrent() override {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    bool IsGLES() override {
        return true;
    }

private:
    EGLDisplay egl_display{EGL_NO_DISPLAY};
    EGLSurface egl_surface{EGL_NO_SURFACE};
    EGLContext egl_context{EGL_NO_CONTEXT};
};

} // namespace

EmuWindow_Switch::EmuWindow_Switch(void* native_window, bool is_secondary)
    : EmuWindow{is_secondary} {
    if (!CreateEGLContext(native_window)) {
        DestroyEGLContext();
        return;
    }

    window_info.render_surface = native_window;

    UpdateCurrentFramebufferLayout(window_width, window_height);
    is_valid = true;
}

EmuWindow_Switch::~EmuWindow_Switch() {
    DestroyEGLContext();
}

bool EmuWindow_Switch::CreateEGLContext(void* native_window) {
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        LOG_CRITICAL(Frontend, "eglGetDisplay() failed: 0x{:x}", eglGetError());
        return false;
    }
    if (eglInitialize(egl_display, nullptr, nullptr) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglInitialize() failed: 0x{:x}", eglGetError());
        return false;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglBindAPI() failed: 0x{:x}", eglGetError());
        return false;
    }

    EGLint num_configs = 0;
    if (eglChooseConfig(egl_display, config_attribs.data(), &egl_config, 1, &num_configs) !=
            EGL_TRUE ||
        num_configs < 1) {
        LOG_CRITICAL(Frontend, "eglChooseConfig() failed: 0x{:x}", eglGetError());
        return false;
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_config, native_window, nullptr);
    if (egl_surface == EGL_NO_SURFACE) {
        LOG_CRITICAL(Frontend, "eglCreateWindowSurface() failed: 0x{:x}", eglGetError());
        return false;
    }
    eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &window_width);
    eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        window_width = kSwitchScreenWidth;
        window_height = kSwitchScreenHeight;
    }

    egl_context =
        eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs.data());
    if (egl_context == EGL_NO_CONTEXT) {
        LOG_CRITICAL(Frontend, "eglCreateContext() failed: 0x{:x}", eglGetError());
        return false;
    }
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) != EGL_TRUE) {
        LOG_CRITICAL(Frontend, "eglMakeCurrent() failed: 0x{:x}", eglGetError());
        return false;
    }
    if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(eglGetProcAddress))) {
        LOG_CRITICAL(Frontend, "gladLoadGLES2Loader() failed");
        return false;
    }
    eglSwapInterval(egl_display, Settings::values.use_vsync.GetValue() ? 1 : 0);
    glViewport(0, 0, window_width, window_height);

    LOG_INFO(Frontend, "EGL/GLES context up: {}x{}, GL_VERSION={}", window_width, window_height,
             reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return true;
}

void EmuWindow_Switch::DestroyEGLContext() {
    if (egl_display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;
    }
    if (egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display, egl_surface);
        egl_surface = EGL_NO_SURFACE;
    }
    eglTerminate(egl_display);
    egl_display = EGL_NO_DISPLAY;
}

void EmuWindow_Switch::MakeCurrent() {
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

void EmuWindow_Switch::DoneCurrent() {
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void EmuWindow_Switch::PollEvents() {
    // TODO: Input and applet events
}

void EmuWindow_Switch::SwapBuffers() {
    eglSwapBuffers(egl_display, egl_surface);
}

std::unique_ptr<Frontend::GraphicsContext> EmuWindow_Switch::CreateSharedContext() const {
    return std::make_unique<SharedContext_Switch>(egl_display, egl_config, egl_context);
}

void EmuWindow_Switch::PresentClear() {
    if (!is_valid) {
        return;
    }
    // Azahar orange.
    glClearColor(0.96f, 0.55f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers();
}

namespace SwitchFrontend {

namespace {
std::unique_ptr<EmuWindow_Switch> s_window;
} // namespace

bool CreateWindow(void* native_window) {
    s_window = std::make_unique<EmuWindow_Switch>(native_window);
    if (!s_window->IsValid()) {
        LOG_CRITICAL(Frontend, "Failed to bring up EmuWindow");
        s_window.reset();
        return false;
    }
    return true;
}

void PresentFrame() {
    if (s_window) {
        s_window->PresentClear();
    }
}

void DestroyWindow() {
    s_window.reset();
}

} // namespace SwitchFrontend
