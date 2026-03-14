/// @file ColorMatrixStage.cpp
/// @brief Implementation of ColorMatrixStage — camera RGB → ProPhoto RGB
///        via composed 3×3 matrix with ARM NEON acceleration.

#include "ColorMatrixStage.hpp"

#include <algorithm>
#include <array>
#include <iostream>

#include "../gpu/MetalContext.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHER_HAS_NEON 1
#else
#define AETHER_HAS_NEON 0
#endif

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// Constants — XYZ D50 → ProPhoto RGB (row-major 3×3)
// ─────────────────────────────────────────────────────────────────────
// Source: ICC ProPhoto RGB specification (ROMM RGB), D50 reference white.

static constexpr std::array<float, 9> kXYZ_D50_to_ProPhoto = {
     1.3459433F, -0.2556075F, -0.0511118F,
    -0.5445989F,  1.5081673F,  0.0205351F,
     0.0000000F,  0.0000000F,  1.2118128F
};

// ─────────────────────────────────────────────────────────────────────
// White Balance & Chromatic Adaptation Helpers
// ─────────────────────────────────────────────────────────────────────

/// @brief Approximate CIE xy from Kelvin (Planckian locus).
/// Formulas from Wyszecki & Stiles or Krystek's algorithm.
[[nodiscard]] static WhitePoint kelvinToXy(float K) noexcept {
    float x = 0.0f;
    if (K >= 1666.6f && K <= 4000.0f) {
        x = -0.2661239f * (1e9f / (K * K * K)) - 0.2343580f * (1e6f / (K * K)) + 0.8776956f * (1e3f / K) + 0.179910f;
    } else if (K > 4000.0f && K <= 25000.0f) {
        x = -3.0258469f * (1e9f / (K * K * K)) + 2.1070379f * (1e6f / (K * K)) + 0.2226347f * (1e3f / K) + 0.240390f;
    }
    float y = (K <= 2222.0f) 
        ? -1.1063814f * (x * x * x) - 1.3481102f * (x * x) + 2.1855583f * x - 0.202196f
        : (K <= 4000.0f)
            ? -0.9549476f * (x * x * x) - 1.3741859f * (x * x) + 2.0913701f * x - 0.167488f
            : 3.0817580f * (x * x * x) - 5.8733867f * (x * x) + 3.7511299f * x - 0.467352f;
    return {x, y};
}

/// @brief Shift xy chromaticity along the Green-Magenta axis (Tint).
[[nodiscard]] static WhitePoint applyTint(WhitePoint wp, float tint) noexcept {
    // Tint shift is roughly perpendicular to the Planckian locus in xy space.
    // The Planckian locus at ~5000K has a slope of approx -1.0. 
    // Perpendicular direction is roughly [1, 1].
    // Scaling for [-150, 150] range to be subtle but visible.
    const float factor = tint / 3000.0f;
    return { wp.x + factor * 0.05f, wp.y + factor * 0.05f };
}

/// @brief Row-major 3×3 matrix multiply: C = A × B.
[[nodiscard]] static std::array<float, 9> multiplyMatrix3x3(
    const std::array<float, 9>& A,
    const std::array<float, 9>& B) noexcept
{
    std::array<float, 9> C{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            C[static_cast<size_t>(r * 3 + c)] =
                A[static_cast<size_t>(r * 3 + 0)] * B[static_cast<size_t>(0 * 3 + c)] +
                A[static_cast<size_t>(r * 3 + 1)] * B[static_cast<size_t>(1 * 3 + c)] +
                A[static_cast<size_t>(r * 3 + 2)] * B[static_cast<size_t>(2 * 3 + c)];
        }
    }
    return C;
}

