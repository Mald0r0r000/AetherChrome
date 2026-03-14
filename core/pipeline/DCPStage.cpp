#include "DCPStage.hpp"
#include "../ImageBuffer.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// RGB <-> HSV Utility (Internal)
// ─────────────────────────────────────────────────────────────────────

static void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float max = std::max({r, g, b});
    float min = std::min({r, g, b});
    float delta = max - min;

    v = max;
    s = (max > 1e-6f) ? (delta / max) : 0.0f;

    if (delta < 1e-6f) {
        h = 0.0f;
    } else {
        if (max == r) h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
        else if (max == g) h = 60.0f * (((b - r) / delta) + 2.0f);
        else h = 60.0f * (((r - g) / delta) + 4.0f);
        if (h < 0.0f) h += 360.0f;
    }
}

static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    if (h < 60.0f)  { r = c; g = x; b = 0; }
    else if (h < 120.0f) { r = x; g = c; b = 0; }
    else if (h < 180.0f) { r = 0; g = c; b = x; }
    else if (h < 240.0f) { r = 0; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    r += m; g += m; b += m;
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer DCPStage::process(const ImageBuffer& in) {
    std::cerr << "[DCPStage] process() called. width=" << in.width << " profile=" << (m_profile.has_value() ? "YES" : "NO") << "\n";
    if (!m_profile.has_value() || in.width == 0) return in;

    // ProPhoto RGB (ROMM) standard: White point D50
    auto outRes = ImageBuffer::create(in.width, in.height, ColorSpace::PROPHOTO_LINEAR);
    if (!outRes) return in;
    ImageBuffer out = std::move(*outRes);
    out.whitePoint = WhitePoint::D50();
    out.isLinear = true;

    const float* src = in.ptr();
    float* dst = out.ptr();

    std::cerr << "[DCPStage] Output buffer created with colorSpace=" 
              << static_cast<int>(out.colorSpace) << "\n";

    // Matrice XYZ (D50) -> ProPhoto RGB (Linear)
    // Source: Bruce Lindbloom / ANSI/I3A IT10.7466-2002
    static constexpr float XYZ_TO_PROPHOTO[9] = {
         1.3459433f, -0.2556075f, -0.0511118f,
        -0.5445989f,  1.5081673f,  0.0205351f,
         0.0000000f,  0.0000000f,  1.2118128f
    };

    // 1. Calculer la matrice Camera -> XYZ interpolée
    auto mat = m_profile->interpolateMatrix(m_params.temperature);

    // 2. Calculer le blend pour la HueSatMap (interpolée entre A et D65)
    float t1 = (m_profile->illuminant1 == 17) ? 2856.0f : 6504.0f;
    float t2 = (m_profile->illuminant2 == 21) ? 6504.0f : 2856.0f;
    float blend = 0.5f;
    if (std::abs(t2 - t1) > 1.0f) {
        float invT  = 1.0f / m_params.temperature;
        float invT1 = 1.0f / t1;
        float invT2 = 1.0f / t2;
        blend = std::clamp((invT - invT1) / (invT2 - invT1), 0.0f, 1.0f);
    }

    const size_t pxCount = in.pixelCount();
    for (size_t i = 0; i < pxCount; ++i) {
        float r = src[i * 3 + 0];
        float g = src[i * 3 + 1];
        float b = src[i * 3 + 2];

        // --- A. Camera RGB -> XYZ ---
        float X = mat[0] * r + mat[1] * g + mat[2] * b;
        float Y = mat[3] * r + mat[4] * g + mat[5] * b;
        float Z = mat[6] * r + mat[7] * g + mat[8] * b;

        // --- B. HueSatMap (Optionnel) ---
        if (m_params.enableHueSatMap && m_profile->hasHueSatMap) {
            float h, s, v;
            rgbToHsv(X, Y, Z, h, s, v);
            m_profile->applyHueSatMap(h, s, v, blend);
            hsvToRgb(h, s, v, X, Y, Z);
        }

        // --- C. XYZ (D50) -> ProPhoto RGB ---
        float pr = XYZ_TO_PROPHOTO[0] * X + XYZ_TO_PROPHOTO[1] * Y + XYZ_TO_PROPHOTO[2] * Z;
        float pg = XYZ_TO_PROPHOTO[3] * X + XYZ_TO_PROPHOTO[4] * Y + XYZ_TO_PROPHOTO[5] * Z;
        float pb = XYZ_TO_PROPHOTO[6] * X + XYZ_TO_PROPHOTO[7] * Y + XYZ_TO_PROPHOTO[8] * Z;

        dst[i * 3 + 0] = std::max(0.0f, pr);
        dst[i * 3 + 1] = std::max(0.0f, pg);
        dst[i * 3 + 2] = std::max(0.0f, pb);
    }

    return out;
}

} // namespace aether
