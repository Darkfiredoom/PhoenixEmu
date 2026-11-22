// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Vulkan {

constexpr u64 DefaultSeparatePoolBlockSize = u64{16} << 20;
constexpr u64 SmallDeviceBufferPoolBlockSize = u64{4} << 20;

/// The buffer cache's device-local pool block size. On a small discrete card (under 9 GiB, or a
/// budget under 7.5 GiB) 4 MiB: at Castle Town ~140 MiB of small buffers held about 50 blocks of
/// 16 MiB, 800 MiB, since a block stays while any buffer in it lives (Tasarren's pool size,
/// Frizti's small-card gate; ported from Lichii #2316).
[[nodiscard]] constexpr u64 SelectDeviceBufferPoolBlockSize(bool discrete, u64 heap_size,
                                                           u64 heap_budget) {
    constexpr u64 small_heap = u64{9} << 30;
    constexpr u64 small_budget = u64{7680} << 20;
    const bool use_small_blocks =
        discrete && heap_size != 0 &&
        (heap_size < small_heap || (heap_budget != 0 && heap_budget < small_budget));
    return use_small_blocks ? SmallDeviceBufferPoolBlockSize : DefaultSeparatePoolBlockSize;
}

/// Whether a buffer goes in the pool: up to half a block, and no alignment of its own.
[[nodiscard]] constexpr bool FitsDeviceBufferPool(u64 size, u64 min_alignment, u64 block_size) {
    return min_alignment <= 1 && size <= block_size / 2;
}

} // namespace Vulkan
