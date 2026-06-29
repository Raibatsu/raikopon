// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/custom_textures/material.h"
#include "video_core/rasterizer_cache/sampler_params.h"
#include "video_core/renderer_deko3d/dk_texture_runtime.h"

namespace Deko3D {

using VideoCore::PixelFormat;
using VideoCore::SurfaceType;

namespace {

// Backing for the transfer command buffer that drives copy/blit/clear work.
constexpr u32 TransferCmdBufSize = 64 * 1024;
// Initial size of each staging stream.
// This grows on demand if needed.
constexpr u32 InitialStagingSize = 8 * 1024 * 1024;
// Alignment of each staging sub-allocation handed to the copy engine.
constexpr u32 StagingAlignment = 64;

// Render target and 2D egine usage so every cached image can be cleared.
constexpr u32 DefaultImageFlags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;

constexpr FormatTuple DEFAULT_TUPLE = {DkImageFormat_RGBA8_Unorm, DefaultImageFlags};

// Indexed by VideoCore::PixelFormat for the colour (0-4) and texture (5-13) ranges. The texture formats are all decoded
// to RGBA8 by the rasterizer cache before upload.
constexpr std::array<FormatTuple, 14> COLOR_TUPLES = {{
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // RGBA8
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // RGB8
    {DkImageFormat_RGB5A1_Unorm, DefaultImageFlags},  // RGB5A1
    {DkImageFormat_RGB565_Unorm, DefaultImageFlags},  // RGB565
    {DkImageFormat_RGBA4_Unorm, DefaultImageFlags},   // RGBA4
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // IA8
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // RG8
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // I8
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // A8
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // IA4
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // I4
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // A4
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // ETC1
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags},   // ETC1A4
}};

// Indexed by (PixelFormat - D16). The hole at index 1 is the gap between D16 (14) and D24 (16).
constexpr std::array<FormatTuple, 4> DEPTH_TUPLES = {{
    {DkImageFormat_Z16, DefaultImageFlags},   // D16
    {DkImageFormat_None, 0},       // (unused)
    {DkImageFormat_Z24X8, DefaultImageFlags}, // D24
    {DkImageFormat_Z24S8, DefaultImageFlags}, // D24S8
}};

constexpr std::array<FormatTuple, 8> CUSTOM_TUPLES = {{
    {DkImageFormat_RGBA8_Unorm, DefaultImageFlags}, // RGBA8
    {DkImageFormat_RGBA_BC1, DefaultImageFlags},    // BC1
    {DkImageFormat_RGBA_BC3, DefaultImageFlags},    // BC3
    {DkImageFormat_None, 0},             // BC5 (RGTC2, unsupported)
    {DkImageFormat_RGBA_BC7_Unorm, DefaultImageFlags},  // BC7
    {DkImageFormat_RGBA_ASTC_4x4, DefaultImageFlags},   // ASTC4
    {DkImageFormat_RGBA_ASTC_6x6, DefaultImageFlags},   // ASTC6
    {DkImageFormat_RGBA_ASTC_8x6, DefaultImageFlags},   // ASTC8
}};

constexpr bool IsColorType(SurfaceType type) {
    return type == SurfaceType::Color || type == SurfaceType::Texture || type == SurfaceType::Fill;
}

DkImageLayout MakeLayout(DkDevice device, const FormatTuple& tuple, u32 width, u32 height,
                         u32 levels) {
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, device);
    maker.flags = tuple.usage_flags;
    maker.format = tuple.format;
    maker.dimensions[0] = width;
    maker.dimensions[1] = height;
    maker.mipLevels = levels;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &maker);
    return layout;
}

DkImageRect MakeRect(const Common::Rectangle<u32>& rect, u32 layer = 0) {
    return DkImageRect{rect.left, rect.bottom, layer, rect.GetWidth(), rect.GetHeight(), 1};
}

DkFilter ToDkFilter(Pica::TexturingRegs::TextureConfig::TextureFilter filter) {
    using TextureFilter = Pica::TexturingRegs::TextureConfig::TextureFilter;
    return filter == TextureFilter::Nearest ? DkFilter_Nearest : DkFilter_Linear;
}

DkMipFilter ToDkMipFilter(Pica::TexturingRegs::TextureConfig::TextureFilter filter) {
    using TextureFilter = Pica::TexturingRegs::TextureConfig::TextureFilter;
    return filter == TextureFilter::Nearest ? DkMipFilter_Nearest : DkMipFilter_Linear;
}

