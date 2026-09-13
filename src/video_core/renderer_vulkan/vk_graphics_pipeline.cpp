// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/gpu_frame_log.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "common/shader_compile_stats.h"
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

MICROPROFILE_DEFINE(Vulkan_Pipeline, "Vulkan", "Pipeline Building", MP_RGB(0, 192, 32));

vk::ShaderStageFlagBits MakeShaderStage(std::size_t index) {
    switch (index) {
    case 0:
        return vk::ShaderStageFlagBits::eVertex;
    case 1:
        return vk::ShaderStageFlagBits::eFragment;
    case 2:
        return vk::ShaderStageFlagBits::eGeometry;
    default:
        LOG_CRITICAL(Render_Vulkan, "Invalid shader stage index!");
        UNREACHABLE();
    }
    return vk::ShaderStageFlagBits::eVertex;
}

const char* WaitModeName(PipelineWaitMode mode) {
    switch (mode) {
    case PipelineWaitMode::Async:
        return "Async";
    case PipelineWaitMode::Bounded:
        return "Bounded";
    case PipelineWaitMode::Blocking:
        return "Blocking";
    }
    return "?";
}

void EmitVertexInput(vk::CommandBuffer cmdbuf, const Instance& instance,
                     const VertexLayout& layout) {
    const u32 stride_alignment = instance.GetMinVertexStrideAlignment();
    boost::container::static_vector<vk::VertexInputBindingDescription2EXT, MAX_VERTEX_BINDINGS>
        bindings;
    for (u32 i = 0; i < layout.binding_count; i++) {
        const auto& binding = layout.bindings[i];
        bindings.push_back(vk::VertexInputBindingDescription2EXT{
            .binding = binding.binding,
            .stride = Common::AlignUp(binding.byte_count.Value(), stride_alignment),
            .inputRate = binding.fixed.Value() ? vk::VertexInputRate::eInstance
                                               : vk::VertexInputRate::eVertex,
            .divisor = 1,
        });
    }

    boost::container::static_vector<vk::VertexInputAttributeDescription2EXT, MAX_VERTEX_ATTRIBUTES>
        attributes;
    for (u32 i = 0; i < layout.attribute_count; i++) {
        const auto& attr = layout.attributes[i];
        const FormatTraits& traits = instance.GetTraits(attr.type, attr.size);
        vk::Format format = traits.native;
        // At the end there's always the fixed binding which takes up
        // at least 16 bytes so we should always be able to alias.
        if (traits.needs_emulation) {
            format = instance.GetTraits(attr.type, 4).native;
        }
        attributes.push_back(vk::VertexInputAttributeDescription2EXT{
            .location = attr.location,
            .binding = attr.binding,
            .format = format,
            .offset = attr.offset,
        });
    }

    cmdbuf.setVertexInputEXT(bindings, attributes);
}

u64 StaticPipelineInfo::OptimizedHash(const Instance& instance) const {
    // With EDS3 the blend equation (factors/ops), write-mask, blend-enable, and logic-op-enable
    // are all set dynamically, so none of them may key the pipeline -- otherwise every blend combo
    // would still be a distinct pipeline, defeating the point. The one remaining piece, the
    // logic-op *value* itself, needs EDS2's separate dynamic-logic-op feature; only once both are
    // available is the whole blending struct excludable.
    const bool blend_fully_dynamic =
        instance.IsExtendedDynamicState3Supported() && instance.IsDynamicLogicOpSupported();
    const u64 blend_hash =
        blend_fully_dynamic ? 0
        : instance.IsExtendedDynamicState3Supported()
            ? Common::HashCombine(static_cast<u64>(blending.blend_enable),
                                  static_cast<u64>(blending.logic_op))
            : Common::ComputeStructHash64(blending);
    // With VK_EXT_vertex_input_dynamic_state the whole binding/attribute layout is set dynamically
    // via vkCmdSetVertexInputEXT, so it must NOT key the pipeline either -- this was otherwise the
    // single biggest remaining static-key contributor, since every distinct mesh/model's vertex
    // attribute set forced its own pipeline.
    const u64 vertex_layout_hash = instance.IsVertexInputDynamicStateSupported()
                                       ? 0
                                       : Common::ComputeStructHash64(vertex_layout);
    u64 info_hash =
        Common::HashCombine(shader_ids[0], shader_ids[1], shader_ids[2], vertex_layout_hash,
                            Common::ComputeStructHash64(attachments), blend_hash);

    if (!instance.IsExtendedDynamicStateSupported()) {
        info_hash = Common::HashCombine(info_hash, Common::ComputeStructHash64(rasterization),
                                        Common::ComputeStructHash64(depth_stencil));
    }

    return info_hash;
}