/// @brief Compute a Bradford chromatic adaptation matrix (Source XYZ -> Dest XYZ).
[[nodiscard]] static std::array<float, 9> bradfordAdaptation(WhitePoint src, WhitePoint dest) noexcept {
    auto xyToXYZ = [](WhitePoint p) {
        return std::array<float, 3>{p.x / p.y, 1.0f, (1.0f - p.x - p.y) / p.y};
    };
    auto sXYZ = xyToXYZ(src);
    auto dXYZ = xyToXYZ(dest);

    // Bradford / CAT02 cone response matrix (Mc)
    static constexpr std::array<float, 9> Mc = {
         0.7328f,  0.4296f, -0.1624f,
        -0.7036f,  1.6975f,  0.0061f,
         0.0308f,  0.0136f,  0.9658f
    };

    // Inverse cone response matrix (Mc_inv)
    static constexpr std::array<float, 9> Mc_inv = {
         1.096124f, -0.278869f,  0.182745f,
         0.454369f,  0.473531f,  0.072099f,
        -0.041210f, -0.005728f,  1.046939f
    };

    auto transform = [&](const std::array<float, 9>& mat, const std::array<float, 3>& vec) {
        return std::array<float, 3>{
            mat[0] * vec[0] + mat[1] * vec[1] + mat[2] * vec[2],
            mat[3] * vec[0] + mat[4] * vec[1] + mat[5] * vec[2],
            mat[6] * vec[0] + mat[7] * vec[1] + mat[8] * vec[2]
        };
    };

    auto sLMS = transform(Mc, sXYZ);
    auto dLMS = transform(Mc, dXYZ);

    std::array<float, 9> R = {0};
    R[0] = dLMS[0] / sLMS[0];
    R[4] = dLMS[1] / sLMS[1];
    R[8] = dLMS[2] / sLMS[2];

    // CAT = Mc_inv * R * Mc
    auto temp = multiplyMatrix3x3(R, Mc);
    return multiplyMatrix3x3(Mc_inv, temp);
}