DkWrapMode ToDkWrapMode(Pica::TexturingRegs::TextureConfig::WrapMode wrap) {
    using WrapMode = Pica::TexturingRegs::TextureConfig::WrapMode;
    switch (wrap) {
    case WrapMode::ClampToEdge:
    case WrapMode::ClampToEdge2:
        return DkWrapMode_ClampToEdge;
    case WrapMode::ClampToBorder:
    case WrapMode::ClampToBorder2:
        return DkWrapMode_ClampToBorder;
    case WrapMode::Repeat:
    case WrapMode::Repeat2:
    case WrapMode::Repeat3:
        return DkWrapMode_Repeat;
    case WrapMode::MirroredRepeat:
        return DkWrapMode_MirroredRepeat;
    default:
        return DkWrapMode_ClampToEdge;
    }
}

} // Anonymous namespace

TextureRuntime::TextureRuntime(DkDevice device_, DkQueue queue_) : device{device_}, queue{queue_} {
    image_pool =
        std::make_unique<MemoryPool>(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    data_pool = std::make_unique<MemoryPool>(
        device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, TransferCmdBufSize);
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    cmdbuf_memblock = dkMemBlockCreate(&maker);

    DkCmdBufMaker cmdbuf_maker;
    dkCmdBufMakerDefaults(&cmdbuf_maker, device);
    transfer_cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    dkCmdBufAddMemory(transfer_cmdbuf, cmdbuf_memblock, 0, TransferCmdBufSize);

    CreateStaging(upload_buffer, InitialStagingSize);
    CreateStaging(download_buffer, InitialStagingSize);
}

TextureRuntime::~TextureRuntime() {
    if (queue != nullptr) {
        dkQueueWaitIdle(queue);
    }
    if (transfer_cmdbuf != nullptr) {
        dkCmdBufDestroy(transfer_cmdbuf);
    }
    if (cmdbuf_memblock != nullptr) {
        dkMemBlockDestroy(cmdbuf_memblock);
    }
    if (upload_buffer.memblock != nullptr) {
        dkMemBlockDestroy(upload_buffer.memblock);
    }
    if (download_buffer.memblock != nullptr) {
        dkMemBlockDestroy(download_buffer.memblock);
    }
}

void TextureRuntime::CreateStaging(StagingBuffer& buffer, u32 size) {
    size = AlignUp(size, DK_MEMBLOCK_ALIGNMENT);

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, size);
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    buffer.memblock = dkMemBlockCreate(&maker);
    buffer.cpu_addr = static_cast<u8*>(dkMemBlockGetCpuAddr(buffer.memblock));
    buffer.gpu_addr = dkMemBlockGetGpuAddr(buffer.memblock);
    buffer.size = size;
    buffer.offset = 0;
}

void TextureRuntime::EnsureStaging(StagingBuffer& buffer, u32 size) {
    if (size <= buffer.size) {
        return;
    }
    if (queue != nullptr) {
        dkQueueWaitIdle(queue);
    }
    if (buffer.memblock != nullptr) {
        dkMemBlockDestroy(buffer.memblock);
    }
    CreateStaging(buffer, std::max(size, buffer.size * 2));
}

DkCmdBuf TextureRuntime::TransferBegin() {
    dkCmdBufClear(transfer_cmdbuf);
    dkCmdBufAddMemory(transfer_cmdbuf, cmdbuf_memblock, 0, TransferCmdBufSize);
    return transfer_cmdbuf;
}

void TextureRuntime::TransferSubmit() {
    dkQueueSubmitCommands(queue, dkCmdBufFinishList(transfer_cmdbuf));
    dkQueueWaitIdle(queue);
}

u32 TextureRuntime::RemoveThreshold() {
    return 8;
}

void TextureRuntime::Finish() {
    if (queue != nullptr) {
        dkQueueWaitIdle(queue);
    }
}

bool TextureRuntime::NeedsConversion(const Surface& surface) const {
    const PixelFormat format = surface.pixel_format;
    // RGBA8 needs a byteswap and RGB8 must be expanded to RGBA8.
    return format == PixelFormat::RGBA8 || format == PixelFormat::RGB8;
}

VideoCore::StagingData TextureRuntime::FindStaging(u32 size, bool upload) {
    StagingBuffer& buffer = upload ? upload_buffer : download_buffer;
    EnsureStaging(buffer, size);

    u32 offset = AlignUp(buffer.offset, StagingAlignment);
    if (offset + size > buffer.size) {
        offset = 0;
    }
    buffer.offset = offset + size;

    return VideoCore::StagingData{
        .size = size,
        .offset = offset,
        .mapped = std::span{buffer.cpu_addr + offset, size},
    };
}

