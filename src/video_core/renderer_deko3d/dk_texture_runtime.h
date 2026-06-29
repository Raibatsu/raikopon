// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <fmt/format.h>
#include "common/math_util.h"
#include "common/vector_math.h"
#include "video_core/rasterizer_cache/framebuffer_base.h"
#include "video_core/rasterizer_cache/rasterizer_cache_base.h"
#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/renderer_deko3d/dk_common.h"
#include "video_core/renderer_deko3d/dk_memory.h"

namespace VideoCore {
struct Material;
}

namespace Deko3D {

/// Maps a PICA pixel format onto a deko3d image format plus the usage flags it needs.
struct FormatTuple {
    DkImageFormat format = DkImageFormat_None;
    u32 usage_flags = 0;

    bool operator==(const FormatTuple& other) const noexcept {
        return format == other.format && usage_flags == other.usage_flags;
    }
};

class Surface;

/**
 * Provides texture manipulation functions to the rasterizer cache. Owns the device-side memory
 * pools, the staging streams and a transfer command buffer used to drive the copy engine. This is
 * the deko3d analogue of OpenGL::TextureRuntime / Vulkan::TextureRuntime.
 */
class TextureRuntime {
    friend class Surface;

public:
    explicit TextureRuntime(DkDevice device, DkQueue queue);
    ~TextureRuntime();

    TextureRuntime(const TextureRuntime&) = delete;
    TextureRuntime& operator=(const TextureRuntime&) = delete;

    /// Returns the removal threshold ticks for the garbage collector.
    u32 RemoveThreshold();

    /// Submits and waits for any pending GPU work.
    void Finish();

    /// Returns true if the provided pixel format cannot be uploaded byte-for-byte.
    bool NeedsConversion(const Surface& surface) const;

    /// Maps a region of the upload/download staging stream of the requested size.
    VideoCore::StagingData FindStaging(u32 size, bool upload);

    /// Returns the deko3d format tuple associated with the provided pixel format.
    const FormatTuple& GetFormatTuple(VideoCore::PixelFormat pixel_format) const;
    const FormatTuple& GetFormatTuple(VideoCore::CustomPixelFormat pixel_format) const;