/// @brief Linearly interpolate two 3×3 matrices element-wise.
/// @param A      Matrix at blend = 0.
/// @param B      Matrix at blend = 1.
/// @param blend  Interpolation factor in [0, 1].
[[nodiscard]] static std::array<float, 9> lerpMatrix3x3(
    const std::array<float, 9>& A,
    const std::array<float, 9>& B,
    float blend) noexcept
{
    std::array<float, 9> out{};
    for (size_t i = 0; i < 9; ++i) {
        out[i] = A[i] * (1.0F - blend) + B[i] * blend;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────
// PIMPL
// ─────────────────────────────────────────────────────────────────────

struct ColorMatrixStage::Impl {
    ColorMatrixParams        params;
    std::array<float, 9>     finalMatrix{};  ///< Composed Camera→ProPhoto.
    bool                     matrixDirty{true};

    /// Rebuild the composed matrix from current params.
    void buildTransform() {
        // 1. Calculate source white from Temperature and Tint.
        WhitePoint srcWp = kelvinToXy(params.temperature);
        srcWp = applyTint(srcWp, params.tint);

        // 2. Interpolate Camera→XYZ between D50 (ColorMatrix1) and D65 (ColorMatrix2).
        // Standard DNG logic uses the specific illuminant temperatures:
        // StdA (approx 2856K) and D65 (6504K).
        float blend = std::clamp((params.temperature - 2856.0f) / (6504.0f - 2856.0f), 0.0f, 1.0f);
        
        bool hasMatrix2 = false;
        for (float v : params.cameraToXYZ_D65) {
            if (std::abs(v) > 1e-6f) {
                hasMatrix2 = true;
                break;
            }
        }

        auto camToXYZ = hasMatrix2
            ? lerpMatrix3x3(params.cameraToXYZ_D50, params.cameraToXYZ_D65, blend)
            : params.cameraToXYZ_D50;

        // 3. Chromatic Adaptation: Source White -> Destination White (D50 for ProPhoto).
        auto cat = bradfordAdaptation(srcWp, WhitePoint::D50());

        // 4. Compose:  Camera→XYZ  ×  CAT  ×  XYZ→ProPhoto  =  Camera→ProPhoto
        auto adaptedCamToXYZ = multiplyMatrix3x3(cat, camToXYZ);
        finalMatrix = multiplyMatrix3x3(kXYZ_D50_to_ProPhoto, adaptedCamToXYZ);

        matrixDirty = false;
    }
};

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────

ColorMatrixStage::ColorMatrixStage()
    : m_impl{std::make_unique<Impl>()} {}

ColorMatrixStage::~ColorMatrixStage() = default;

// ─────────────────────────────────────────────────────────────────────
// Introspection
// ─────────────────────────────────────────────────────────────────────

StageId ColorMatrixStage::id() const noexcept {
    return StageId::ColorMatrix;
}

bool ColorMatrixStage::supportsGPU() const noexcept {
    return true;
}

std::string_view ColorMatrixStage::description() const noexcept {
    return "Camera RGB → ProPhoto RGB colour matrix (linear, D50)";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void ColorMatrixStage::setParams(const StageParams& p) {
    if (const auto* cmp = std::get_if<ColorMatrixParams>(&p)) {
        m_impl->params      = *cmp;
        m_impl->matrixDirty = true;
    }
    // Silently ignore unrelated types.
}

StageParams ColorMatrixStage::getParams() const {
    return m_impl->params;
}

// ─────────────────────────────────────────────────────────────────────
// process — NEON path
// ─────────────────────────────────────────────────────────────────────

#if AETHER_HAS_NEON

/// @brief Apply a 3×3 matrix to interleaved RGB float data using NEON.
///
/// Processes 4 pixels at a time by loading each row-coefficient as a
/// uniform and performing fused multiply-add across the R, G, B lanes.
static void applyMatrix3x3_NEON(
    const float*                   src,
    float*                         dst,
    size_t                         pixelCount,
    const std::array<float, 9>&    mat) noexcept
{
    // Load matrix rows into NEON scalars (broadcast).
    const float32x4_t m00 = vdupq_n_f32(mat[0]);
    const float32x4_t m01 = vdupq_n_f32(mat[1]);
    const float32x4_t m02 = vdupq_n_f32(mat[2]);
    const float32x4_t m10 = vdupq_n_f32(mat[3]);
    const float32x4_t m11 = vdupq_n_f32(mat[4]);
    const float32x4_t m12 = vdupq_n_f32(mat[5]);
    const float32x4_t m20 = vdupq_n_f32(mat[6]);
    const float32x4_t m21 = vdupq_n_f32(mat[7]);
    const float32x4_t m22 = vdupq_n_f32(mat[8]);

    const float32x4_t zero = vdupq_n_f32(0.0F);

    // Process 4 pixels at a time.
    // Data layout: R0 G0 B0 R1 G1 B1 R2 G2 B2 R3 G3 B3
    // We deinterleave to get R×4, G×4, B×4 using vld3q.
    const size_t fullQuads = pixelCount / 4;
    size_t i = 0;

    for (size_t q = 0; q < fullQuads; ++q, i += 4) {
        const size_t off = i * 3;

        // Load 4 interleaved RGB pixels → deinterleave to R, G, B.
        const float32x4x3_t rgb = vld3q_f32(src + off);

        // Matrix multiply per channel.
        float32x4_t outR = vmulq_f32(m00, rgb.val[0]);
        outR = vfmaq_f32(outR, m01, rgb.val[1]);
        outR = vfmaq_f32(outR, m02, rgb.val[2]);

        float32x4_t outG = vmulq_f32(m10, rgb.val[0]);
        outG = vfmaq_f32(outG, m11, rgb.val[1]);
        outG = vfmaq_f32(outG, m12, rgb.val[2]);

        float32x4_t outB = vmulq_f32(m20, rgb.val[0]);
        outB = vfmaq_f32(outB, m21, rgb.val[1]);
        outB = vfmaq_f32(outB, m22, rgb.val[2]);

        // Clamp low to 0 (preserve HDR highlights — no high clamp).
        outR = vmaxq_f32(outR, zero);
        outG = vmaxq_f32(outG, zero);
        outB = vmaxq_f32(outB, zero);

        // Re-interleave and store.
        const float32x4x3_t result = {outR, outG, outB};
        vst3q_f32(dst + off, result);
    }

    // Scalar tail for remaining pixels.
    for (; i < pixelCount; ++i) {
        const size_t off = i * 3;
        const float r = src[off + 0];
        const float g = src[off + 1];
        const float b = src[off + 2];

        dst[off + 0] = std::max(0.0F, mat[0] * r + mat[1] * g + mat[2] * b);
        dst[off + 1] = std::max(0.0F, mat[3] * r + mat[4] * g + mat[5] * b);
        dst[off + 2] = std::max(0.0F, mat[6] * r + mat[7] * g + mat[8] * b);
    }
}

#endif // AETHER_HAS_NEON

// ─────────────────────────────────────────────────────────────────────
// process — scalar fallback (compiled only when NEON is unavailable)
// ─────────────────────────────────────────────────────────────────────

#if !AETHER_HAS_NEON

static void applyMatrix3x3_Scalar(
    const float*                   src,
    float*                         dst,
    size_t                         pixelCount,
    const std::array<float, 9>&    mat) noexcept
{
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t off = i * 3;
        const float r = src[off + 0];
        const float g = src[off + 1];
        const float b = src[off + 2];

        dst[off + 0] = std::max(0.0F, mat[0] * r + mat[1] * g + mat[2] * b);
        dst[off + 1] = std::max(0.0F, mat[3] * r + mat[4] * g + mat[5] * b);
        dst[off + 2] = std::max(0.0F, mat[6] * r + mat[7] * g + mat[8] * b);
    }
}

#endif // !AETHER_HAS_NEON

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer ColorMatrixStage::process(const ImageBuffer& in) {
    if (MetalContext::shared().isAvailable()) {
        return processGPU(in);
    }
    return processCPU(in);
}

ImageBuffer ColorMatrixStage::processCPU(const ImageBuffer& in) {
    // ── Input validation ─────────────────────────────────────────────
    if (in.width == 0 || in.height == 0) {
        std::cerr << "[ColorMatrixStage] Warning: empty input buffer. Returning empty.\n";
        return {};
    }

    if (in.colorSpace != ColorSpace::LINEAR_RAW) {
        std::cerr << "[ColorMatrixStage] Warning: input is not LINEAR_RAW "
                     "(got " << static_cast<int>(in.colorSpace)
                  << "). Returning input unchanged.\n";
        return in;
    }

    // ── Rebuild matrix if params changed ─────────────────────────────
    if (m_impl->matrixDirty) {
        m_impl->buildTransform();
    }

    // ── Allocate output buffer ───────────────────────────────────────
    auto result = ImageBuffer::create(in.width, in.height,
                                      ColorSpace::PROPHOTO_LINEAR);
    if (!result) {
        std::cerr << "[ColorMatrixStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        return in;
    }
    ImageBuffer& out = *result;
    out.whitePoint = WhitePoint::D50();
    out.isLinear   = true;

    // ── Apply matrix ─────────────────────────────────────────────────
    const size_t px = in.pixelCount();

#if AETHER_HAS_NEON
    applyMatrix3x3_NEON(in.ptr(), out.ptr(), px, m_impl->finalMatrix);
#else
    applyMatrix3x3_Scalar(in.ptr(), out.ptr(), px, m_impl->finalMatrix);
#endif

    return out;
}

ImageBuffer ColorMatrixStage::processGPU(const ImageBuffer& in) {
    if (in.width == 0 || in.height == 0) {
        std::cerr << "[ColorMatrixStage] Warning: empty input buffer. Returning empty.\n";
        return {};
    }

    if (in.colorSpace != ColorSpace::LINEAR_RAW) {
        std::cerr << "[ColorMatrixStage] Warning: GPU input is not LINEAR_RAW. "
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

    // Metal float3x3 is 3 column vectors, each aligned to 16 bytes (4 floats).
    struct alignas(16) { float matrix[12]; } uniforms;
    const auto& m = m_impl->finalMatrix;
    
    // Column 0
    uniforms.matrix[0] = m[0]; uniforms.matrix[1] = m[3]; uniforms.matrix[2] = m[6]; uniforms.matrix[3] = 0.0F;
    // Column 1
    uniforms.matrix[4] = m[1]; uniforms.matrix[5] = m[4]; uniforms.matrix[6] = m[7]; uniforms.matrix[7] = 0.0F;
    // Column 2
    uniforms.matrix[8] = m[2]; uniforms.matrix[9] = m[5]; uniforms.matrix[10]= m[8]; uniforms.matrix[11]= 0.0F;

    ctx.dispatchKernel("colorMatrixKernel", inTex, outTex, &uniforms, sizeof(uniforms));
    ImageBuffer outDl = ctx.downloadTexture(outTex, ColorSpace::PROPHOTO_LINEAR);

    ctx.releaseTexture(inTex);
    ctx.releaseTexture(outTex);

    outDl.whitePoint = WhitePoint::D50();
    outDl.isLinear = true;

    return outDl;
}

} // namespace aether
