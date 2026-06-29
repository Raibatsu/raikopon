// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/assert.h"
#include "video_core/renderer_deko3d/dk_common.h"
#include "video_core/renderer_deko3d/dk_memory.h"

namespace Deko3D {

/// A single deko3d memory block with its free ranges, sorted by start offset.
struct PoolBlock {
    struct Range {
        u32 start;
        u32 end;
    };

    DkMemBlock memblock = nullptr;
    u8* cpu_addr = nullptr;
    DkGpuAddr gpu_addr = DK_GPU_ADDR_INVALID;
    u32 size = 0;
    std::vector<Range> free_ranges;
};

DkMemBlock MemoryAllocation::MemBlock() const noexcept {
    return block ? block->memblock : nullptr;
}

void* MemoryAllocation::CpuAddr() const noexcept {
    return block && block->cpu_addr ? block->cpu_addr + offset : nullptr;
}

DkGpuAddr MemoryAllocation::GpuAddr() const noexcept {
    return block ? block->gpu_addr + offset : DK_GPU_ADDR_INVALID;
}

void MemoryAllocation::Release() noexcept {
    if (pool) {
        pool->Free(block, offset, size);
        pool = nullptr;
        block = nullptr;
    }
}

MemoryPool::MemoryPool(DkDevice device, u32 flags, u32 block_size)
    : device{device}, flags{flags}, block_size{block_size} {}

MemoryPool::~MemoryPool() {
    for (auto& block : blocks) {
        if (block->memblock) {
            dkMemBlockDestroy(block->memblock);
        }
    }
}

PoolBlock* MemoryPool::CreateBlock(u32 min_size) {
    const u32 usable = AlignUp(std::max(block_size, min_size), DK_MEMBLOCK_ALIGNMENT);

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, usable);
    maker.flags = flags;
    DkMemBlock memblock = dkMemBlockCreate(&maker);
    if (memblock == nullptr) {
        return nullptr;
    }

    auto block = std::make_unique<PoolBlock>();
    block->memblock = memblock;
    block->cpu_addr = static_cast<u8*>(dkMemBlockGetCpuAddr(memblock));
    block->gpu_addr = dkMemBlockGetGpuAddr(memblock);
    block->size = usable;
    block->free_ranges.push_back({0, usable});

    blocks.push_back(std::move(block));
    return blocks.back().get();
}

MemoryAllocation MemoryPool::Allocate(u32 size, u32 alignment) {
    if (size == 0) {
        return {};
    }
    ASSERT_MSG((alignment & (alignment - 1)) == 0, "Alignment must be a power of two");

    const auto try_block = [&](PoolBlock* block) -> MemoryAllocation {
        for (auto it = block->free_ranges.begin(); it != block->free_ranges.end(); ++it) {
            const u32 aligned_start = AlignUp(it->start, alignment);
            const u32 aligned_end = aligned_start + size;
            if (aligned_end > it->end) {
                continue;
            }
            const u32 range_start = it->start;
            const u32 range_end = it->end;
            it = block->free_ranges.erase(it);
            // Re-insert the leftover head/tail gaps, keeping the list sorted.
            if (aligned_end != range_end) {
                it = block->free_ranges.insert(it, {aligned_end, range_end});
            }
            if (aligned_start != range_start) {
                block->free_ranges.insert(it, {range_start, aligned_start});
            }
            return MemoryAllocation{this, block, aligned_start, size};
        }
        return {};
    };

    for (auto& block : blocks) {
        if (auto alloc = try_block(block.get())) {
            return alloc;
        }
    }

    PoolBlock* block = CreateBlock(AlignUp(size, alignment) + alignment);
    if (block == nullptr) {
        return {};
    }
    return try_block(block);
}

void MemoryPool::Free(PoolBlock* block, u32 offset, u32 size) {
    if (block == nullptr || size == 0) {
        return;
    }
    auto& ranges = block->free_ranges;
    const PoolBlock::Range hole{offset, offset + size};

    // Insert keeping the list sorted by start, then coalesce with neighbours.
    auto it = std::lower_bound(ranges.begin(), ranges.end(), hole,
                               [](const auto& a, const auto& b) { return a.start < b.start; });
    it = ranges.insert(it, hole);

    if (it + 1 != ranges.end() && it->end == (it + 1)->start) {
        it->end = (it + 1)->end;
        ranges.erase(it + 1);
    }
    if (it != ranges.begin() && (it - 1)->end == it->start) {
        (it - 1)->end = it->end;
        ranges.erase(it);
    }
}

} // namespace Deko3D