const FormatTuple& TextureRuntime::GetFormatTuple(PixelFormat pixel_format) const {
    if (pixel_format == PixelFormat::Invalid) {
        return DEFAULT_TUPLE;
    }

    const SurfaceType type = GetFormatType(pixel_format);
    const std::size_t index = static_cast<std::size_t>(pixel_format);

    if (type == SurfaceType::Color || type == SurfaceType::Texture) {
        ASSERT(index < COLOR_TUPLES.size());
        return COLOR_TUPLES[index];
    }
    if (type == SurfaceType::Depth || type == SurfaceType::DepthStencil) {
        const std::size_t depth_index = index - static_cast<std::size_t>(PixelFormat::D16);
        ASSERT(depth_index < DEPTH_TUPLES.size());
        return DEPTH_TUPLES[depth_index];
    }
    return DEFAULT_TUPLE;
}

const FormatTuple& TextureRuntime::GetFormatTuple(VideoCore::CustomPixelFormat pixel_format) const {
    const std::size_t index = static_cast<std::size_t>(pixel_format);
    if (index >= CUSTOM_TUPLES.size()) {
        return DEFAULT_TUPLE;
    }
    return CUSTOM_TUPLES[index];
}

bool TextureRuntime::Reinterpret(Surface& source, Surface& dest,
                                 const VideoCore::TextureCopy& copy) {
    if (source.pixel_format == dest.pixel_format) {
        return CopyTextures(source, dest, copy);
    }
    // Cross-format reinterpretation needs conversion shaders.
    // TODO: ^
    LOG_WARNING(Render, "Deko3d: unimplemented reinterpretation {} -> {}",
                VideoCore::PixelFormatAsString(source.pixel_format),
                VideoCore::PixelFormatAsString(dest.pixel_format));
    return false;
}

void TextureRuntime::ClearTexture(Surface& surface, const VideoCore::TextureClear& clear) {
    if (surface.Image() == nullptr) {
        return;
    }

    DkImageView view = surface.View(true, clear.texture_level);
    const auto& rect = clear.texture_rect;

    DkCmdBuf cmdbuf = TransferBegin();
    const DkViewport viewport = {static_cast<float>(rect.left), static_cast<float>(rect.bottom),
                                 static_cast<float>(rect.GetWidth()),
                                 static_cast<float>(rect.GetHeight()), 0.0f, 1.0f};
    const DkScissor scissor = {rect.left, rect.bottom, rect.GetWidth(), rect.GetHeight()};
    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);

    if (IsColorType(surface.type)) {
        dkCmdBufBindRenderTarget(cmdbuf, &view, nullptr);
        const auto& color = clear.value.color;
        dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, color.r(), color.g(), color.b(),
                                color.a());
    } else {
        dkCmdBufBindRenderTarget(cmdbuf, nullptr, &view);
        dkCmdBufClearDepthStencil(cmdbuf, true, clear.value.depth, 0xFF, clear.value.stencil);
    }
    TransferSubmit();
}

bool TextureRuntime::CopyTextures(Surface& source, Surface& dest,
                                  std::span<const VideoCore::TextureCopy> copies) {
    if (source.Image() == nullptr || dest.Image() == nullptr) {
        return false;
    }

    DkCmdBuf cmdbuf = TransferBegin();
    for (const auto& copy : copies) {
        DkImageView src_view = source.View(true, copy.src_level);
        DkImageView dst_view = dest.View(true, copy.dst_level);
        const DkImageRect src_rect = {copy.src_offset.x, copy.src_offset.y, copy.src_layer,
                                      copy.extent.width, copy.extent.height, 1};
        const DkImageRect dst_rect = {copy.dst_offset.x, copy.dst_offset.y, copy.dst_layer,
                                      copy.extent.width, copy.extent.height, 1};
        dkCmdBufCopyImage(cmdbuf, &src_view, &src_rect, &dst_view, &dst_rect, 0);
    }
    TransferSubmit();
    return true;
}

bool TextureRuntime::BlitTextures(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    if (source.Image() == nullptr || dest.Image() == nullptr) {
        return false;
    }

    DkImageView src_view = source.View(true, blit.src_level);
    DkImageView dst_view = dest.View(true, blit.dst_level);
    const DkImageRect src_rect = MakeRect(blit.src_rect, blit.src_layer);
    const DkImageRect dst_rect = MakeRect(blit.dst_rect, blit.dst_layer);
    const u32 flags = IsColorType(source.type) ? DkBlitFlag_FilterLinear : DkBlitFlag_FilterNearest;

    DkCmdBuf cmdbuf = TransferBegin();
    dkCmdBufBlitImage(cmdbuf, &src_view, &src_rect, &dst_view, &dst_rect, flags, 0);
    TransferSubmit();
    return true;
}

