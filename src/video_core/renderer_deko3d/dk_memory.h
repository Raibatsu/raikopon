// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <deko3d.h>
#include "common/common_types.h"

namespace Deko3D {

class MemoryPool;
struct PoolBlock;

/**
 * RAII handle to a sub-allocation within a MemoryPool.
 */
class MemoryAllocation {
public:
    MemoryAllocation() = default;
    MemoryAllocation(MemoryPool* pool, PoolBlock* block, u32 offset, u32 size)
        : pool{pool}, block{block}, offset{offset}, size{size} {}
    ~MemoryAllocation() {
        Release();
    }

    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;

    MemoryAllocation(MemoryAllocation&& o) noexcept
        : pool{std::exchange(o.pool, nullptr)}, block{std::exchange(o.block, nullptr)},
          offset{std::exchange(o.offset, 0)}, size{std::exchange(o.size, 0)} {}

    MemoryAllocation& operator=(MemoryAllocation&& o) noexcept {
        if (this != &o) {
            Release();
            pool = std::exchange(o.pool, nullptr);
            block = std::exchange(o.block, nullptr);
            offset = std::exchange(o.offset, 0);
            size = std::exchange(o.size, 0);
        }
        return *this;
    }

    explicit operator bool() const noexcept {
        return pool != nullptr;
    }

    /// The Deko3d memory block backing this allocation.
    [[nodiscard]] DkMemBlock MemBlock() const noexcept;

    /// Byte offset of this allocation.
    [[nodiscard]] u32 Offset() const noexcept {
        return offset;
    }

    [[nodiscard]] u32 Size() const noexcept {
        return size;
    }

    /// CPU pointer to the allocation.
    [[nodiscard]] void* CpuAddr() const noexcept;

    /// GPU address of the allocation.
    [[nodiscard]] DkGpuAddr GpuAddr() const noexcept;

private:
    void Release() noexcept;

    MemoryPool* pool = nullptr;
    PoolBlock* block = nullptr;
    u32 offset = 0;
    u32 size = 0;
};

/**
 * A growable pool that sub-allocates from a list of DkMemBlocks with a first-fit free list. One pool is created per
 * memory class (GPU images, GPU data, CPU-visible).
 */
class MemoryPool {
public:
    static constexpr u32 DefaultBlockSize = 8 * 1024 * 1024;

    explicit MemoryPool(DkDevice device, u32 flags, u32 block_size = DefaultBlockSize);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// Allocates size bytes aligned to alignment.
    [[nodiscard]] MemoryAllocation Allocate(u32 size, u32 alignment);

private:
    friend class MemoryAllocation;

    /// Returns a slice [offset, offset+size) of block to the free list.
    void Free(PoolBlock* block, u32 offset, u32 size);

    /// Creates a new block large enough to hold at least min_size bytes.
    PoolBlock* CreateBlock(u32 min_size);

    DkDevice device;
    u32 flags;
    u32 block_size;
    std::vector<std::unique_ptr<PoolBlock>> blocks;
};

} // namespace Deko3D
