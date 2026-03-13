/// @file ExposureStage.cpp
/// @brief Implementation of ExposureStage — linear exposure compensation
///        with black-point, shadow lift, and soft-knee highlight recovery.

#include "ExposureStage.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHER_HAS_NEON 1
#else
#define AETHER_HAS_NEON 0
#endif

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// Introspection
// ─────────────────────────────────────────────────────────────────────

ExposureStage::ExposureStage() {
    buildLUT();
}

StageId ExposureStage::id() const noexcept {
    return StageId::Exposure;
}

bool ExposureStage::supportsGPU() const noexcept {
    return false;
}

std::string_view ExposureStage::description() const noexcept {
    return "Linear exposure compensation with soft-knee highlight recovery";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void ExposureStage::setParams(const StageParams& p) {
    if (const auto* ep = std::get_if<ExposureParams>(&p)) {
        m_params   = *ep;
        m_lutDirty = true;
    }
}

// ─────────────────────────────────────────────────────────────────────
// buildLUT — tanh LUT for highlight recovery
// ─────────────────────────────────────────────────────────────────────

void ExposureStage::buildLUT() {
    // LUT maps normalised input [0, 3.0] → tanh(x) in 256 bins.
    constexpr float kMaxInput = 3.0F;
    constexpr float kStep     = kMaxInput / 255.0F;
    for (size_t i = 0; i < 256; ++i) {
        m_tanhLUT[i] = std::tanh(static_cast<float>(i) * kStep);
    }
    m_lutDirty = false;
}

/// @brief Look up tanh via linear interpolation in the LUT.
static float tanhLUT(float x, const std::array<float, 256>& lut) noexcept {
    constexpr float kMaxInput = 3.0F;
    constexpr float kScale    = 255.0F / kMaxInput;

    if (x <= 0.0F) return 0.0F;
    if (x >= kMaxInput) return 1.0F;  // tanh(3) ≈ 0.9951 ≈ 1

    const float pos   = x * kScale;
    const auto  idx   = static_cast<size_t>(pos);
    const float frac  = pos - static_cast<float>(idx);
    const size_t next = std::min(idx + 1, size_t{255});

    return lut[idx] + (lut[next] - lut[idx]) * frac;
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer ExposureStage::process(const ImageBuffer& in) {
    // ── Input validation ─────────────────────────────────────────────
    if (in.width == 0 || in.height == 0) {
        std::cerr << "[ExposureStage] Warning: empty input buffer. Returning empty.\n";
        return {};
    }

    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[ExposureStage] Warning: input is not PROPHOTO_LINEAR "
                     "(got " << static_cast<int>(in.colorSpace)
                  << "). Returning input unchanged.\n";
        return in;
    }

    if (m_lutDirty) {
        buildLUT();
    }

    // ── Allocate output ──────────────────────────────────────────────
    auto result = ImageBuffer::create(in.width, in.height,
                                      ColorSpace::PROPHOTO_LINEAR);
    if (!result) {
        std::cerr << "[ExposureStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        return in;
    }
    ImageBuffer& out = *result;
    out.whitePoint = in.whitePoint;
    out.isLinear   = true;

    const size_t total = in.pixelCount() * ImageBuffer::channelCount();
    const float* src   = in.ptr();
    float*       dst   = out.ptr();

    const float bp   = m_params.blackPoint;
    const float gain = std::pow(2.0F, m_params.exposureEV);
    const float sl   = m_params.shadowLift;
    const float hc   = m_params.highlightComp;

    // Precompute black-point normalisation factor.
    const float bpNorm = (bp < 1.0F) ? 1.0F / (1.0F - bp) : 1.0F;

    // ── Step 1 : Black point subtraction ─────────────────────────────
    // Copy & apply black point (or just copy if bp == 0).
    if (bp > 0.0F) {
        for (size_t i = 0; i < total; ++i) {
            dst[i] = std::max(0.0F, src[i] - bp) * bpNorm;
        }
    } else {
        std::copy(src, src + total, dst);
    }

    // ── Step 2 : Exposure gain ───────────────────────────────────────
    if (gain != 1.0F) {
#if AETHER_HAS_NEON
        const float32x4_t vGain = vdupq_n_f32(gain);
        const size_t simdEnd = (total / 4) * 4;
        for (size_t i = 0; i < simdEnd; i += 4) {
            float32x4_t v = vld1q_f32(dst + i);
            v = vmulq_f32(v, vGain);
            vst1q_f32(dst + i, v);
        }
        for (size_t i = simdEnd; i < total; ++i) {
            dst[i] *= gain;
        }
#else
        for (size_t i = 0; i < total; ++i) {
            dst[i] *= gain;
        }
#endif
    }

    // ── Step 3 : Highlight soft-knee recovery ────────────────────────
    if (hc > 0.0F) {
        constexpr float kKneeStart = 0.8F;
        const float kRange = 1.0F - kKneeStart;

        for (size_t i = 0; i < total; ++i) {
            const float v = dst[i];
            if (v > kKneeStart) {
                const float arg = (v - kKneeStart) / kRange * hc;
                dst[i] = kKneeStart + kRange * tanhLUT(arg, m_tanhLUT);
            }
        }
    }

    // ── Step 4 : Shadow lift ─────────────────────────────────────────
    if (sl > 0.0F) {
        const float scale = 1.0F - sl;
#if AETHER_HAS_NEON
        const float32x4_t vScale = vdupq_n_f32(scale);
        const float32x4_t vLift  = vdupq_n_f32(sl);
        const size_t simdEnd = (total / 4) * 4;
        for (size_t i = 0; i < simdEnd; i += 4) {
            float32x4_t v = vld1q_f32(dst + i);
            v = vfmaq_f32(vLift, v, vScale);  // v*scale + lift
            vst1q_f32(dst + i, v);
        }
        for (size_t i = simdEnd; i < total; ++i) {
            dst[i] = dst[i] * scale + sl;
        }
#else
        for (size_t i = 0; i < total; ++i) {
            dst[i] = dst[i] * scale + sl;
        }
#endif
    }

    return out;
}

} // namespace aether