    /// Attempts to reinterpret a rectangle of source to another rectangle of dest.
    bool Reinterpret(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    /// Fills the rectangle of the texture with the clear value provided.
    void ClearTexture(Surface& surface, const VideoCore::TextureClear& clear);

    /// Copies a rectangle of source to another rectangle of dest.
    bool CopyTextures(Surface& source, Surface& dest,
                      std::span<const VideoCore::TextureCopy> copies);

    bool CopyTextures(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy) {
        return CopyTextures(source, dest, std::array{copy});
    }

    /// Blits a rectangle of source to another rectangle of dest.
    bool BlitTextures(Surface& source, Surface& dest, const VideoCore::TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture.
    void GenerateMipmaps(Surface& surface);

    [[nodiscard]] DkDevice GetDevice() const noexcept {
        return device;
    }

    [[nodiscard]] MemoryPool& ImagePool() noexcept {
        return *image_pool;
    }

private:
    /// A linear, ring-allocated CPU-visible buffer used for copy-engine uploads/downloads.
    struct StagingBuffer {
        DkMemBlock memblock = nullptr;
        u8* cpu_addr = nullptr;
        DkGpuAddr gpu_addr = 0;
        u32 size = 0;
        u32 offset = 0;
    };

    /// Resets the transfer command buffer and returns it ready for recording.
    DkCmdBuf TransferBegin();
    /// Finishes the recorded transfer list, submits it and waits for completion.
    void TransferSubmit();

    void CreateStaging(StagingBuffer& buffer, u32 size);
    void EnsureStaging(StagingBuffer& buffer, u32 size);

    DkDevice device;
    DkQueue queue;

    std::unique_ptr<MemoryPool> image_pool;
    std::unique_ptr<MemoryPool> data_pool;

    DkMemBlock cmdbuf_memblock = nullptr;
    DkCmdBuf transfer_cmdbuf = nullptr;

    StagingBuffer upload_buffer;
    StagingBuffer download_buffer;
};

class Surface : public VideoCore::SurfaceBase {
    friend class TextureRuntime;

public:
    explicit Surface(TextureRuntime& runtime, const VideoCore::SurfaceParams& params,
                     const VideoCore::SurfaceFlagBits& initial_flag_bits = {});
    explicit Surface(TextureRuntime& runtime, const VideoCore::SurfaceBase& surface,
                     const VideoCore::Material* material);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    Surface(Surface&& o) noexcept = default;
    Surface& operator=(Surface&& o) noexcept = default;

    [[nodiscard]] const FormatTuple& Tuple() const noexcept {
        return tuple;
    }

    /// Returns the Deko3d image.
    [[nodiscard]] DkImage* Image(bool scaled = true) const noexcept {
        return (scaled && scaled_image) ? scaled_image.get() : image.get();
    }

    /// Builds a default image view over the surface image.
    [[nodiscard]] DkImageView View(bool scaled = true, u32 level = 0) const noexcept;

    /// Uploads pixel data in staging to a rectangle region of the surface texture.
    void Upload(const VideoCore::BufferTextureCopy& upload, const VideoCore::StagingData& staging);

    /// Uploads the custom material to the surface allocation.
    void UploadCustom(const VideoCore::Material* material, u32 level);

    /// Downloads pixel data to staging from a rectangle region of the surface texture.
    void Download(const VideoCore::BufferTextureCopy& download,
                  const VideoCore::StagingData& staging);

    /// Scales up the surface to match the new resolution scale.
    void ScaleUp(u32 new_scale);

    /// Returns the bpp of the internal surface format.
    u32 GetInternalBytesPerPixel() const;

private:
    /// Allocates an image of the given dimensions into out_image/out_alloc.
    void Alloc(std::unique_ptr<DkImage>& out_image, MemoryAllocation& out_alloc, u32 width,
               u32 height, u32 levels);

    /// Blits between the scaled and unscaled images.
    void BlitScale(const VideoCore::TextureBlit& blit, bool up_scale);

private:
    TextureRuntime* runtime;
    FormatTuple tuple;
    std::unique_ptr<DkImage> image;
    std::unique_ptr<DkImage> scaled_image;
    MemoryAllocation allocation;
    MemoryAllocation scaled_allocation;
};

class Framebuffer : public VideoCore::FramebufferParams {
public:
    explicit Framebuffer(TextureRuntime& runtime, const VideoCore::FramebufferParams& params,
                         const Surface* color, const Surface* depth_stencil);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& o) noexcept = default;
    Framebuffer& operator=(Framebuffer&& o) noexcept = default;

    [[nodiscard]] u32 Scale() const noexcept {
        return res_scale;
    }

    [[nodiscard]] DkImage* Image(VideoCore::SurfaceType type) const noexcept {
        return images[Index(type)];
    }

    [[nodiscard]] bool HasAttachment(VideoCore::SurfaceType type) const noexcept {
        return images[Index(type)] != nullptr;
    }

    [[nodiscard]] VideoCore::PixelFormat Format(VideoCore::SurfaceType type) const noexcept {
        return formats[Index(type)];
    }

private:
    u32 res_scale{1};
    std::array<DkImage*, 2> images{};
    std::array<VideoCore::PixelFormat, 2> formats{VideoCore::PixelFormat::Invalid,
                                                  VideoCore::PixelFormat::Invalid};
};

class Sampler {
public:
    explicit Sampler(TextureRuntime& runtime, const VideoCore::SamplerParams& params);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&&) noexcept = default;
    Sampler& operator=(Sampler&&) noexcept = default;

    [[nodiscard]] const DkSamplerDescriptor& Descriptor() const noexcept {
        return descriptor;
    }

private:
    DkSamplerDescriptor descriptor{};
};

class DebugScope {
public:
    template <typename... T>
    explicit DebugScope(TextureRuntime& runtime, Common::Vec4f color,
                        fmt::format_string<T...> format, T... args)
        : DebugScope{runtime, color, fmt::format(format, std::forward<T>(args)...)} {}
    explicit DebugScope(TextureRuntime&, Common::Vec4f, std::string_view) {}
    ~DebugScope() = default;
};

struct Traits {
    using Runtime = Deko3D::TextureRuntime;
    using Sampler = Deko3D::Sampler;
    using Surface = Deko3D::Surface;
    using Framebuffer = Deko3D::Framebuffer;
    using DebugScope = Deko3D::DebugScope;
};

using RasterizerCache = VideoCore::RasterizerCache<Traits>;

} // namespace Deko3D
