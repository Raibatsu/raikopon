// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <memory>
#include <unordered_map>
#include "common/common_types.h"
#include "video_core/shader/shader.h"
#ifdef __SWITCH__
#include <atomic>
#include <mutex>
#include "common/thread_worker.h"
#include "video_core/pica/shader_setup.h"
#endif

namespace Pica::Shader {

class InterpreterEngine;
class JitShader;

class JitEngine final : public ShaderEngine {
public:
    JitEngine();
    ~JitEngine() override;

    void SetupBatch(ShaderSetup& setup, u32 entry_point) override;
    void Run(const ShaderSetup& setup, ShaderUnit& state) const override;

private:
#ifdef __SWITCH__
    /// A ready entry with a null shader failed to compile and runs on the interpreter instead.
    struct CacheEntry {
        std::unique_ptr<JitShader> shader;
        std::atomic<bool> ready{false};
    };

    void CompileEntry(CacheEntry* entry, std::shared_ptr<const ProgramCode> program_code,
                      std::shared_ptr<const SwizzleData> swizzle_data);

    std::unordered_map<u64, std::unique_ptr<CacheEntry>> cache;
    std::mutex cache_mutex;
    Common::ThreadWorker compile_workers;
    std::atomic<bool> exec_memory_exhausted{false};
#else
    /// A null entry marks a shader that failed to compile and runs on the interpreter instead.
    std::unordered_map<u64, std::unique_ptr<JitShader>> cache;
    /// Set once executable memory is exhausted.
    bool exec_memory_exhausted = false;
#endif
    std::unique_ptr<InterpreterEngine> interpreter;
};

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
