// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>

#include "common/common_types.h"
#include "video_core/android_performance.h"
#include "video_core/surface.h"
#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
#include <arm_neon.h>
#endif

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
inline bool CanSanitize(VideoCore::Surface::PixelFormat format) {
    using VideoCore::Surface::PixelFormat;
    switch (format) {
    case PixelFormat::R16G16B16A16_FLOAT:
    case PixelFormat::R16G16B16X16_FLOAT:
    case PixelFormat::R16G16_FLOAT:
    case PixelFormat::R16_FLOAT:
    case PixelFormat::R32G32B32A32_FLOAT:
    case PixelFormat::R32G32B32_FLOAT:
    case PixelFormat::R32G32_FLOAT:
    case PixelFormat::R32_FLOAT:
    case PixelFormat::B10G11R11_FLOAT:
        return true;
    default:
        return false;
    }
}

inline size_t ScrubNonFinite(VideoCore::Surface::PixelFormat format, std::span<u8> data) {
    using VideoCore::Surface::PixelFormat;
    size_t fixed = 0;
    switch (format) {
    case PixelFormat::R16G16B16A16_FLOAT:
    case PixelFormat::R16G16B16X16_FLOAT:
    case PixelFormat::R16G16_FLOAT:
    case PixelFormat::R16_FLOAT: {
        const size_t count = data.size() / sizeof(u16);
        size_t i = 0;
#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
        if (AndroidPerformance::Enabled()) {
            const auto exponent = vdupq_n_u16(0x7C00);
            const auto one = vdupq_n_u16(1);
            for (; i + 8 <= count; i += 8) {
                auto* ptr = reinterpret_cast<u16*>(data.data() + i * sizeof(u16));
                const auto values = vld1q_u16(ptr);
                const auto bad = vceqq_u16(vandq_u16(values, exponent), exponent);
                vst1q_u16(ptr, vbicq_u16(values, bad));
                fixed += vaddvq_u16(vandq_u16(bad, one));
            }
        }
#endif
        for (; i < count; ++i) {
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
        size_t i = 0;
#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
        if (AndroidPerformance::Enabled()) {
            const auto exponent = vdupq_n_u32(0x7F800000u);
            const auto one = vdupq_n_u32(1);
            for (; i + 4 <= count; i += 4) {
                auto* ptr = reinterpret_cast<u32*>(data.data() + i * sizeof(u32));
                const auto values = vld1q_u32(ptr);
                const auto bad = vceqq_u32(vandq_u32(values, exponent), exponent);
                vst1q_u32(ptr, vbicq_u32(values, bad));
                fixed += vaddvq_u32(vandq_u32(bad, one));
            }
        }
#endif
        for (; i < count; ++i) {
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
        size_t i = 0;
#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
        if (AndroidPerformance::Enabled()) {
            const auto r_exp = vdupq_n_u32(0x000007C0u);
            const auto g_exp = vdupq_n_u32(0x003E0000u);
            const auto b_exp = vdupq_n_u32(0xF8000000u);
            const auto one = vdupq_n_u32(1);
            for (; i + 4 <= count; i += 4) {
                auto* ptr = reinterpret_cast<u32*>(data.data() + i * sizeof(u32));
                const auto values = vld1q_u32(ptr);
                const auto r_bad = vceqq_u32(vandq_u32(values, r_exp), r_exp);
                const auto g_bad = vceqq_u32(vandq_u32(values, g_exp), g_exp);
                const auto b_bad = vceqq_u32(vandq_u32(values, b_exp), b_exp);
                const auto clear = vorrq_u32(vandq_u32(r_bad, vdupq_n_u32(0x000007FFu)),
                                             vorrq_u32(vandq_u32(g_bad, vdupq_n_u32(0x003FF800u)),
                                                       vandq_u32(b_bad, vdupq_n_u32(0xFFC00000u))));
                vst1q_u32(ptr, vbicq_u32(values, clear));
                const auto counts = vaddq_u32(
                    vandq_u32(r_bad, one), vaddq_u32(vandq_u32(g_bad, one), vandq_u32(b_bad, one)));
                fixed += vaddvq_u32(counts);
            }
        }
#endif
        for (; i < count; ++i) {
            u32 v;
            std::memcpy(&v, data.data() + i * sizeof(u32), sizeof(v));
            u32 out = v;
            if ((v & 0x000007C0u) == 0x000007C0u) {
                out &= ~0x000007FFu;
                ++fixed;
            }
            if ((v & 0x003E0000u) == 0x003E0000u) {
                out &= ~0x003FF800u;
                ++fixed;
            }
            if ((v & 0xF8000000u) == 0xF8000000u) {
                out &= ~0xFFC00000u;
                ++fixed;
            }
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
