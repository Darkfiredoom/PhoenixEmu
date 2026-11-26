// SPDX-FileCopyrightText: Copyright 2026 Phoenix Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Vulkan {

/// Stub BufferArena – the full implementation lives in build-410's buffer
/// re-recording subsystem.  Only SideScope is referenced by the graphics
/// pipeline today; it is kept as a no-op until the rest of the arena is ported.
class BufferArena {
public:
    /// Marks code whose scheduler use belongs to the buffer side (no-op stub).
    struct SideScope {
        SideScope() noexcept = default;
        ~SideScope() = default;
    };
};

} // namespace Vulkan
