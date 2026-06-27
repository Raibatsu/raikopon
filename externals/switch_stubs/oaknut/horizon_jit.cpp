// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <switch.h>

#include <oaknut/horizon_jit.h>

namespace oaknut::horizon {

JitAllocation JitAllocate(std::size_t size) {
    auto* jit = new Jit{};
    if (R_FAILED(jitCreate(jit, size))) {
        // Most likely opened in applet mode.
        std::fprintf(stderr,
                     "oaknut: jitCreate(%zu) failed. The process likely lacks JIT "
                     "capability. Make sure you aren't running in Applet Mode.\n",
                     size);
        delete jit;
        return {nullptr, nullptr, nullptr};
    }
    if (R_FAILED(jitTransitionToExecutable(jit))) {
        std::fprintf(stderr, "oaknut: jitTransitionToExecutable failed.\n");
        jitClose(jit);
        delete jit;
        return {nullptr, nullptr, nullptr};
    }
    return {jit, jitGetRwAddr(jit), jitGetRxAddr(jit)};
}

void JitFree(void* handle) {
    if (handle == nullptr) {
        return;
    }
    auto* jit = static_cast<Jit*>(handle);
    jitClose(jit);
    delete jit;
}

} // namespace oaknut::horizon
