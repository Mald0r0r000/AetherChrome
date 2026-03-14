/// @file MaskCompositeStage.cpp
/// @brief Implementation of MaskCompositeStage — parametric mask generation,
///        merging, and local adjustment application.

#include "MaskCompositeStage.hpp"
#include "../mask/ParametricMaskBuilder.hpp"

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

StageId MaskCompositeStage::id() const noexcept {
    return StageId::MaskComposite;
}

bool MaskCompositeStage::supportsGPU() const noexcept {
    return false;
}

std::string_view MaskCompositeStage::description() const noexcept {
    return "Parametric mask composite with local adjustments";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void MaskCompositeStage::setParams(const StageParams& p) {
    if (const auto* mcp = std::get_if<MaskCompositeParams>(&p)) {
        m_params = *mcp;
    }
}

StageParams MaskCompositeStage::getParams() const {
    return m_params;
}

// ─────────────────────────────────────────────────────────────────────
// Mask merging helpers
// ─────────────────────────────────────────────────────────────────────

/// @brief Merge two alpha arrays using the specified BlendMode.
static void mergeMasks(std::vector<float>& dst,
                       const std::vector<float>& src,
                       BlendMode mode) noexcept {
    const size_t n = std::min(dst.size(), src.size());
    for (size_t i = 0; i < n; ++i) {
        const float a = dst[i];
        const float b = src[i];
        switch (mode) {
            case BlendMode::Multiply:
                dst[i] = a * b;
                break;
            case BlendMode::Screen:
                dst[i] = 1.0F - (1.0F - a) * (1.0F - b);
                break;

            case BlendMode::Normal:
            default:
                // Normal alpha-over.
                dst[i] = a + b * (1.0F - a);
                break;
        }

    }
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer MaskCompositeStage::process(const ImageBuffer& in) {
    if (m_params.layerCount == 0)
        return in;  // pass-through total, aucune modification

    // ── Input validation ─────────────────────────────────────────────
    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[MaskCompositeStage] Warning: input is not "
                     "PROPHOTO_LINEAR. Returning input unchanged.\n";
        return in;
    }

    const size_t px = in.pixelCount();

    // ── Step 1 : Build and merge masks ───────────────────────────────
    std::vector<float> combinedAlpha(px, 0.0F);
    bool firstMask = true;

    for (uint8_t li = 0; li < m_params.layerCount && li < 8; ++li) {
        if (!m_params.layerEnabled[li]) continue;

        MaskLayer mask = ParametricMaskBuilder::build(in, m_params.layers[li]);
        
        // Apply per-layer settings.
        mask.blend = m_params.layerBlend[li];
        if (m_params.layerInverted[li]) {
            for (auto& a : mask.alpha) a = 1.0F - a;
        }

        if (firstMask) {
            combinedAlpha = std::move(mask.alpha);
            firstMask = false;
        } else {
            mergeMasks(combinedAlpha, mask.alpha, mask.blend);
        }
    }

    // ── AI Mask Integration ──────────────────────────────────────────
    if (m_params.hasAiMask) {
        if (firstMask) {
            combinedAlpha = m_params.aiMask.alpha;
            firstMask = false;
        } else {
            mergeMasks(combinedAlpha, m_params.aiMask.alpha, m_params.aiMask.blend);
        }
    }


    if (firstMask) {
        // No enabled layers → pass-through.
        return in;
    }

    // ── Step 2 : Allocate output ─────────────────────────────────────
    auto result = ImageBuffer::create(in.width, in.height,
                                      ColorSpace::PROPHOTO_LINEAR);
    if (!result) {
        std::cerr << "[MaskCompositeStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        return in;
    }
    ImageBuffer& out = *result;
    out.whitePoint = in.whitePoint;
    out.isLinear   = in.isLinear;

    const float* src = in.ptr();
    float*       dst = out.ptr();

    // ── Step 3 : Apply local adjustments + blend ─────────────────────
    const float gain      = std::pow(2.0F, m_params.exposureInMask);
    const float satMask   = m_params.saturationInMask;
    const float contrast  = m_params.contrastInMask;

    for (size_t i = 0; i < px; ++i) {
        const size_t off = i * 3;
        const float alpha = combinedAlpha[i];

        float r = src[off + 0];
        float g = src[off + 1];
        float b = src[off + 2];

        // ── Local exposure ───────────────────────────────────────────
        float ar = r * gain;
        float ag = g * gain;
        float ab = b * gain;

        // ── Local contrast (pivot at 0.5) ────────────────────────────
        if (contrast != 1.0F) {
            ar = 0.5F + (ar - 0.5F) * contrast;
            ag = 0.5F + (ag - 0.5F) * contrast;
            ab = 0.5F + (ab - 0.5F) * contrast;
        }

        // ── Local saturation ─────────────────────────────────────────
        if (satMask != 1.0F) {
            const float luma = 0.2126F * ar + 0.7152F * ag + 0.0722F * ab;
            ar = luma + (ar - luma) * satMask;
            ag = luma + (ag - luma) * satMask;
            ab = luma + (ab - luma) * satMask;
        }

        // ── Alpha blend: original × (1-α) + adjusted × α ────────────
        dst[off + 0] = r * (1.0F - alpha) + ar * alpha;
        dst[off + 1] = g * (1.0F - alpha) + ag * alpha;
        dst[off + 2] = b * (1.0F - alpha) + ab * alpha;
    }

    return out;
}

} // namespace aether
