// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#define VK_USE_PLATFORM_VI_NN
#define VK_ENABLE_BETA_EXTENSIONS
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_STRUCT_SETTERS
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>
#include <switch.h>

#include "citra_switch/canvas_quad_spv.h"
#include "citra_switch/gpu_canvas.h"

extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

namespace SwitchFrontend {

namespace {

constexpr std::uint32_t kTargetApiVersion = VK_API_VERSION_1_1;
constexpr std::uint32_t kDefaultAtlasSize = 1024;
constexpr std::uint32_t kMaxQuadVertices = 16384;
constexpr std::uint32_t kCaptureImageWidth = 1920;
constexpr std::uint32_t kCaptureImageHeight = 1080;

vk::ClearColorValue ToClearColor(CanvasColor color) {
    return vk::ClearColorValue{std::array{color.r, color.g, color.b, color.a}};
}

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    float mode;
};

} // namespace

struct GpuCanvas::Impl {
    NWindow* window = nullptr;
    vk::UniqueInstance instance;
    vk::UniqueSurfaceKHR surface;
    vk::PhysicalDevice physical_device;
    vk::UniqueDevice device;
    vk::Queue queue;
    std::uint32_t queue_family = 0;
    vk::Format format = vk::Format::eUndefined;
    vk::SampleCountFlagBits msaa_samples = vk::SampleCountFlagBits::e1;
    vk::Extent2D extent{};
    vk::UniqueSwapchainKHR swapchain;
    std::vector<vk::Image> images;
    std::vector<vk::UniqueImageView> image_views;
    vk::UniqueRenderPass render_pass;
    struct MsaaTarget {
        vk::UniqueImage image;
        vk::UniqueDeviceMemory memory;
        vk::UniqueImageView view;
    };
    std::vector<MsaaTarget> msaa_targets;
    std::vector<vk::UniqueFramebuffer> framebuffers;
    vk::UniqueCommandPool command_pool;
    std::vector<vk::CommandBuffer> command_buffers;
    std::vector<vk::UniqueSemaphore> image_available;
    std::vector<vk::UniqueSemaphore> render_finished;
    std::vector<vk::UniqueFence> in_flight;
    std::vector<vk::Fence> images_in_flight;
    std::uint32_t frame_index = 0;
    std::uint32_t image_index = 0;
    bool valid = false;
    bool needs_resize = false;
    AppletOperationMode last_operation_mode = AppletOperationMode_Handheld;

    vk::UniqueDescriptorSetLayout descriptor_set_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline pipeline;
    vk::UniqueDescriptorPool descriptor_pool;
    vk::DescriptorSet descriptor_set;
    vk::UniqueSampler sampler;
    vk::UniqueImage atlas_image;
    vk::UniqueDeviceMemory atlas_memory;
    vk::UniqueImageView atlas_view;
    std::uint32_t atlas_width = 0;
    std::uint32_t atlas_height = 0;
    bool atlas_layout_initialized = false;

    struct VertexBufferSlot {
        vk::UniqueBuffer buffer;
        vk::UniqueDeviceMemory memory;
        void* mapped = nullptr;
    };
    std::vector<VertexBufferSlot> vertex_buffers;
    std::uint32_t pending_vertex_count = 0;
    std::uint32_t flush_start = 0;
    vk::Rect2D current_scissor{};

    struct CaptureTarget {
        vk::UniqueImage image;
        vk::UniqueDeviceMemory memory;
        vk::UniqueImageView view;
        vk::UniqueImage msaa_image;
        vk::UniqueDeviceMemory msaa_memory;
        vk::UniqueImageView msaa_view;
        vk::UniqueFramebuffer framebuffer;
        vk::DescriptorSet descriptor_set;
        vk::CommandBuffer command_buffer;
        vk::UniqueFence fence;
        VertexBufferSlot vertex_buffer;
        std::uint32_t captured_width = 0;
        std::uint32_t captured_height = 0;
        std::uint32_t pending_vertex_count = 0;
        std::uint32_t flush_start = 0;
    };
    vk::UniqueRenderPass capture_render_pass;
    std::array<CaptureTarget, kCaptureSlotCount> capture_targets;
    bool capturing = false;
    std::uint32_t capture_slot = 0;

    void FlushSpan();
    vk::CommandBuffer CurrentCommandBuffer();
    VertexBufferSlot& CurrentVertexSlot();
    std::uint32_t& CurrentPendingVertexCount();
    std::uint32_t& CurrentFlushStart();
    void EmitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 CanvasColor tint, float rotation_radians, float mode);
    bool CreateOneVertexBuffer(VertexBufferSlot& slot);
    bool CreateInstance();
    bool CreateSurface();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain();
    bool CreateRenderPass();
    bool CreateMsaaTargets();
    bool CreateFramebuffers();
    bool CreateCommandObjects();
    bool CreateSyncObjects();
    bool CreatePipeline();
    bool CreateAtlasResources(std::uint32_t width, std::uint32_t height);
    bool CreateVertexBuffers();
    bool CreateCaptureRenderPass();
    bool CreateCaptureTargets();
    bool RecreateSwapchainObjects();
    std::uint32_t FindMemoryType(std::uint32_t type_bits, vk::MemoryPropertyFlags properties);
};

