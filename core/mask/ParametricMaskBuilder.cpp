/// @file ParametricMaskBuilder.cpp
/// @brief Implementation of ParametricMaskBuilder — luminance + HSL
///        parametric mask generation with NEON-accelerated luminance pass.

#include "ParametricMaskBuilder.hpp"

#include <algorithm>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHER_HAS_NEON 1
#else
#define AETHER_HAS_NEON 0
#endif

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// rgbToHSL
// ─────────────────────────────────────────────────────────────────────

void ParametricMaskBuilder::rgbToHSL(float r, float g, float b,
                                      float& h, float& s, float& l) noexcept {
    const float cMax = std::max({r, g, b});
    const float cMin = std::min({r, g, b});
    const float delta = cMax - cMin;

    l = (cMax + cMin) * 0.5F;

    if (delta < 1e-6F) {
        h = 0.0F;
        s = 0.0F;
        return;
    }

    s = (l > 0.5F) ? delta / (2.0F - cMax - cMin)
                    : delta / (cMax + cMin);

    if (cMax == r) {
        h = 60.0F * std::fmod((g - b) / delta, 6.0F);
    } else if (cMax == g) {
        h = 60.0F * ((b - r) / delta + 2.0F);
    } else {
        h = 60.0F * ((r - g) / delta + 4.0F);
    }

    if (h < 0.0F) h += 360.0F;
}

// ─────────────────────────────────────────────────────────────────────
// smoothStep — trapezoidal ramp with feather
// ─────────────────────────────────────────────────────────────────────

float ParametricMaskBuilder::smoothStep(float v, float low, float high,
                                         float feather) noexcept {
    if (feather < 1e-6F) {
        // Hard cutoff.
        return (v >= low && v <= high) ? 1.0F : 0.0F;
    }

    // Ramp up: [low - feather, low] → [0, 1]
    if (v < low) {
        const float edge = low - feather;
        if (v <= edge) return 0.0F;
        return (v - edge) / feather;
    }

    // Ramp down: [high, high + feather] → [1, 0]
    if (v > high) {
        const float edge = high + feather;
        if (v >= edge) return 0.0F;
        return (edge - v) / feather;
    }

    return 1.0F;
}

// ─────────────────────────────────────────────────────────────────────
// hueDistance — circular distance in [0, 180]
// ─────────────────────────────────────────────────────────────────────

float ParametricMaskBuilder::hueDistance(float h, float center) noexcept {
    float d = std::fabs(h - center);
    if (d > 180.0F) d = 360.0F - d;
    return d;
}

// ─────────────────────────────────────────────────────────────────────
// build
// ─────────────────────────────────────────────────────────────────────

MaskLayer ParametricMaskBuilder::build(const ImageBuffer& image,
                                        const ParametricMaskParams& p) {
    MaskLayer mask;
    mask.width  = image.width;
    mask.height = image.height;
    const size_t px = image.pixelCount();
    mask.alpha.resize(px);

    const float* src = image.ptr();

    for (size_t i = 0; i < px; ++i) {
        const size_t off = i * 3;
        const float r = src[off + 0];
        const float g = src[off + 1];
        const float b = src[off + 2];

        // 1. Rec.709 luminance.
        const float L = 0.2126F * r + 0.7152F * g + 0.0722F * b;

        // 2. HSL conversion.
        float H, S, Lhsl;
        rgbToHSL(r, g, b, H, S, Lhsl);

        // 3. Component masks.
        const float lumaMask = smoothStep(L, p.lumaMin, p.lumaMax, p.lumaFeather);
        const float hueDist  = hueDistance(H, p.hueCenter);
        const float hueMask  = smoothStep(hueDist, 0.0F, p.hueWidth * 0.5F, p.hueFeather);
        const float satMask  = smoothStep(S, p.satMin, p.satMax, p.satFeather);

        // 4. Combine.
        float alpha;
        if (p.intersect) {
            alpha = lumaMask * hueMask * satMask;
        } else {
            alpha = std::max(lumaMask, hueMask * satMask);
        }

        mask.alpha[i] = std::clamp(alpha, 0.0F, 1.0F);
    }

    return mask;
}

} // namespace aether