u16 PipelineInfo::GetFinalColorWriteMask(const Instance& instance) {
    u16 color_write_mask = state.blending.color_write_mask;
    const bool is_logic_op_emulated =
        instance.NeedsLogicOpEmulation() && !state.blending.blend_enable;
    const bool is_logic_op_noop = state.blending.logic_op == Pica::FramebufferRegs::LogicOp::NoOp;
    if (is_logic_op_emulated && is_logic_op_noop) {
        // Color output is disabled by logic operation. We use color write mask to skip
        // color but allow depth write.
        color_write_mask = 0;
    }
    return color_write_mask;
}

Shader::Shader(const Instance& instance) : device{instance.GetDevice()} {}

Shader::Shader(const Instance& instance, vk::ShaderStageFlagBits stage, std::string code)
    : Shader{instance} {
    // Retain the intermediate SPIR-V (Compile() would discard it) -- CreateShaderObject needs the
    // raw bytecode and shouldn't have to re-run glslang to get it.
    spirv = CompileGLSL(code, stage);
    module = CompileSPV(spirv, instance.GetDevice());
    MarkDone();
}

Shader::~Shader() {
    if (device && module) {
        device.destroyShaderModule(module);
    }
    if (device && shader_object) {
        device.destroyShaderEXT(shader_object);
    }
}

void Shader::CreateShaderObject(vk::ShaderStageFlagBits stage, vk::ShaderStageFlags next_stage,
                                std::span<const vk::DescriptorSetLayout> set_layouts) {
    ASSERT_MSG(!spirv.empty(),
              "CreateShaderObject requires the SPIR-V this shader was compiled from");
    const vk::ShaderCreateInfoEXT create_info = {
        .stage = stage,
        .nextStage = next_stage,
        .codeType = vk::ShaderCodeTypeEXT::eSpirv,
        .codeSize = spirv.size() * sizeof(u32),
        .pCode = spirv.data(),
        .pName = "main",
        .setLayoutCount = static_cast<u32>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
    };
    const auto result = device.createShaderEXT(create_info);
    if (result.result == vk::Result::eSuccess) {
        shader_object = result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Shader object creation failed for stage {}!",
                    static_cast<u32>(stage));
    }
}

GraphicsPipeline::GraphicsPipeline(const Instance& instance_, RenderManager& renderpass_cache_,
                                   const PipelineInfo& info_, vk::PipelineCache pipeline_cache_,
                                   vk::PipelineLayout layout_, std::array<Shader*, 3> stages_,
                                   Common::ThreadWorker* worker_)
    : instance{instance_}, renderpass_cache{renderpass_cache_}, worker{worker_},
      pipeline_layout{layout_}, pipeline_cache{pipeline_cache_}, info{info_}, stages{stages_} {}

GraphicsPipeline::~GraphicsPipeline() = default;