bool GpuCanvas::Impl::CreateInstance() {
    setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    setenv("MESA_SHADER_CACHE_DISABLE", "1", 1);

    const auto get_instance_proc_addr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(&vk_icdGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(get_instance_proc_addr);

    const vk::ApplicationInfo app_info{
        .pApplicationName = "raikopon-launcher",
        .applicationVersion = 1,
        .pEngineName = "gpu_canvas",
        .engineVersion = 1,
        .apiVersion = kTargetApiVersion,
    };
    const std::array<const char*, 2> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_NN_VI_SURFACE_EXTENSION_NAME,
    };
    const vk::InstanceCreateInfo instance_ci{
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    try {
        instance = vk::createInstanceUnique(instance_ci);
    } catch (const vk::SystemError&) {
        return false;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
    return true;
}

bool GpuCanvas::Impl::CreateSurface() {
    const vk::ViSurfaceCreateInfoNN surface_ci{
        .window = static_cast<void*>(window),
    };
    try {
        surface = instance->createViSurfaceNNUnique(surface_ci);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::SelectPhysicalDevice() {
    std::vector<vk::PhysicalDevice> candidates;
    try {
        candidates = instance->enumeratePhysicalDevices();
    } catch (const vk::SystemError&) {
        return false;
    }

    try {
        for (const vk::PhysicalDevice& candidate : candidates) {
            const auto families = candidate.getQueueFamilyProperties();
            for (std::uint32_t i = 0; i < families.size(); ++i) {
                const bool graphics_capable =
                    (families[i].queueFlags & vk::QueueFlagBits::eGraphics) ==
                    vk::QueueFlagBits::eGraphics;
                if (!graphics_capable) {
                    continue;
                }
                if (candidate.getSurfaceSupportKHR(i, *surface) == VK_FALSE) {
                    continue;
                }
                physical_device = candidate;
                queue_family = i;
                const vk::SampleCountFlags sample_counts =
                    physical_device.getProperties().limits.framebufferColorSampleCounts;
                if (sample_counts & vk::SampleCountFlagBits::e4) {
                    msaa_samples = vk::SampleCountFlagBits::e4;
                } else if (sample_counts & vk::SampleCountFlagBits::e2) {
                    msaa_samples = vk::SampleCountFlagBits::e2;
                } else {
                    msaa_samples = vk::SampleCountFlagBits::e1;
                }
                return true;
            }
        }
    } catch (const vk::SystemError&) {
        return false;
    }
    return false;
}

bool GpuCanvas::Impl::CreateLogicalDevice() {
    const float priority = 1.0f;
    const vk::DeviceQueueCreateInfo queue_ci{
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const std::array<const char*, 1> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const vk::DeviceCreateInfo device_ci{
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        .enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    try {
        device = physical_device.createDeviceUnique(device_ci);
    } catch (const vk::SystemError&) {
        return false;
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
    queue = device->getQueue(queue_family, 0);
    return true;
}

bool GpuCanvas::Impl::CreateSwapchain() {
    vk::SurfaceCapabilitiesKHR caps;
    std::vector<vk::SurfaceFormatKHR> formats;
    try {
        caps = physical_device.getSurfaceCapabilitiesKHR(*surface);
        formats = physical_device.getSurfaceFormatsKHR(*surface);
    } catch (const vk::SystemError&) {
        return false;
    }
    if (formats.empty()) {
        return false;
    }

    vk::ColorSpaceKHR color_space = formats.front().colorSpace;
    format = formats.front().format;
    for (const vk::SurfaceFormatKHR& candidate : formats) {
        if (candidate.format == vk::Format::eB8G8R8A8Unorm &&
            candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            format = candidate.format;
            color_space = candidate.colorSpace;
            break;
        }
    }

    extent = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent : caps.minImageExtent;

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    const vk::SwapchainCreateInfoKHR swapchain_ci{
        .surface = *surface,
        .minImageCount = image_count,
        .imageFormat = format,
        .imageColorSpace = color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = caps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = vk::PresentModeKHR::eFifo,
        .clipped = true,
    };

    try {
        swapchain = device->createSwapchainKHRUnique(swapchain_ci);
        images = device->getSwapchainImagesKHR(*swapchain);
    } catch (const vk::SystemError&) {
        return false;
    }

    image_views.clear();
    image_views.reserve(images.size());
    for (const vk::Image& image : images) {
        const vk::ImageViewCreateInfo view_ci{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        try {
            image_views.push_back(device->createImageViewUnique(view_ci));
        } catch (const vk::SystemError&) {
            return false;
        }
    }
    return true;
}

bool GpuCanvas::Impl::CreateRenderPass() {
    const std::array<vk::AttachmentDescription, 2> attachments{
        vk::AttachmentDescription{
            .format = format,
            .samples = msaa_samples,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
        },
        vk::AttachmentDescription{
            .format = format,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::ePresentSrcKHR,
        },
    };
    const vk::AttachmentReference color_ref{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::AttachmentReference resolve_ref{
        .attachment = 1,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pResolveAttachments = &resolve_ref,
    };
    const vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = {},
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
    };
    const vk::RenderPassCreateInfo render_pass_ci{
        .attachmentCount = static_cast<std::uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    try {
        render_pass = device->createRenderPassUnique(render_pass_ci);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::CreateMsaaTargets() {
    msaa_targets.clear();
    msaa_targets.resize(image_views.size());
    for (MsaaTarget& target : msaa_targets) {
        const vk::ImageCreateInfo image_ci{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = msaa_samples,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eTransientAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        try {
            target.image = device->createImageUnique(image_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::MemoryRequirements mem_reqs = device->getImageMemoryRequirements(*target.image);
        std::uint32_t memory_type = FindMemoryType(
            mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal |
                                         vk::MemoryPropertyFlagBits::eLazilyAllocated);
        if (memory_type == UINT32_MAX) {
            memory_type =
                FindMemoryType(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        }
        if (memory_type == UINT32_MAX) {
            return false;
        }
        const vk::MemoryAllocateInfo alloc_info{
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = memory_type,
        };
        try {
            target.memory = device->allocateMemoryUnique(alloc_info);
            device->bindImageMemory(*target.image, *target.memory, 0);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::ImageViewCreateInfo view_ci{
            .image = *target.image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        try {
            target.view = device->createImageViewUnique(view_ci);
        } catch (const vk::SystemError&) {
            return false;
        }
    }
    return true;
}

bool GpuCanvas::Impl::CreateFramebuffers() {
    framebuffers.clear();
    framebuffers.reserve(image_views.size());
    for (std::size_t i = 0; i < image_views.size(); ++i) {
        const std::array<vk::ImageView, 2> attachments{*msaa_targets[i].view, *image_views[i]};
        const vk::FramebufferCreateInfo framebuffer_ci{
            .renderPass = *render_pass,
            .attachmentCount = static_cast<std::uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };
        try {
            framebuffers.push_back(device->createFramebufferUnique(framebuffer_ci));
        } catch (const vk::SystemError&) {
            return false;
        }
    }
    return true;
}

bool GpuCanvas::Impl::CreateCommandObjects() {
    const vk::CommandPoolCreateInfo pool_ci{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queue_family,
    };
    try {
        command_pool = device->createCommandPoolUnique(pool_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::CommandBufferAllocateInfo alloc_info{
        .commandPool = *command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = static_cast<std::uint32_t>(images.size()),
    };
    try {
        command_buffers = device->allocateCommandBuffers(alloc_info);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::CreateSyncObjects() {
    image_available.clear();
    render_finished.clear();
    in_flight.clear();
    try {
        for (std::uint32_t i = 0; i < images.size(); ++i) {
            image_available.push_back(device->createSemaphoreUnique({}));
            render_finished.push_back(device->createSemaphoreUnique({}));
            const vk::FenceCreateInfo fence_ci{.flags = vk::FenceCreateFlagBits::eSignaled};
            in_flight.push_back(device->createFenceUnique(fence_ci));
        }
    } catch (const vk::SystemError&) {
        return false;
    }
    images_in_flight.assign(images.size(), vk::Fence{});
    return true;
}

std::uint32_t GpuCanvas::Impl::FindMemoryType(std::uint32_t type_bits,
                                              vk::MemoryPropertyFlags properties) {
    const vk::PhysicalDeviceMemoryProperties mem_props = physical_device.getMemoryProperties();
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        const bool type_matches = (type_bits & (1u << i)) != 0;
        const bool props_match =
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties;
        if (type_matches && props_match) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool GpuCanvas::Impl::CreatePipeline() {
    const vk::DescriptorSetLayoutBinding sampler_binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    };
    const vk::DescriptorSetLayoutCreateInfo set_layout_ci{
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };
    try {
        descriptor_set_layout = device->createDescriptorSetLayoutUnique(set_layout_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::PushConstantRange push_range{
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .offset = 0,
        .size = sizeof(float) * 2,
    };
    const vk::DescriptorSetLayout set_layout = *descriptor_set_layout;
    const vk::PipelineLayoutCreateInfo pipeline_layout_ci{
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    try {
        pipeline_layout = device->createPipelineLayoutUnique(pipeline_layout_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::ShaderModuleCreateInfo vert_ci{
        .codeSize = sizeof(kCanvasQuadVertSpv),
        .pCode = kCanvasQuadVertSpv,
    };
    const vk::ShaderModuleCreateInfo frag_ci{
        .codeSize = sizeof(kCanvasQuadFragSpv),
        .pCode = kCanvasQuadFragSpv,
    };
    vk::UniqueShaderModule vert_module;
    vk::UniqueShaderModule frag_module;
    try {
        vert_module = device->createShaderModuleUnique(vert_ci);
        frag_module = device->createShaderModuleUnique(frag_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vert_module,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *frag_module,
            .pName = "main",
        },
    };

    const vk::VertexInputBindingDescription binding_desc{
        .binding = 0,
        .stride = sizeof(QuadVertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };
    const std::array<vk::VertexInputAttributeDescription, 4> attribute_descs{
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(QuadVertex, x)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat,
                                            offsetof(QuadVertex, u)},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32A32Sfloat,
                                            offsetof(QuadVertex, r)},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32Sfloat,
                                            offsetof(QuadVertex, mode)},
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input_ci{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attribute_descs.size()),
        .pVertexAttributeDescriptions = attribute_descs.data(),
    };

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly_ci{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    const vk::PipelineViewportStateCreateInfo viewport_ci{
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const vk::PipelineRasterizationStateCreateInfo rasterization_ci{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisample_ci{
        .rasterizationSamples = msaa_samples,
    };

    const vk::PipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = true,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blend_ci{
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    const std::array<vk::DynamicState, 2> dynamic_states{vk::DynamicState::eViewport,
                                                          vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_state_ci{
        .dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::GraphicsPipelineCreateInfo pipeline_ci{
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input_ci,
        .pInputAssemblyState = &input_assembly_ci,
        .pViewportState = &viewport_ci,
        .pRasterizationState = &rasterization_ci,
        .pMultisampleState = &multisample_ci,
        .pColorBlendState = &color_blend_ci,
        .pDynamicState = &dynamic_state_ci,
        .layout = *pipeline_layout,
        .renderPass = *render_pass,
        .subpass = 0,
    };

    try {
        auto result = device->createGraphicsPipelineUnique(nullptr, pipeline_ci);
        pipeline = std::move(result.value);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::CreateAtlasResources(std::uint32_t width, std::uint32_t height) {
    atlas_width = width;
    atlas_height = height;

    const vk::ImageCreateInfo image_ci{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    try {
        atlas_image = device->createImageUnique(image_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::MemoryRequirements mem_reqs = device->getImageMemoryRequirements(*atlas_image);
    const std::uint32_t memory_type =
        FindMemoryType(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    if (memory_type == UINT32_MAX) {
        return false;
    }
    const vk::MemoryAllocateInfo alloc_info{
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = memory_type,
    };
    try {
        atlas_memory = device->allocateMemoryUnique(alloc_info);
        device->bindImageMemory(*atlas_image, *atlas_memory, 0);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::ImageViewCreateInfo view_ci{
        .image = *atlas_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .subresourceRange =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    const vk::SamplerCreateInfo sampler_ci{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatTransparentBlack,
    };
    try {
        atlas_view = device->createImageViewUnique(view_ci);
        sampler = device->createSamplerUnique(sampler_ci);
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1 + kCaptureSlotCount,
    };
    const vk::DescriptorPoolCreateInfo pool_ci{
        .maxSets = 1 + kCaptureSlotCount,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    try {
        descriptor_pool = device->createDescriptorPoolUnique(pool_ci);
        const vk::DescriptorSetLayout set_layout = *descriptor_set_layout;
        const vk::DescriptorSetAllocateInfo set_alloc{
            .descriptorPool = *descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &set_layout,
        };
        descriptor_set = device->allocateDescriptorSets(set_alloc).front();
    } catch (const vk::SystemError&) {
        return false;
    }

    const vk::DescriptorImageInfo image_info{
        .sampler = *sampler,
        .imageView = *atlas_view,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::WriteDescriptorSet write{
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &image_info,
    };
    device->updateDescriptorSets(1, &write, 0, nullptr);
    return true;
}

bool GpuCanvas::Impl::CreateOneVertexBuffer(VertexBufferSlot& slot) {
    const vk::BufferCreateInfo buffer_ci{
        .size = sizeof(QuadVertex) * kMaxQuadVertices,
        .usage = vk::BufferUsageFlagBits::eVertexBuffer,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    try {
        slot.buffer = device->createBufferUnique(buffer_ci);
        const vk::MemoryRequirements mem_reqs = device->getBufferMemoryRequirements(*slot.buffer);
        const std::uint32_t memory_type =
            FindMemoryType(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible |
                                                        vk::MemoryPropertyFlagBits::eHostCoherent);
        if (memory_type == UINT32_MAX) {
            return false;
        }
        const vk::MemoryAllocateInfo alloc_info{
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = memory_type,
        };
        slot.memory = device->allocateMemoryUnique(alloc_info);
        device->bindBufferMemory(*slot.buffer, *slot.memory, 0);
        slot.mapped = device->mapMemory(*slot.memory, 0, buffer_ci.size);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::CreateVertexBuffers() {
    vertex_buffers.clear();
    vertex_buffers.resize(images.size());
    for (VertexBufferSlot& slot : vertex_buffers) {
        if (!CreateOneVertexBuffer(slot)) {
            return false;
        }
    }
    return true;
}

bool GpuCanvas::Impl::CreateCaptureRenderPass() {
    const std::array<vk::AttachmentDescription, 2> attachments{
        vk::AttachmentDescription{
            .format = format,
            .samples = msaa_samples,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
        },
        vk::AttachmentDescription{
            .format = format,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
    };
    const vk::AttachmentReference color_ref{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::AttachmentReference resolve_ref{
        .attachment = 1,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pResolveAttachments = &resolve_ref,
    };
    const vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = {},
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
    };
    const vk::SubpassDependency exit_dependency{
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
    };
    const std::array<vk::SubpassDependency, 2> dependencies{dependency, exit_dependency};
    const vk::RenderPassCreateInfo render_pass_ci{
        .attachmentCount = static_cast<std::uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<std::uint32_t>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };

    try {
        capture_render_pass = device->createRenderPassUnique(render_pass_ci);
    } catch (const vk::SystemError&) {
        return false;
    }
    return true;
}

bool GpuCanvas::Impl::CreateCaptureTargets() {
    for (CaptureTarget& target : capture_targets) {
        const vk::ImageCreateInfo image_ci{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {kCaptureImageWidth, kCaptureImageHeight, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        try {
            target.image = device->createImageUnique(image_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::MemoryRequirements mem_reqs = device->getImageMemoryRequirements(*target.image);
        const std::uint32_t memory_type =
            FindMemoryType(mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        if (memory_type == UINT32_MAX) {
            return false;
        }
        const vk::MemoryAllocateInfo alloc_info{
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = memory_type,
        };
        try {
            target.memory = device->allocateMemoryUnique(alloc_info);
            device->bindImageMemory(*target.image, *target.memory, 0);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::ImageViewCreateInfo view_ci{
            .image = *target.image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        try {
            target.view = device->createImageViewUnique(view_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::ImageCreateInfo msaa_image_ci{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {kCaptureImageWidth, kCaptureImageHeight, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = msaa_samples,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eTransientAttachment,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        try {
            target.msaa_image = device->createImageUnique(msaa_image_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::MemoryRequirements msaa_mem_reqs =
            device->getImageMemoryRequirements(*target.msaa_image);
        std::uint32_t msaa_memory_type = FindMemoryType(
            msaa_mem_reqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal |
                                              vk::MemoryPropertyFlagBits::eLazilyAllocated);
        if (msaa_memory_type == UINT32_MAX) {
            msaa_memory_type = FindMemoryType(msaa_mem_reqs.memoryTypeBits,
                                              vk::MemoryPropertyFlagBits::eDeviceLocal);
        }
        if (msaa_memory_type == UINT32_MAX) {
            return false;
        }
        const vk::MemoryAllocateInfo msaa_alloc_info{
            .allocationSize = msaa_mem_reqs.size,
            .memoryTypeIndex = msaa_memory_type,
        };
        try {
            target.msaa_memory = device->allocateMemoryUnique(msaa_alloc_info);
            device->bindImageMemory(*target.msaa_image, *target.msaa_memory, 0);
        } catch (const vk::SystemError&) {
            return false;
        }

        const vk::ImageViewCreateInfo msaa_view_ci{
            .image = *target.msaa_image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        try {
            target.msaa_view = device->createImageViewUnique(msaa_view_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        const std::array<vk::ImageView, 2> framebuffer_attachments{*target.msaa_view,
                                                                   *target.view};
        const vk::FramebufferCreateInfo framebuffer_ci{
            .renderPass = *capture_render_pass,
            .attachmentCount = static_cast<std::uint32_t>(framebuffer_attachments.size()),
            .pAttachments = framebuffer_attachments.data(),
            .width = kCaptureImageWidth,
            .height = kCaptureImageHeight,
            .layers = 1,
        };
        try {
            target.framebuffer = device->createFramebufferUnique(framebuffer_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        try {
            const vk::DescriptorSetLayout set_layout = *descriptor_set_layout;
            const vk::DescriptorSetAllocateInfo set_alloc{
                .descriptorPool = *descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &set_layout,
            };
            target.descriptor_set = device->allocateDescriptorSets(set_alloc).front();
        } catch (const vk::SystemError&) {
            return false;
        }
        const vk::DescriptorImageInfo image_info{
            .sampler = *sampler,
            .imageView = *target.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::WriteDescriptorSet write{
            .dstSet = target.descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_info,
        };
        device->updateDescriptorSets(1, &write, 0, nullptr);

        try {
            const vk::CommandBufferAllocateInfo cmd_alloc{
                .commandPool = *command_pool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1,
            };
            target.command_buffer = device->allocateCommandBuffers(cmd_alloc).front();
            const vk::FenceCreateInfo fence_ci{.flags = vk::FenceCreateFlagBits::eSignaled};
            target.fence = device->createFenceUnique(fence_ci);
        } catch (const vk::SystemError&) {
            return false;
        }

        if (!CreateOneVertexBuffer(target.vertex_buffer)) {
            return false;
        }
    }
    return true;
}

bool GpuCanvas::Impl::RecreateSwapchainObjects() {
    void(device->waitIdle());
    const bool ok = CreateSwapchain() && CreateMsaaTargets() && CreateFramebuffers() &&
                    CreateCommandObjects() && CreateSyncObjects() && CreateVertexBuffers();
    frame_index = 0;
    image_index = 0;
    needs_resize = false;
    return ok;
}

GpuCanvas::GpuCanvas() : impl(std::make_unique<Impl>()) {}

GpuCanvas::~GpuCanvas() {
    Shutdown();
}

bool GpuCanvas::Init() {
    impl->window = nwindowGetDefault();
    if (impl->window == nullptr) {
        return false;
    }
    const bool ok = impl->CreateInstance() && impl->CreateSurface() &&
                    impl->SelectPhysicalDevice() && impl->CreateLogicalDevice() &&
                    impl->CreateSwapchain() && impl->CreateRenderPass() &&
                    impl->CreateCaptureRenderPass() && impl->CreateMsaaTargets() &&
                    impl->CreateFramebuffers() && impl->CreateCommandObjects() &&
                    impl->CreateSyncObjects() && impl->CreatePipeline() &&
                    impl->CreateAtlasResources(kDefaultAtlasSize, kDefaultAtlasSize) &&
                    impl->CreateVertexBuffers() && impl->CreateCaptureTargets();
    impl->valid = ok;
    if (!ok) {
        Shutdown();
        return false;
    }
    impl->last_operation_mode = appletGetOperationMode();
    const std::array<std::uint8_t, 16> white_block{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    UpdateAtlasRegion(0, 0, 2, 2, white_block.data());
    return true;
}

void GpuCanvas::Shutdown() {
    if (impl->device) {
        void(impl->device->waitIdle());
    }
    impl->vertex_buffers.clear();
    for (Impl::CaptureTarget& target : impl->capture_targets) {
        target.vertex_buffer.buffer.reset();
        target.vertex_buffer.memory.reset();
        target.fence.reset();
        target.framebuffer.reset();
        target.view.reset();
        target.image.reset();
        target.memory.reset();
        target.msaa_view.reset();
        target.msaa_image.reset();
        target.msaa_memory.reset();
    }
    impl->capture_render_pass.reset();
    impl->descriptor_pool.reset();
    impl->sampler.reset();
    impl->atlas_view.reset();
    impl->atlas_image.reset();
    impl->atlas_memory.reset();
    impl->atlas_width = 0;
    impl->atlas_height = 0;
    impl->atlas_layout_initialized = false;
    impl->pipeline.reset();
    impl->pipeline_layout.reset();
    impl->descriptor_set_layout.reset();
    impl->in_flight.clear();
    impl->render_finished.clear();
    impl->image_available.clear();
    impl->command_buffers.clear();
    impl->command_pool.reset();
    impl->framebuffers.clear();
    impl->msaa_targets.clear();
    impl->render_pass.reset();
    impl->image_views.clear();
    impl->images.clear();
    impl->swapchain.reset();
    impl->device.reset();
    impl->surface.reset();
    impl->instance.reset();
    impl->window = nullptr;
    impl->valid = false;
}

bool GpuCanvas::IsValid() const {
    return impl->valid;
}

bool GpuCanvas::BeginFrame(CanvasColor clear_color) {
    if (!impl->valid) {
        return false;
    }

    const AppletOperationMode current_mode = appletGetOperationMode();
    if (current_mode != impl->last_operation_mode || impl->needs_resize) {
        impl->last_operation_mode = current_mode;
        if (!impl->RecreateSwapchainObjects()) {
            return false;
        }
    }

    const vk::Fence frame_fence = *impl->in_flight[impl->frame_index];
    if (impl->device->waitForFences(1, &frame_fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
        return false;
    }

    const vk::Semaphore acquire_semaphore = *impl->image_available[impl->frame_index];
    vk::ResultValue<std::uint32_t> acquire{vk::Result::eErrorUnknown, 0};
    try {
        acquire = impl->device->acquireNextImageKHR(*impl->swapchain, UINT64_MAX,
                                                     acquire_semaphore, {});
    } catch (const vk::OutOfDateKHRError&) {
        impl->needs_resize = true;
        return false;
    } catch (const vk::SystemError&) {
        return false;
    }
    if (acquire.result != vk::Result::eSuccess && acquire.result != vk::Result::eSuboptimalKHR) {
        return false;
    }
    if (acquire.result == vk::Result::eSuboptimalKHR) {
        impl->needs_resize = true;
    }
    impl->image_index = acquire.value;

    const vk::Fence image_fence = impl->images_in_flight[impl->image_index];
    if (image_fence && image_fence != frame_fence) {
        if (impl->device->waitForFences(1, &image_fence, VK_TRUE, UINT64_MAX) !=
            vk::Result::eSuccess) {
            return false;
        }
    }
    impl->images_in_flight[impl->image_index] = frame_fence;

    void(impl->device->resetFences(1, &frame_fence));

    vk::CommandBuffer command_buffer = impl->command_buffers[impl->image_index];
    command_buffer.reset();
    const vk::CommandBufferBeginInfo begin_info{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    command_buffer.begin(begin_info);

    const std::array<vk::ClearValue, 2> clear_values{ToClearColor(clear_color),
                                                     ToClearColor(clear_color)};
    const vk::RenderPassBeginInfo render_pass_begin{
        .renderPass = *impl->render_pass,
        .framebuffer = *impl->framebuffers[impl->image_index],
        .renderArea = {.offset = {0, 0}, .extent = impl->extent},
        .clearValueCount = static_cast<std::uint32_t>(clear_values.size()),
        .pClearValues = clear_values.data(),
    };
    command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);

    const vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(impl->extent.width),
        .height = static_cast<float>(impl->extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    impl->current_scissor = vk::Rect2D{.offset = {0, 0}, .extent = impl->extent};
    command_buffer.setViewport(0, 1, &viewport);
    command_buffer.setScissor(0, 1, &impl->current_scissor);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *impl->pipeline);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *impl->pipeline_layout, 0,
                                      1, &impl->descriptor_set, 0, nullptr);

    const float push_constants[2] = {1.0f / static_cast<float>(impl->extent.width),
                                     1.0f / static_cast<float>(impl->extent.height)};
    command_buffer.pushConstants(*impl->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0,
                                 sizeof(push_constants), push_constants);

    const vk::DeviceSize vertex_offset = 0;
    const vk::Buffer vertex_buffer = *impl->vertex_buffers[impl->image_index].buffer;
    command_buffer.bindVertexBuffers(0, 1, &vertex_buffer, &vertex_offset);

    impl->pending_vertex_count = 0;
    impl->flush_start = 0;
    return true;
}

vk::CommandBuffer GpuCanvas::Impl::CurrentCommandBuffer() {
    return capturing ? capture_targets[capture_slot].command_buffer : command_buffers[image_index];
}

GpuCanvas::Impl::VertexBufferSlot& GpuCanvas::Impl::CurrentVertexSlot() {
    return capturing ? capture_targets[capture_slot].vertex_buffer : vertex_buffers[image_index];
}

std::uint32_t& GpuCanvas::Impl::CurrentPendingVertexCount() {
    return capturing ? capture_targets[capture_slot].pending_vertex_count : pending_vertex_count;
}

std::uint32_t& GpuCanvas::Impl::CurrentFlushStart() {
    return capturing ? capture_targets[capture_slot].flush_start : flush_start;
}

void GpuCanvas::Impl::FlushSpan() {
    std::uint32_t& pending = CurrentPendingVertexCount();
    std::uint32_t& start = CurrentFlushStart();
    if (pending <= start) {
        return;
    }
    vk::CommandBuffer command_buffer = CurrentCommandBuffer();
    command_buffer.setScissor(0, 1, &current_scissor);
    command_buffer.draw(pending - start, 1, start, 0);
    start = pending;
}

void GpuCanvas::Impl::EmitQuad(float x, float y, float w, float h, float u0, float v0, float u1,
                               float v1, CanvasColor tint, float rotation_radians, float mode) {
    std::uint32_t& pending = CurrentPendingVertexCount();
    if (pending + 6 > kMaxQuadVertices) {
        return;
    }

    VertexBufferSlot& slot = CurrentVertexSlot();
    auto* verts = static_cast<QuadVertex*>(slot.mapped) + pending;

    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float hx = w * 0.5f;
    const float hy = h * 0.5f;
    const float cos_r = std::cos(rotation_radians);
    const float sin_r = std::sin(rotation_radians);

    const auto rotate = [&](float dx, float dy) -> std::pair<float, float> {
        return {cx + dx * cos_r - dy * sin_r, cy + dx * sin_r + dy * cos_r};
    };

    const auto [x0, y0] = rotate(-hx, -hy);
    const auto [x1, y1] = rotate(hx, -hy);
    const auto [x2, y2] = rotate(-hx, hy);
    const auto [x3, y3] = rotate(hx, hy);

    verts[0] = {x0, y0, u0, v0, tint.r, tint.g, tint.b, tint.a, mode};
    verts[1] = {x1, y1, u1, v0, tint.r, tint.g, tint.b, tint.a, mode};
    verts[2] = {x2, y2, u0, v1, tint.r, tint.g, tint.b, tint.a, mode};
    verts[3] = {x1, y1, u1, v0, tint.r, tint.g, tint.b, tint.a, mode};
    verts[4] = {x3, y3, u1, v1, tint.r, tint.g, tint.b, tint.a, mode};
    verts[5] = {x2, y2, u0, v1, tint.r, tint.g, tint.b, tint.a, mode};

    pending += 6;
}

void GpuCanvas::SetClipRect(float x, float y, float w, float h) {
    if (!impl->valid) {
        return;
    }
    impl->FlushSpan();
    const float clamped_w = std::max(0.0f, w);
    const float clamped_h = std::max(0.0f, h);
    impl->current_scissor = vk::Rect2D{
        .offset = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)},
        .extent = {static_cast<std::uint32_t>(clamped_w), static_cast<std::uint32_t>(clamped_h)},
    };
}

void GpuCanvas::ClearClipRect() {
    if (!impl->valid) {
        return;
    }
    impl->FlushSpan();
    impl->current_scissor = vk::Rect2D{.offset = {0, 0}, .extent = impl->extent};
}

void GpuCanvas::DrawTexturedQuad(float x, float y, float w, float h, float u0, float v0, float u1,
                                 float v1, CanvasColor tint, float rotation_radians,
                                 bool full_color) {
    if (!impl->valid) {
        return;
    }
    impl->EmitQuad(x, y, w, h, u0, v0, u1, v1, tint, rotation_radians, full_color ? 1.0f : 0.0f);
}

void GpuCanvas::DrawQuad(float x, float y, float w, float h, CanvasColor color,
                         float rotation_radians) {
    if (!impl->valid || impl->atlas_width == 0 || impl->atlas_height == 0) {
        return;
    }
    const float u = 0.5f / static_cast<float>(impl->atlas_width);
    const float v = 0.5f / static_cast<float>(impl->atlas_height);
    DrawTexturedQuad(x, y, w, h, u, v, u, v, color, rotation_radians);
}

void GpuCanvas::UpdateAtlasRegion(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                  std::uint32_t height, const std::uint8_t* pixels) {
    if (!impl->valid || width == 0 || height == 0) {
        return;
    }
    // A region whose right/bottom edge would land past the actual atlas image is refused rather
    // than attempted - see GlyphAtlas::ReserveAtlasRegion, whose own bounds check this backs up;
    // writing past the image here would be a genuine out-of-bounds GPU memory write.
    if (x + width > impl->atlas_width || y + height > impl->atlas_height) {
        return;
    }

    const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(width) * height * 4;
    vk::UniqueBuffer staging_buffer;
    vk::UniqueDeviceMemory staging_memory;
    try {
        const vk::BufferCreateInfo staging_ci{
            .size = byte_size,
            .usage = vk::BufferUsageFlagBits::eTransferSrc,
            .sharingMode = vk::SharingMode::eExclusive,
        };
        staging_buffer = impl->device->createBufferUnique(staging_ci);
        const vk::MemoryRequirements mem_reqs =
            impl->device->getBufferMemoryRequirements(*staging_buffer);
        const std::uint32_t memory_type = impl->FindMemoryType(
            mem_reqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        if (memory_type == UINT32_MAX) {
            return;
        }
        const vk::MemoryAllocateInfo alloc_info{
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = memory_type,
        };
        staging_memory = impl->device->allocateMemoryUnique(alloc_info);
        impl->device->bindBufferMemory(*staging_buffer, *staging_memory, 0);
        void* mapped = impl->device->mapMemory(*staging_memory, 0, byte_size);
        std::memcpy(mapped, pixels, static_cast<std::size_t>(byte_size));
        impl->device->unmapMemory(*staging_memory);
    } catch (const vk::SystemError&) {
        return;
    }

    vk::CommandBuffer cmd;
    try {
        const vk::CommandBufferAllocateInfo cmd_alloc{
            .commandPool = *impl->command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };
        cmd = impl->device->allocateCommandBuffers(cmd_alloc).front();
    } catch (const vk::SystemError&) {
        return;
    }

    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    const vk::ImageLayout old_layout = impl->atlas_layout_initialized
                                           ? vk::ImageLayout::eShaderReadOnlyOptimal
                                           : vk::ImageLayout::eUndefined;
    const vk::AccessFlags src_access =
        impl->atlas_layout_initialized ? vk::AccessFlagBits::eShaderRead : vk::AccessFlags{};
    const vk::PipelineStageFlags src_stage = impl->atlas_layout_initialized
                                                 ? vk::PipelineStageFlagBits::eFragmentShader
                                                 : vk::PipelineStageFlagBits::eTopOfPipe;

    const vk::ImageMemoryBarrier to_transfer{
        .srcAccessMask = src_access,
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = old_layout,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *impl->atlas_image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmd.pipelineBarrier(src_stage, vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0,
                        nullptr, 1, &to_transfer);

    constexpr std::uint32_t kMaxTileEdge = 48;
    std::vector<vk::BufferImageCopy> regions;
    for (std::uint32_t ty = 0; ty < height; ty += kMaxTileEdge) {
        const std::uint32_t tile_h = std::min(kMaxTileEdge, height - ty);
        for (std::uint32_t tx = 0; tx < width; tx += kMaxTileEdge) {
            const std::uint32_t tile_w = std::min(kMaxTileEdge, width - tx);
            regions.push_back(vk::BufferImageCopy{
                .bufferOffset = (static_cast<vk::DeviceSize>(ty) * width + tx) * 4,
                .bufferRowLength = width,
                .bufferImageHeight = height,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageOffset = {static_cast<std::int32_t>(x + tx), static_cast<std::int32_t>(y + ty),
                               0},
                .imageExtent = {tile_w, tile_h, 1},
            });
        }
    }
    cmd.copyBufferToImage(*staging_buffer, *impl->atlas_image, vk::ImageLayout::eTransferDstOptimal,
                          static_cast<std::uint32_t>(regions.size()), regions.data());

    const vk::ImageMemoryBarrier to_shader_read{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *impl->atlas_image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1,
                        &to_shader_read);

    cmd.end();

    vk::UniqueFence fence;
    try {
        fence = impl->device->createFenceUnique(vk::FenceCreateInfo{});
    } catch (const vk::SystemError&) {
        impl->device->freeCommandBuffers(*impl->command_pool, 1, &cmd);
        return;
    }

    const vk::SubmitInfo submit{
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    if (impl->queue.submit(1, &submit, *fence) != vk::Result::eSuccess) {
        impl->device->freeCommandBuffers(*impl->command_pool, 1, &cmd);
        return;
    }
    const vk::Fence raw_fence = *fence;
    void(impl->device->waitForFences(1, &raw_fence, VK_TRUE, UINT64_MAX));
    impl->device->freeCommandBuffers(*impl->command_pool, 1, &cmd);

    if (static_cast<std::uint64_t>(width) * height > 5000) {
        svcSleepThread(100'000'000);
    }

    impl->atlas_layout_initialized = true;
}

std::uint32_t GpuCanvas::AtlasWidth() const {
    return impl->atlas_width;
}

std::uint32_t GpuCanvas::AtlasHeight() const {
    return impl->atlas_height;
}

void GpuCanvas::EndFrame() {
    if (!impl->valid) {
        return;
    }
    impl->FlushSpan();
    vk::CommandBuffer command_buffer = impl->command_buffers[impl->image_index];
    command_buffer.endRenderPass();
    command_buffer.end();

    const vk::Semaphore wait_semaphore = *impl->image_available[impl->frame_index];
    const vk::Semaphore signal_semaphore = *impl->render_finished[impl->frame_index];
    const vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    const vk::SubmitInfo submit_info{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &signal_semaphore,
    };
    void(impl->queue.submit(1, &submit_info, *impl->in_flight[impl->frame_index]));

    const vk::SwapchainKHR swapchain = *impl->swapchain;
    const vk::PresentInfoKHR present_info{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &signal_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &impl->image_index,
    };
    try {
        if (impl->queue.presentKHR(present_info) == vk::Result::eSuboptimalKHR) {
            impl->needs_resize = true;
        }
    } catch (const vk::OutOfDateKHRError&) {
        impl->needs_resize = true;
    } catch (const vk::SystemError&) {
    }

    impl->frame_index = (impl->frame_index + 1) % static_cast<std::uint32_t>(impl->images.size());
}

bool GpuCanvas::BeginCapture(std::uint32_t slot, CanvasColor clear_color) {
    if (!impl->valid || slot >= kCaptureSlotCount || impl->capturing) {
        return false;
    }
    GpuCanvas::Impl::CaptureTarget& target = impl->capture_targets[slot];

    const vk::Fence fence = *target.fence;
    if (impl->device->waitForFences(1, &fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
        return false;
    }
    void(impl->device->resetFences(1, &fence));

    target.command_buffer.reset();
    const vk::CommandBufferBeginInfo begin_info{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    target.command_buffer.begin(begin_info);

    const std::array<vk::ClearValue, 2> clear_values{ToClearColor(clear_color),
                                                     ToClearColor(clear_color)};
    const vk::RenderPassBeginInfo render_pass_begin{
        .renderPass = *impl->capture_render_pass,
        .framebuffer = *target.framebuffer,
        .renderArea = {.offset = {0, 0}, .extent = impl->extent},
        .clearValueCount = static_cast<std::uint32_t>(clear_values.size()),
        .pClearValues = clear_values.data(),
    };
    target.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);

    const vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(impl->extent.width),
        .height = static_cast<float>(impl->extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    impl->current_scissor = vk::Rect2D{.offset = {0, 0}, .extent = impl->extent};
    target.command_buffer.setViewport(0, 1, &viewport);
    target.command_buffer.setScissor(0, 1, &impl->current_scissor);

    target.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *impl->pipeline);
    target.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             *impl->pipeline_layout, 0, 1, &impl->descriptor_set, 0,
                                             nullptr);

    const float push_constants[2] = {1.0f / static_cast<float>(impl->extent.width),
                                     1.0f / static_cast<float>(impl->extent.height)};
    target.command_buffer.pushConstants(*impl->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0,
                                        sizeof(push_constants), push_constants);

    const vk::DeviceSize vertex_offset = 0;
    const vk::Buffer vertex_buffer = *target.vertex_buffer.buffer;
    target.command_buffer.bindVertexBuffers(0, 1, &vertex_buffer, &vertex_offset);

    target.captured_width = impl->extent.width;
    target.captured_height = impl->extent.height;
    target.pending_vertex_count = 0;
    target.flush_start = 0;

    impl->capturing = true;
    impl->capture_slot = slot;
    return true;
}

void GpuCanvas::EndCapture() {
    if (!impl->valid || !impl->capturing) {
        return;
    }
    impl->FlushSpan();
    GpuCanvas::Impl::CaptureTarget& target = impl->capture_targets[impl->capture_slot];
    target.command_buffer.endRenderPass();
    target.command_buffer.end();

    const vk::SubmitInfo submit_info{
        .commandBufferCount = 1,
        .pCommandBuffers = &target.command_buffer,
    };
    void(impl->queue.submit(1, &submit_info, *target.fence));
    const vk::Fence fence = *target.fence;
    void(impl->device->waitForFences(1, &fence, VK_TRUE, UINT64_MAX));

    impl->capturing = false;
    impl->current_scissor = vk::Rect2D{.offset = {0, 0}, .extent = impl->extent};
}

void GpuCanvas::DrawCaptureQuad(std::uint32_t slot, float x, float y, float w, float h,
                                float rotation_radians, float src_x, float src_y, float src_w,
                                float src_h) {
    if (!impl->valid || impl->capturing || slot >= kCaptureSlotCount) {
        return;
    }
    GpuCanvas::Impl::CaptureTarget& target = impl->capture_targets[slot];
    if (target.captured_width == 0 || target.captured_height == 0) {
        return;
    }
    const float resolved_src_w = src_w >= 0.0f ? src_w : static_cast<float>(target.captured_width);
    const float resolved_src_h = src_h >= 0.0f ? src_h : static_cast<float>(target.captured_height);

    impl->FlushSpan();
    vk::CommandBuffer command_buffer = impl->CurrentCommandBuffer();
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *impl->pipeline_layout, 0,
                                      1, &target.descriptor_set, 0, nullptr);

    const float u0 = src_x / static_cast<float>(kCaptureImageWidth);
    const float v0 = src_y / static_cast<float>(kCaptureImageHeight);
    const float u1 = (src_x + resolved_src_w) / static_cast<float>(kCaptureImageWidth);
    const float v1 = (src_y + resolved_src_h) / static_cast<float>(kCaptureImageHeight);
    impl->EmitQuad(x, y, w, h, u0, v0, u1, v1, CanvasColor{1.0f, 1.0f, 1.0f, 1.0f}, rotation_radians,
                   1.0f);

    impl->FlushSpan();
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *impl->pipeline_layout, 0,
                                      1, &impl->descriptor_set, 0, nullptr);
}

std::uint32_t GpuCanvas::Width() const {
    return impl->extent.width;
}

std::uint32_t GpuCanvas::Height() const {
    return impl->extent.height;
}

} // namespace SwitchFrontend
