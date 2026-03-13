/// @file ToneMappingStage.cpp
/// @brief Implementation of ToneMappingStage — LUT-accelerated scene-to-display
///        tone mapping with Filmic S-curve, ACES, and Linear operators.

#include "ToneMappingStage.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../gpu/MetalContext.h"

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// Tone-mapping operator functions
// ─────────────────────────────────────────────────────────────────────

/// @brief Hable / Uncharted 2 filmic curve helper.
///
/// f(x) = ((x*(A*x+C*B)+D*E) / (x*(A*x+B)+D*F)) - E/F
static float hableF(float x) noexcept {
    constexpr float A = 0.15F;
    constexpr float B = 0.50F;
    constexpr float C = 0.10F;
    constexpr float D = 0.20F;
    constexpr float E = 0.02F;
    constexpr float F = 0.30F;

    return ((x * (A * x + C * B) + D * E) /
            (x * (A * x + B) + D * F)) - E / F;
}

/// @brief Filmic S-curve (Hable) with contrast modulation.
static float filmicS(float x, float contrast) noexcept {
    constexpr float W = 11.2F;  // Scene white point.
    const float cx = std::pow(x * 2.0F, contrast);
    return hableF(cx) / hableF(W);
}

/// @brief ACES RRT approximation (Stephen Hill).
static float acesHill(float x, float contrast) noexcept {
    const float cx = std::pow(x, contrast);
    return std::clamp(
        (cx * (2.51F * cx + 0.03F)) /
        (cx * (2.43F * cx + 0.59F) + 0.14F),
        0.0F, 1.0F);
}

// ─────────────────────────────────────────────────────────────────────
// Introspection
// ─────────────────────────────────────────────────────────────────────

ToneMappingStage::ToneMappingStage() {
    buildLUT1D();
}

StageId ToneMappingStage::id() const noexcept {
    return StageId::ToneMapping;
}

bool ToneMappingStage::supportsGPU() const noexcept {
    return true;
}

