// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <switch.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

namespace {

EGLDisplay s_display = EGL_NO_DISPLAY;
EGLContext s_context = EGL_NO_CONTEXT;
EGLSurface s_surface = EGL_NO_SURFACE;

bool InitEgl(NWindow* window) {
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_display == EGL_NO_DISPLAY) {
        std::printf("eglGetDisplay failed: 0x%x\n", eglGetError());
        return false;
    }

    if (eglInitialize(s_display, nullptr, nullptr) == EGL_FALSE) {
        std::printf("eglInitialize failed: 0x%x\n", eglGetError());
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        std::printf("eglBindAPI failed: 0x%x\n", eglGetError());
        return false;
    }

    const EGLint config_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE,
    };
    EGLConfig config;
    EGLint num_configs = 0;
    if (eglChooseConfig(s_display, config_attrs, &config, 1, &num_configs) == EGL_FALSE ||
        num_configs < 1) {
        std::printf("eglChooseConfig failed: 0x%x\n", eglGetError());
        return false;
    }

    s_surface = eglCreateWindowSurface(s_display, config, window, nullptr);
    if (s_surface == EGL_NO_SURFACE) {
        std::printf("eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        return false;
    }

    const EGLint context_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 2,
        EGL_NONE,
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, context_attrs);
    if (s_context == EGL_NO_CONTEXT) {
        std::printf("eglCreateContext failed: 0x%x\n", eglGetError());
        return false;
    }

    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
    return true;
}

void ExitEgl() {
    if (s_display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s_context != EGL_NO_CONTEXT) {
        eglDestroyContext(s_display, s_context);
        s_context = EGL_NO_CONTEXT;
    }
    if (s_surface != EGL_NO_SURFACE) {
        eglDestroySurface(s_display, s_surface);
        s_surface = EGL_NO_SURFACE;
    }
    eglTerminate(s_display);
    s_display = EGL_NO_DISPLAY;
}

} // namespace

int main(int argc, char* argv[]) {
    const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
    if (have_socket) {
        nxlinkStdio();
    }
    std::printf("Dekopon, an Azahar port for the Nintendo Switch\n");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    NWindow* window = nwindowGetDefault();
    if (!InitEgl(window)) {
        std::printf("EGL initialisation failed.\n");
        if (have_socket) {
            socketExit();
        }
        return 1;
    }

    EGLint width = 0, height = 0;
    eglQuerySurface(s_display, s_surface, EGL_WIDTH, &width);
    eglQuerySurface(s_display, s_surface, EGL_HEIGHT, &height);
    glViewport(0, 0, width, height);
    eglSwapInterval(s_display, 1);
    std::printf("EGL/GLES context up: %dx%d, GL_VERSION=%s\n", width, height,
                glGetString(GL_VERSION));

    // Fun orange
    glClearColor(0.96f, 0.55f, 0.13f, 1.0f);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }

        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(s_display, s_surface);
    }

    ExitEgl();
    if (have_socket) {
        socketExit();
    }
    return 0;
}