bool GraphicsPipeline::TryBuild(PipelineWaitMode wait_mode, Common::ThreadWorker* priority_worker) {
    constexpr std::chrono::microseconds kBoundedWaitBudget{1500};

    const auto wait_for_pending = [&](PipelineWaitMode mode) {
        switch (mode) {
        case PipelineWaitMode::Async:
            return false;
        case PipelineWaitMode::Bounded: {
            const auto start = std::chrono::steady_clock::now();
            const bool ready = WaitDoneFor(kBoundedWaitBudget);
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);
            Common::ShaderCompileStats::RecordStall(elapsed_us);
            Common::GpuFrameLog::LogStall("pending_wait", Common::GpuFrameLog::CurrentFrame(),
                                          "Bounded", elapsed_us.count());
            return ready;
        }
        case PipelineWaitMode::Blocking:
            return true;
        }
        return false;
    };

    // The pipeline is currently being compiled. We can either wait for it
    // or skip the draw.
    if (is_pending.load(std::memory_order::acquire)) {
        return wait_for_pending(wait_mode);
    }

    // If the shaders haven't been compiled yet, we cannot proceed.
    bool shaders_ready = std::all_of(
        stages.begin(), stages.end(), [](Shader* shader) { return !shader || shader->IsDone(); });

    if (!shaders_ready) {
        if (wait_mode == PipelineWaitMode::Async) {
            return false;
        }
        if (wait_mode == PipelineWaitMode::Bounded) {
            const auto start = std::chrono::steady_clock::now();
            shaders_ready = true;
            for (Shader* shader : stages) {
                if (shader && !shader->WaitDoneFor(kBoundedWaitBudget)) {
                    shaders_ready = false;
                    break;
                }
            }
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);
            Common::ShaderCompileStats::RecordStall(elapsed_us);
            Common::GpuFrameLog::LogStall("shaders_wait", Common::GpuFrameLog::CurrentFrame(),
                                          "Bounded", elapsed_us.count());
            if (!shaders_ready) {
                return false;
            }
        }
    }

    // Ask the driver if it can give us the pipeline quickly.
    if (shaders_ready && !Settings::values.disable_pipeline_fast_path.GetValue() &&
        instance.IsPipelineCreationCacheControlSupported()) {
        constexpr std::chrono::microseconds kFastPathSlowThreshold{1000};
        const auto start = std::chrono::steady_clock::now();
        const bool built = Build(true);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed > kFastPathSlowThreshold) {
            static std::atomic_bool warned_slow_fast_path{false};
            if (!warned_slow_fast_path.exchange(true)) {
                LOG_WARNING(Render_Vulkan,
                            "Pipeline fast path (FAIL_ON_PIPELINE_COMPILE_REQUIRED) took {}us, "
                            "driver may not honor the fail-fast hint",
                            elapsed.count());
            }
        }
        if (built) {
            return true;
        }
    }

    // Fallback to (a)synchronous compilation. priority_worker overrides the pipeline's own
    // worker whenever a caller supplies one, not just in Bounded mode - e.g. boot-time cache
    // replay passes a dedicated, unpaced pool since nothing needs protecting from it yet.
    // Claim the build atomically: the inline path below runs Build() on the calling thread, so
    // without this two threads racing the is_pending check above could both build the same
    // pipeline concurrently. Previously every Build() was serialized onto one worker pool.
    if (is_pending.exchange(true, std::memory_order::acq_rel)) {
        return wait_for_pending(wait_mode);
    }

    // A Blocking caller is going to sit on WaitDone() regardless, so building on the calling thread
    // costs it nothing extra and keeps the (single, on Switch) pipeline worker free. Queueing it
    // instead used to head-of-line block the whole pool: Build() waits on shader modules from a
    // different pool, so one cross-pool wait froze every other pipeline behind it.
    if (wait_mode == PipelineWaitMode::Blocking) {
        Common::GpuFrameLog::LogPipelineEvent("start", Common::GpuFrameLog::CurrentFrame(),
                                              Hash(), "Blocking");
        const bool boosted = Common::ShaderCompileStats::BeginCompile(
            Settings::values.enable_compile_boost.GetValue());
        Build();
        Common::ShaderCompileStats::EndCompile(boosted);
        return true;
    }

    Common::ThreadWorker* const target_worker = priority_worker ? priority_worker : worker;
    const bool boosted =
        Common::ShaderCompileStats::BeginCompile(Settings::values.enable_compile_boost.GetValue());
    const char* const mode_name = WaitModeName(wait_mode);
    Common::GpuFrameLog::LogPipelineEvent("queued", Common::GpuFrameLog::CurrentFrame(), Hash(),
                                          mode_name);
    const auto queued_at = std::chrono::steady_clock::now();
    target_worker->QueueWork([this, boosted, mode_name, queued_at] {
        const auto queue_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - queued_at);
        Common::GpuFrameLog::LogPipelineEvent("start", Common::GpuFrameLog::CurrentFrame(), Hash(),
                                              mode_name, queue_wait_us.count());
        Build();
        Common::ShaderCompileStats::EndCompile(boosted);
    });

    switch (wait_mode) {
    case PipelineWaitMode::Async:
        return false;
    case PipelineWaitMode::Bounded: {
        const auto start = std::chrono::steady_clock::now();
        const bool ready = WaitDoneFor(kBoundedWaitBudget);
        Common::ShaderCompileStats::RecordStall(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start));
        return ready;
    }
    case PipelineWaitMode::Blocking:
        return true;
    }
    return false;
}