void TextureRuntime::GenerateMipmaps(Surface& surface) {
    if (surface.levels <= 1 || surface.Image() == nullptr) {
        return;
    }

    DkImage* image = surface.Image(true);
    const u32 flags =
        IsColorType(surface.type) ? DkBlitFlag_FilterLinear : DkBlitFlag_FilterNearest;
    const u32 base_width = surface.GetScaledWidth();
    const u32 base_height = surface.GetScaledHeight();

    for (u32 level = 1; level < surface.levels; ++level) {
        DkImageView src_view;
        dkImageViewDefaults(&src_view, image);
        src_view.mipLevelOffset = level - 1;
        src_view.mipLevelCount = 1;

        DkImageView dst_view;
        dkImageViewDefaults(&dst_view, image);
        dst_view.mipLevelOffset = level;
        dst_view.mipLevelCount = 1;

        const DkImageRect src_rect = {0, 0, 0, std::max(base_width >> (level - 1), 1u),
                                      std::max(base_height >> (level - 1), 1u), 1};
        const DkImageRect dst_rect = {0, 0, 0, std::max(base_width >> level, 1u),
                                      std::max(base_height >> level, 1u), 1};

        DkCmdBuf cmdbuf = TransferBegin();
        dkCmdBufBlitImage(cmdbuf, &src_view, &src_rect, &dst_view, &dst_rect, flags, 0);
        TransferSubmit();
    }
}

Surface::Surface(TextureRuntime& runtime_, const VideoCore::SurfaceParams& params,
                 const VideoCore::SurfaceFlagBits& initial_flag_bits)
    : SurfaceBase{params, initial_flag_bits}, runtime{&runtime_},
      tuple{runtime_.GetFormatTuple(pixel_format)} {
    if (pixel_format == PixelFormat::Invalid || tuple.format == DkImageFormat_None) {
        return;
    }

    Alloc(image, allocation, width, height, levels);
    if (res_scale != 1) {
        Alloc(scaled_image, scaled_allocation, GetScaledWidth(), GetScaledHeight(), levels);
    }
}

Surface::Surface(TextureRuntime& runtime_, const VideoCore::SurfaceBase& surface,
                 const VideoCore::Material* mat)
    : SurfaceBase{surface, {}}, runtime{&runtime_}, tuple{runtime_.GetFormatTuple(mat->format)} {
    custom_format = mat->format;
    material = mat;
    if (tuple.format == DkImageFormat_None) {
        return;
    }
    Alloc(image, allocation, mat->width, mat->height, levels);
}

Surface::~Surface() = default;

void Surface::Alloc(std::unique_ptr<DkImage>& out_image, MemoryAllocation& out_alloc, u32 width_,
                    u32 height_, u32 levels_) {
    const DkImageLayout layout = MakeLayout(runtime->GetDevice(), tuple, width_, height_, levels_);
    const u32 size = static_cast<u32>(dkImageLayoutGetSize(&layout));
    const u32 alignment = dkImageLayoutGetAlignment(&layout);

    out_alloc = runtime->ImagePool().Allocate(size, alignment);
    if (!out_alloc) {
        LOG_ERROR(Render, "Deko3d: failed to allocate {}x{} image ({} bytes)", width_, height_,
                  size);
        return;
    }
    out_image = std::make_unique<DkImage>();
    dkImageInitialize(out_image.get(), &layout, out_alloc.MemBlock(), out_alloc.Offset());
}

DkImageView Surface::View(bool scaled, u32 level) const noexcept {
    DkImageView view;
    dkImageViewDefaults(&view, Image(scaled));
    view.mipLevelOffset = level;
    view.mipLevelCount = 1;
    return view;
}

void Surface::Upload(const VideoCore::BufferTextureCopy& upload,
                     const VideoCore::StagingData& staging) {
    if (!image) {
        return;
    }

    DkImageView view = View(false, upload.texture_level);
    const DkImageRect rect = MakeRect(upload.texture_rect);
    const DkCopyBuf src = {runtime->upload_buffer.gpu_addr + upload.buffer_offset, 0, 0};

    DkCmdBuf cmdbuf = runtime->TransferBegin();
    dkCmdBufCopyBufferToImage(cmdbuf, &src, &view, &rect, 0);
    runtime->TransferSubmit();

    if (res_scale != 1) {
        const VideoCore::TextureBlit blit = {
            .src_level = upload.texture_level,
            .dst_level = upload.texture_level,
            .src_rect = upload.texture_rect,
            .dst_rect = upload.texture_rect * res_scale,
        };
        BlitScale(blit, true);
    }
}

