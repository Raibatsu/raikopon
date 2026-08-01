// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <bitset>
#include <memory>

#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Pica {
struct RegsInternal;
struct ShaderSetup;
} // namespace Pica

namespace Vulkan {

class Instance;
class Scheduler;
class RenderManager;
class DescriptorUpdateQueue;

enum class DescriptorHeapType : u32 {
    Buffer,
    Texture,
    Utility,
};

/**
 * Stores a collection of rasterizer pipelines used during rendering.
 */
class PipelineCache {
    static constexpr u32 NumRasterizerSets = 3;
    static constexpr u32 NumDescriptorHeaps = 3;
    // 4 dynamic UBOs in set 0: b0 vs-pica, b1 vs, b2 fs, b6 fs_config (ubershader). Vulkan orders
    // dynamic offsets by (set,binding), so fs_config's offset lives at index 3.
    static constexpr u32 NumDynamicOffsets = 4;
    static constexpr u32 FsConfigOffsetIndex = 3;

public:
    explicit PipelineCache(const Instance& instance, Scheduler& scheduler,
                           RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue);
    ~PipelineCache();

    /// Acquires and binds a free descriptor set from the appropriate heap.
    vk::DescriptorSet Acquire(DescriptorHeapType type) {
        const u32 index = static_cast<u32>(type);
        const auto descriptor_set = descriptor_heaps[index].Commit();
        bound_descriptor_sets[index] = descriptor_set;
        return descriptor_set;
    }

    /// Sets the dynamic offset for the uniform buffer at binding
    void UpdateRange(u8 binding, u32 offset) {
        offsets[binding] = offset;
    }

    /// Sets the dynamic offset for the ubershader fs_config UBO (set0/binding6 -> offset index 3).
    /// Kept separate from UpdateRange because binding number != dynamic-offset index here.
    void UpdateFsConfigRange(u32 offset) {
        offsets[FsConfigOffsetIndex] = offset;
    }

    /// Loads the driver pipeline cache and the disk shader cache
    void LoadCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                   const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Nothing else needs the CPU/GPU yet during a boot loading screen -- spins up an
    /// unrestricted, unpaced worker pool for replaying the disk cache and bypasses the shared
    /// compile pacer, since neither serves any purpose without a frame being presented.
    void BeginBootLoading();

    /// Drains and tears down the boot worker pool, and re-arms normal pacing for gameplay.
    void EndBootLoading();

    /// The pool boot-time cache replay should queue onto instead of the normal small/paced
    /// pools, or nullptr outside of BeginBootLoading()/EndBootLoading().
    Common::ThreadWorker* BootWorkerOverride() {
        return boot_workers.get();
    }

    /// Switches the driver pipeline cache and the shader disk cache to the specified title
    void SwitchCache(u64 title_id, const std::atomic_bool& stop_loading = std::atomic_bool{false},
                     const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Binds a pipeline using the provided information
    bool BindPipeline(PipelineInfo& info, PipelineWaitMode wait_mode = PipelineWaitMode::Async);

    /// Readiness probe for @p info's pipeline with no side effects on the command buffer. Queues the
    /// background build exactly like an Async BindPipeline would, but records nothing and touches no
    /// descriptor state, so a caller that is about to abandon the draw doesn't pay for it.
    bool IsPipelineReady(PipelineInfo& info);

    Pica::Shader::Generator::ExtraVSConfig CalcExtraConfig(
        const Pica::Shader::Generator::PicaVSConfig& config);

    /// Binds a PICA decompiled vertex shader
    bool UseProgrammableVertexShader(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup,
                                     const VertexLayout& layout);

    /// Binds a passthrough vertex shader
    void UseTrivialVertexShader();

    /// Binds a PICA decompiled geometry shader
    bool UseFixedGeometryShader(const Pica::RegsInternal& regs);

    /// Binds a passthrough geometry shader
    void UseTrivialGeometryShader();

    /// Binds a fragment shader generated from PICA state
    void UseFragmentShader(const Pica::RegsInternal& regs, const Pica::Shader::UserConfig& user);

    /// Binds the generic PICA fragment ubershader (one pre-compiled module handling all fragment
    /// state via the fs_config UBO). Always ready, so a draw whose specialized FS is still compiling
    /// can render immediately instead of skipping (avoids the shader-compile screen bleed).
    void UseUbershaderFragmentShader();

    /// Gets the current program ID
    u64 GetProgramID() const {
        return current_program_id;
    }

    void SetProgramID(u64 program_id) {
        current_program_id = program_id;
    }

    void SetAccurateMul(bool _accurate_mul) {
        profile.enable_accurate_mul = _accurate_mul;
    }

private:
    friend ShaderDiskCache;

    /// Loads the driver pipeline cache
    void LoadDriverPipelineDiskCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                                     const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Stores the generated pipeline cache
    void SaveDriverPipelineDiskCache();

    /// Loads the shader disk cache
    void LoadDiskCache(const std::atomic_bool& stop_loading = std::atomic_bool{false},
                       const VideoCore::DiskResourceLoadCallback& callback = {});

    /// Switches the disk cache at runtime to use a different title ID
    void SwitchDiskCache(u64 title_id, const std::atomic_bool& stop_loading,
                         const VideoCore::DiskResourceLoadCallback& callback);

    /// Builds the rasterizer pipeline layout
    void BuildLayout();

    /// Returns true when the disk data can be used by the current driver
    bool IsCacheValid(std::span<const u8> cache_data) const;

    /// Create pipeline cache directories. Returns true on success.
    bool EnsureDirectories() const;

    /// Returns the Vulkan shader directory
    std::string GetVulkanDir() const;

    /// Returns the pipeline cache storage dir
    std::string GetPipelineCacheDir() const;

    /// Returns the transferable shader dir
    std::string GetTransferableDir() const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;

    Pica::Shader::Profile profile{};
    vk::UniquePipelineCache driver_pipeline_cache;
    vk::UniquePipelineLayout pipeline_layout;
    std::size_t num_worker_threads;
    std::shared_ptr<Common::PaceLimiter> compile_pacer;
    Common::ThreadWorker pipeline_workers;
    Common::ThreadWorker shader_workers;
    Common::ThreadWorker priority_workers;
    std::unique_ptr<Common::ThreadWorker> boot_workers;
    PipelineInfo current_info{};
    GraphicsPipeline* current_pipeline{};
    std::array<DescriptorHeap, NumDescriptorHeaps> descriptor_heaps;
    std::array<vk::DescriptorSet, NumRasterizerSets> bound_descriptor_sets{};
    std::array<u32, NumDynamicOffsets> offsets{};

    std::array<u64, MAX_SHADER_STAGES> shader_hashes;
    std::array<Shader*, MAX_SHADER_STAGES> current_shaders;

    Shader trivial_vertex_shader;
    Shader ubershader_fragment_shader;

    u64 current_program_id{0};
    std::vector<std::shared_ptr<ShaderDiskCache>> disk_caches;
    std::shared_ptr<ShaderDiskCache> curr_disk_cache{};
};

} // namespace Vulkan