bool GraphicsPipeline::Build(bool fail_on_compile_required) {
    MICROPROFILE_SCOPE(Vulkan_Pipeline);
    const auto setup_start = std::chrono::steady_clock::now();

    // With VK_EXT_vertex_input_dynamic_state, the whole layout is supplied per-draw via
    // vkCmdSetVertexInputEXT (see EmitVertexInput) instead of baked here -- it's excluded from
    // OptimizedHash() in that case too, so this pipeline may end up shared across draws with
    // different vertex layouts entirely. pVertexInputState is ignored by the driver whenever
    // VK_DYNAMIC_STATE_VERTEX_INPUT_EXT is in pDynamicState (left null here to match).
    const u32 stride_alignment = instance.GetMinVertexStrideAlignment();
    std::array<vk::VertexInputBindingDescription, MAX_VERTEX_BINDINGS> bindings;
    std::array<vk::VertexInputAttributeDescription, MAX_VERTEX_ATTRIBUTES> attributes;
    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    if (!instance.IsVertexInputDynamicStateSupported()) {
        for (u32 i = 0; i < info.state.vertex_layout.binding_count; i++) {
            const auto& binding = info.state.vertex_layout.bindings[i];
            bindings[i] = vk::VertexInputBindingDescription{
                .binding = binding.binding,
                .stride = Common::AlignUp(binding.byte_count.Value(), stride_alignment),
                .inputRate = binding.fixed.Value() ? vk::VertexInputRate::eInstance
                                                   : vk::VertexInputRate::eVertex,
            };
        }

        for (u32 i = 0; i < info.state.vertex_layout.attribute_count; i++) {
            const auto& attr = info.state.vertex_layout.attributes[i];
            const FormatTraits& traits = instance.GetTraits(attr.type, attr.size);
            attributes[i] = vk::VertexInputAttributeDescription{
                .location = attr.location,
                .binding = attr.binding,
                .format = traits.native,
                .offset = attr.offset,
            };

            // At the end there's always the fixed binding which takes up
            // at least 16 bytes so we should always be able to alias.
            if (traits.needs_emulation) {
                const FormatTraits& comp_four_traits = instance.GetTraits(attr.type, 4);
                attributes[i].format = comp_four_traits.native;
            }
        }

        vertex_input_info = vk::PipelineVertexInputStateCreateInfo{
            .vertexBindingDescriptionCount = info.state.vertex_layout.binding_count,
            .pVertexBindingDescriptions = bindings.data(),
            .vertexAttributeDescriptionCount = info.state.vertex_layout.attribute_count,
            .pVertexAttributeDescriptions = attributes.data(),
        };
    }

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = PicaToVK::PrimitiveTopology(info.state.rasterization.topology),
        .primitiveRestartEnable = false,
    };

    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .cullMode = PicaToVK::CullMode(info.state.rasterization.cull_mode,
                                       info.state.rasterization.flip_viewport),
        .frontFace = PicaToVK::FrontFace(info.state.rasterization.cull_mode),
        .depthBiasEnable = false,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
    };

    const vk::PipelineColorBlendAttachmentState colorblend_attachment = {
        .blendEnable = info.state.blending.blend_enable,
        .srcColorBlendFactor = PicaToVK::BlendFunc(info.state.blending.src_color_blend_factor),
        .dstColorBlendFactor = PicaToVK::BlendFunc(info.state.blending.dst_color_blend_factor),
        .colorBlendOp = PicaToVK::BlendEquation(info.state.blending.color_blend_eq),
        .srcAlphaBlendFactor = PicaToVK::BlendFunc(info.state.blending.src_alpha_blend_factor),
        .dstAlphaBlendFactor = PicaToVK::BlendFunc(info.state.blending.dst_alpha_blend_factor),
        .alphaBlendOp = PicaToVK::BlendEquation(info.state.blending.alpha_blend_eq),
        .colorWriteMask =
            static_cast<vk::ColorComponentFlags>(info.GetFinalColorWriteMask(instance)),
    };

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = !info.state.blending.blend_enable && !instance.NeedsLogicOpEmulation(),
        .logicOp = PicaToVK::LogicOp(info.state.blending.logic_op),
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment,
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = 1.0f,
        .height = 1.0f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const vk::Rect2D scissor = {
        .offset = {0, 0},
        .extent = {1, 1},
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    boost::container::static_vector<vk::DynamicState, 20> dynamic_states = {
        vk::DynamicState::eViewport,           vk::DynamicState::eScissor,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilReference,   vk::DynamicState::eBlendConstants,
    };

    if (instance.IsExtendedDynamicStateSupported()) {
        constexpr std::array extended = {
            vk::DynamicState::eCullModeEXT,        vk::DynamicState::eDepthCompareOpEXT,
            vk::DynamicState::eDepthTestEnableEXT, vk::DynamicState::eDepthWriteEnableEXT,
            vk::DynamicState::eFrontFaceEXT,       vk::DynamicState::ePrimitiveTopologyEXT,
            vk::DynamicState::eStencilOpEXT,       vk::DynamicState::eStencilTestEnableEXT,
        };
        dynamic_states.insert(dynamic_states.end(), extended.begin(), extended.end());
    }

    if (instance.IsExtendedDynamicState3Supported()) {
        // Blend equation (factors + ops), write-mask, blend-enable, and logic-op-enable all
        // become dynamic -- these are the many-combo fields that otherwise explode the pipeline
        // permutation count. See CreateDevice: all four are required together as one unit.
        constexpr std::array eds3 = {
            vk::DynamicState::eColorBlendEquationEXT,
            vk::DynamicState::eColorWriteMaskEXT,
            vk::DynamicState::eColorBlendEnableEXT,
            vk::DynamicState::eLogicOpEnableEXT,
        };
        dynamic_states.insert(dynamic_states.end(), eds3.begin(), eds3.end());
    }

    if (instance.IsDynamicLogicOpSupported()) {
        // The logic-op *value* itself (separate EDS2 feature from EDS3's enable bit above) --
        // together they let the whole blending struct be excluded from the pipeline key.
        dynamic_states.push_back(vk::DynamicState::eLogicOpEXT);
    }

    if (instance.IsVertexInputDynamicStateSupported()) {
        // The single biggest remaining static-key contributor once the above are already dynamic:
        // every distinct mesh/model's vertex attribute set otherwise forces its own pipeline.
        dynamic_states.push_back(vk::DynamicState::eVertexInputEXT);
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::StencilOpState stencil_op_state = {
        .failOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_fail_op),
        .passOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_pass_op),
        .depthFailOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_depth_fail_op),
        .compareOp = PicaToVK::CompareFunc(info.state.depth_stencil.stencil_compare_op),
    };

    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthTestEnable = static_cast<u32>(info.state.depth_stencil.depth_test_enable.Value()),
        .depthWriteEnable = static_cast<u32>(info.state.depth_stencil.depth_write_enable.Value()),
        .depthCompareOp = PicaToVK::CompareFunc(info.state.depth_stencil.depth_compare_op),
        .depthBoundsTestEnable = false,
        .stencilTestEnable = static_cast<u32>(info.state.depth_stencil.stencil_test_enable.Value()),
        .front = stencil_op_state,
        .back = stencil_op_state,
    };

    const auto shader_wait_start = std::chrono::steady_clock::now();
    const auto setup_us = std::chrono::duration_cast<std::chrono::microseconds>(
        shader_wait_start - setup_start);

    u32 shader_count = 0;
    std::array<vk::PipelineShaderStageCreateInfo, MAX_SHADER_STAGES> shader_stages;
    for (std::size_t i = 0; i < stages.size(); i++) {
        Shader* shader = stages[i];
        if (!shader) {
            continue;
        }

        shader->WaitDone();
        shader_stages[shader_count++] = vk::PipelineShaderStageCreateInfo{
            .stage = MakeShaderStage(i),
            .module = shader->Handle(),
            .pName = "main",
        };
    }

    const auto shader_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - shader_wait_start);

    // Under dynamic rendering, pipelines only need attachment *format* compatibility (no render
    // pass object) -- built here and chained via pNext instead of passing a vk::RenderPass.
    vk::Format color_attachment_format = vk::Format::eUndefined;
    if (info.state.attachments.color != VideoCore::PixelFormat::Invalid) {
        color_attachment_format = instance.GetTraits(info.state.attachments.color).native;
    }
    vk::Format depth_attachment_format = vk::Format::eUndefined;
    if (info.state.attachments.depth != VideoCore::PixelFormat::Invalid) {
        depth_attachment_format = instance.GetTraits(info.state.attachments.depth).native;
    }
    // D24S8 is the only combined depth+stencil format this fork uses -- mirrors
    // CreateRenderPass's stencilLoadOp handling, which shares one AttachmentDescription for both.
    const bool has_stencil = info.state.attachments.depth == VideoCore::PixelFormat::D24S8;
    const vk::PipelineRenderingCreateInfoKHR rendering_create_info = {
        .colorAttachmentCount = color_attachment_format != vk::Format::eUndefined ? 1u : 0u,
        .pColorAttachmentFormats = &color_attachment_format,
        .depthAttachmentFormat = depth_attachment_format,
        .stencilAttachmentFormat = has_stencil ? depth_attachment_format : vk::Format::eUndefined,
    };
    const bool dynamic_rendering = instance.IsDynamicRenderingSupported();

    vk::GraphicsPipelineCreateInfo pipeline_info = {
        .pNext = dynamic_rendering ? &rendering_create_info : nullptr,
        .stageCount = shader_count,
        .pStages = shader_stages.data(),
        .pVertexInputState =
            instance.IsVertexInputDynamicStateSupported() ? nullptr : &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = pipeline_layout,
        .renderPass = dynamic_rendering
                          ? VK_NULL_HANDLE
                          : renderpass_cache.GetRenderpass(info.state.attachments.color,
                                                           info.state.attachments.depth, false),
    };

    if (fail_on_compile_required) {
        pipeline_info.flags |= vk::PipelineCreateFlagBits::eFailOnPipelineCompileRequiredEXT;
    }

    constexpr std::chrono::milliseconds kSlowPipelineThreshold{20};
    const auto compile_start = std::chrono::steady_clock::now();
    auto result = instance.GetDevice().createGraphicsPipelineUnique(pipeline_cache, pipeline_info);
    const auto compile_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - compile_start);
    if (compile_elapsed > kSlowPipelineThreshold) {
        LOG_WARNING(Render_Vulkan, "vkCreateGraphicsPipelines took {}ms (fail_on_compile_required={})",
                    compile_elapsed.count(), fail_on_compile_required);
    }
    if (result.result == vk::Result::eSuccess) {
        pipeline = std::move(result.value);
    } else if (result.result == vk::Result::eErrorPipelineCompileRequiredEXT) {
        return false;
    } else {
        UNREACHABLE_MSG("Graphics pipeline creation failed!");
    }

    MarkDone();
    Common::GpuFrameLog::LogPipelineEvent(
        "done", Common::GpuFrameLog::CurrentFrame(), Hash(), "-", 0, setup_us.count(),
        shader_wait_us.count(),
        std::chrono::duration_cast<std::chrono::microseconds>(compile_elapsed).count());
    return true;
}

} // namespace Vulkan