void Surface::UploadCustom(const VideoCore::Material* material_, u32 level) {
    // TODO: Custom textures
    LOG_WARNING(Render, "deko3d: custom texture upload is not implemented");
}

void Surface::Download(const VideoCore::BufferTextureCopy& download,
                       const VideoCore::StagingData& staging) {
    if (!image) {
        return;
    }

    if (res_scale != 1) {
        const VideoCore::TextureBlit blit = {
            .src_level = download.texture_level,
            .dst_level = download.texture_level,
            .src_rect = download.texture_rect * res_scale,
            .dst_rect = download.texture_rect,
        };
        BlitScale(blit, false);
    }

    DkImageView view = View(false, download.texture_level);
    const DkImageRect rect = MakeRect(download.texture_rect);
    const DkCopyBuf dst = {runtime->download_buffer.gpu_addr + download.buffer_offset, 0, 0};

    DkCmdBuf cmdbuf = runtime->TransferBegin();
    dkCmdBufCopyImageToBuffer(cmdbuf, &view, &rect, &dst, 0);
    runtime->TransferSubmit();
}

void Surface::ScaleUp(u32 new_scale) {
    if (res_scale == new_scale || new_scale == 1) {
        return;
    }

    res_scale = new_scale;
    Alloc(scaled_image, scaled_allocation, GetScaledWidth(), GetScaledHeight(), levels);
    if (!scaled_image) {
        return;
    }

    for (u32 level = 0; level < levels; ++level) {
        const VideoCore::TextureBlit blit = {
            .src_level = level,
            .dst_level = level,
            .src_rect = GetRect(level),
            .dst_rect = GetScaledRect(level),
        };
        BlitScale(blit, true);
    }
}

u32 Surface::GetInternalBytesPerPixel() const {
    // Deko3D has no 24-bit colour.
    if (pixel_format == PixelFormat::RGB8) {
        return 4;
    }
    return GetFormatBytesPerPixel(pixel_format);
}

void Surface::BlitScale(const VideoCore::TextureBlit& blit, bool up_scale) {
    if (!image || !scaled_image) {
        return;
    }

    DkImageView src_view = View(!up_scale, blit.src_level);
    DkImageView dst_view = View(up_scale, blit.dst_level);
    const DkImageRect src_rect = MakeRect(blit.src_rect, blit.src_layer);
    const DkImageRect dst_rect = MakeRect(blit.dst_rect, blit.dst_layer);
    const u32 flags = IsColorType(type) ? DkBlitFlag_FilterLinear : DkBlitFlag_FilterNearest;

    DkCmdBuf cmdbuf = runtime->TransferBegin();
    dkCmdBufBlitImage(cmdbuf, &src_view, &src_rect, &dst_view, &dst_rect, flags, 0);
    runtime->TransferSubmit();
}

Framebuffer::Framebuffer(TextureRuntime&, const VideoCore::FramebufferParams& params,
                         const Surface* color, const Surface* depth)
    : VideoCore::FramebufferParams{params},
      res_scale{color ? color->res_scale : (depth ? depth->res_scale : 1u)} {
    if (color) {
        images[0] = color->Image(true);
        formats[0] = color->pixel_format;
    }
    if (depth) {
        images[1] = depth->Image(true);
        formats[1] = depth->pixel_format;
    }
}

Framebuffer::~Framebuffer() = default;

Sampler::Sampler(TextureRuntime&, const VideoCore::SamplerParams& params) {
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = ToDkFilter(params.min_filter);
    sampler.magFilter = ToDkFilter(params.mag_filter);
    sampler.mipFilter = ToDkMipFilter(params.mip_filter);
    sampler.wrapMode[0] = ToDkWrapMode(params.wrap_s);
    sampler.wrapMode[1] = ToDkWrapMode(params.wrap_t);
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    sampler.lodClampMin = static_cast<float>(params.lod_min);
    sampler.lodClampMax = static_cast<float>(params.lod_max);
    sampler.lodBias = static_cast<float>(params.lod_bias) / 256.0f;

    for (u32 i = 0; i < 4; ++i) {
        sampler.borderColor[i].value_f =
            static_cast<float>((params.border_color >> (i * 8)) & 0xFF) / 255.0f;
    }

    dkSamplerDescriptorInitialize(&descriptor, &sampler);
}

Sampler::~Sampler() = default;

} // namespace Deko3D
