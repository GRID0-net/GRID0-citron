// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>

#include "common/common_types.h"
#include "video_core/surface.h"

namespace VideoCommon {

namespace CitrosisDiag {
// Scene-switch corruption diagnostics (Citrosis). Read once from the environment.
//   CITROSIS_TC_SANITIZE=0  disable scrubbing NaN/Inf from float image uploads (default on)
//   CITROSIS_TC_NO_GC=1     never evict texture-cache images
//   CITROSIS_BC_NO_GC=1     never evict buffer-cache buffers
inline bool EnvFlag(const char* name, bool fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') {
        return fallback;
    }
    return !(v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}
inline bool SanitizeUploads() {
    static const bool value = EnvFlag("CITROSIS_TC_SANITIZE", true);
    return value;
}
inline bool TextureGcDisabled() {
    static const bool value = EnvFlag("CITROSIS_TC_NO_GC", false);
    return value;
}
inline bool BufferGcDisabled() {
    static const bool value = EnvFlag("CITROSIS_BC_NO_GC", false);
    return value;
}

// Replaces non-finite values in host-format float texel data with zero. Stale guest memory
// (bytes left by a differently formatted, GPU-only surface that was never written back)
// decodes as Inf/NaN in HDR targets; a single NaN poisons temporal/exposure buffers and the
// whole frame goes white/posterized. Returns the number of scrubbed components.
inline size_t ScrubNonFinite(VideoCore::Surface::PixelFormat format, std::span<u8> data) {
    using VideoCore::Surface::PixelFormat;
    size_t fixed = 0;
    switch (format) {
    case PixelFormat::R16G16B16A16_FLOAT:
    case PixelFormat::R16G16B16X16_FLOAT:
    case PixelFormat::R16G16_FLOAT:
    case PixelFormat::R16_FLOAT: {
        const size_t count = data.size() / sizeof(u16);
        for (size_t i = 0; i < count; ++i) {
            u16 h;
            std::memcpy(&h, data.data() + i * sizeof(u16), sizeof(h));
            if ((h & 0x7C00) == 0x7C00) {
                h = 0;
                std::memcpy(data.data() + i * sizeof(u16), &h, sizeof(h));
                ++fixed;
            }
        }
        break;
    }
    case PixelFormat::R32G32B32A32_FLOAT:
    case PixelFormat::R32G32B32_FLOAT:
    case PixelFormat::R32G32_FLOAT:
    case PixelFormat::R32_FLOAT: {
        const size_t count = data.size() / sizeof(u32);
        for (size_t i = 0; i < count; ++i) {
            u32 f;
            std::memcpy(&f, data.data() + i * sizeof(u32), sizeof(f));
            if ((f & 0x7F800000u) == 0x7F800000u) {
                f = 0;
                std::memcpy(data.data() + i * sizeof(u32), &f, sizeof(f));
                ++fixed;
            }
        }
        break;
    }
    case PixelFormat::B10G11R11_FLOAT: {
        // R: bits 0-10 (exp 6-10), G: bits 11-21 (exp 17-21), B: bits 22-31 (exp 27-31).
        const size_t count = data.size() / sizeof(u32);
        for (size_t i = 0; i < count; ++i) {
            u32 v;
            std::memcpy(&v, data.data() + i * sizeof(u32), sizeof(v));
            u32 out = v;
            if ((v & 0x000007C0u) == 0x000007C0u) { out &= ~0x000007FFu; ++fixed; }
            if ((v & 0x003E0000u) == 0x003E0000u) { out &= ~0x003FF800u; ++fixed; }
            if ((v & 0xF8000000u) == 0xF8000000u) { out &= ~0xFFC00000u; ++fixed; }
            if (out != v) {
                std::memcpy(data.data() + i * sizeof(u32), &out, sizeof(out));
            }
        }
        break;
    }
    default:
        break;
    }
    return fixed;
}
} // namespace CitrosisDiag

} // namespace VideoCommon
