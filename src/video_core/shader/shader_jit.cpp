// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <exception>
#include <new>
#include "common/assert.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

JitEngine::JitEngine() : interpreter{std::make_unique<InterpreterEngine>()} {}
JitEngine::~JitEngine() = default;

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);
    setup.entry_point = entry_point;

    setup.DoProgramCodeFixup();
    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();

    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);
    auto iter = cache.find(cache_key);
    if (iter != cache.end()) {
        setup.cached_shader = iter->second.get();
        return;
    }

    std::unique_ptr<JitShader> shader;
    if (!exec_memory_exhausted) {
        try {
            shader = std::make_unique<JitShader>();
            shader->Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
        } catch (const std::bad_alloc&) {
            // The host has no executable memory left to back this shader.
            // Nothing is ever freed from the cache, so no later shader will fare any better.
            LOG_WARNING(HW_GPU, "Out of executable memory for the shader JIT, falling back to the "
                                "interpreter for this and all subsequent shaders");
            exec_memory_exhausted = true;
            shader.reset();
        } catch (const std::exception& e) {
            LOG_ERROR(HW_GPU, "Failed to compile shader, falling back to the interpreter: {}",
                      e.what());
            shader.reset();
        }
    }

    setup.cached_shader = shader.get();
    cache.emplace_hint(iter, cache_key, std::move(shader));
}

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
    if (setup.cached_shader == nullptr) {
        // This shader has no compiled code.
        interpreter->Run(setup, state);
        return;
    }

    MICROPROFILE_SCOPE(GPU_Shader);

    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