std::string_view ToneMappingStage::description() const noexcept {
    return "Scene-to-display tone mapping (Filmic/ACES/Linear)";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void ToneMappingStage::setParams(const StageParams& p) {
    if (const auto* tp = std::get_if<ToneMappingParams>(&p)) {
        m_params   = *tp;
        m_lutDirty = true;
    }
}

// ─────────────────────────────────────────────────────────────────────
// buildLUT1D
// ─────────────────────────────────────────────────────────────────────

void ToneMappingStage::buildLUT1D() {
    const float wc       = std::max(m_params.whiteClip, 0.001F);
    const float contrast = m_params.contrast;

    m_lutScale = static_cast<float>(kLUTSize - 1) / wc;

    for (size_t i = 0; i < kLUTSize; ++i) {
        const float x = static_cast<float>(i) / m_lutScale; // scene-linear value

        float mapped = 0.0F;
        switch (m_params.op) {
            case ToneMappingParams::Operator::Linear:
                mapped = std::clamp(x, 0.0F, 1.0F);
                break;
            case ToneMappingParams::Operator::FilmicS:
                mapped = filmicS(x, contrast);
                break;
            case ToneMappingParams::Operator::ACES:
                mapped = acesHill(x, contrast);
                break;
        }
        m_lut[i] = mapped;
    }

    m_lutDirty = false;
}

// ─────────────────────────────────────────────────────────────────────
// applyLUT — linear interpolation look-up
// ─────────────────────────────────────────────────────────────────────

float ToneMappingStage::applyLUT(float v) const noexcept {
    if (v <= 0.0F) return m_lut[0];

    const float pos = v * m_lutScale;
    if (pos >= static_cast<float>(kLUTSize - 1)) return m_lut[kLUTSize - 1];

    const auto  idx  = static_cast<size_t>(pos);
    const float frac = pos - static_cast<float>(idx);

    return m_lut[idx] + (m_lut[idx + 1] - m_lut[idx]) * frac;
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer ToneMappingStage::process(const ImageBuffer& in) {
    if (MetalContext::shared().isAvailable()) {
        return processGPU(in);
    }
    return processCPU(in);
}

ImageBuffer ToneMappingStage::processCPU(const ImageBuffer& in) {
    // ── Input validation ─────────────────────────────────────────────
    if (in.width == 0 || in.height == 0) {
        std::cerr << "[ToneMappingStage] Warning: empty input buffer. Returning empty.\n";
        return {};
    }

    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[ToneMappingStage] Warning: input is not PROPHOTO_LINEAR "
                     "(got " << static_cast<int>(in.colorSpace)
                  << "). Returning input unchanged.\n";
        return in;
    }

    if (m_lutDirty) {
        buildLUT1D();
    }

    // ── Allocate output ──────────────────────────────────────────────
    auto result = ImageBuffer::create(in.width, in.height,
                                      ColorSpace::PROPHOTO_LINEAR);
    if (!result) {
        std::cerr << "[ToneMappingStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        return in;
    }
    ImageBuffer& out = *result;
    out.whitePoint = in.whitePoint;
    out.isLinear   = false;  // Tone-mapped data is no longer scene-linear.

    const size_t px    = in.pixelCount();
    const float* src   = in.ptr();
    float*       dst   = out.ptr();

    // ── Apply tone mapping via LUT ───────────────────────────────────
    for (size_t i = 0; i < px; ++i) {
        const size_t off = i * 3;
        dst[off + 0] = applyLUT(src[off + 0]);
        dst[off + 1] = applyLUT(src[off + 1]);
        dst[off + 2] = applyLUT(src[off + 2]);
    }

    // ── Post-tone-map saturation adjustment ──────────────────────────
    const float sat = m_params.saturation;
    if (sat != 1.0F) {
        for (size_t i = 0; i < px; ++i) {
            const size_t off = i * 3;
            const float r = dst[off + 0];
            const float g = dst[off + 1];
            const float b = dst[off + 2];

            // Rec.709 luminance coefficients.
            const float luma = 0.2126F * r + 0.7152F * g + 0.0722F * b;

            dst[off + 0] = std::clamp(luma + (r - luma) * sat, 0.0F, 1.0F);
            dst[off + 1] = std::clamp(luma + (g - luma) * sat, 0.0F, 1.0F);
            dst[off + 2] = std::clamp(luma + (b - luma) * sat, 0.0F, 1.0F);
        }
    }

    return out;
}

ImageBuffer ToneMappingStage::processGPU(const ImageBuffer& in) {
    if (in.width == 0 || in.height == 0) {
        std::cerr << "[ToneMappingStage] Warning: empty input buffer. Returning empty.\n";
        return {};
    }

    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[ToneMappingStage] Warning: GPU input is not PROPHOTO_LINEAR. "
                     "Falling back to CPU.\n";
        return processCPU(in);
    }

    auto& ctx = MetalContext::shared();
    void* inTex = ctx.uploadTexture(in);
    if (!inTex) return processCPU(in);

    auto result = ImageBuffer::create(in.width, in.height, ColorSpace::PROPHOTO_LINEAR);
    if (!result) {
        ctx.releaseTexture(inTex);
        return in;
    }
    ImageBuffer& emptyOut = *result;
    void* outTex = ctx.uploadTexture(emptyOut);
    if (!outTex) {
        ctx.releaseTexture(inTex);
        return processCPU(in);
    }

    struct alignas(16) ToneMapUniforms {
        int   op;
        float contrast;
        float saturation;
        float whiteClip;
    } uniforms;

    uniforms.op         = static_cast<int>(m_params.op);
    uniforms.contrast   = m_params.contrast;
    uniforms.saturation = m_params.saturation;
    uniforms.whiteClip  = m_params.whiteClip;

    ctx.dispatchKernel("toneMappingKernel", inTex, outTex, &uniforms, sizeof(uniforms));
    ImageBuffer outDl = ctx.downloadTexture(outTex, ColorSpace::PROPHOTO_LINEAR);

    ctx.releaseTexture(inTex);
    ctx.releaseTexture(outTex);

    outDl.whitePoint = in.whitePoint;
    outDl.isLinear   = false;

    return outDl;
}

} // namespace aether
