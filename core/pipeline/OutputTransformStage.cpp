/// @file OutputTransformStage.cpp
/// @brief Implementation of OutputTransformStage — ProPhoto Linear (D50)
///        → sRGB / Display P3 via LittleCMS 2.

#include "OutputTransformStage.hpp"

#include <algorithm>
#include <iostream>

#include <lcms2.h>
#include "../gpu/MetalContext.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHER_HAS_NEON 1
#else
#define AETHER_HAS_NEON 0
#endif

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// PIMPL
// ─────────────────────────────────────────────────────────────────────

struct OutputTransformStage::Impl {
    OutputTransformParams params;
    cmsHTRANSFORM         transform{nullptr};

    void destroyTransform() {
        if (transform) {
            cmsDeleteTransform(transform);
            transform = nullptr;
        }
    }

    void buildTransform() {
        destroyTransform();

        // ── Source profile: ProPhoto RGB (linear, D50) ───────────────
        cmsCIExyY srcWhite = {0.3457, 0.3585, 1.0};  // D50
        cmsCIExyYTRIPLE srcPrimaries = {
            {0.7347, 0.2653, 1.0},  // Red
            {0.1596, 0.8404, 1.0},  // Green
            {0.0366, 0.0001, 1.0}   // Blue
        };
        cmsToneCurve* linearGamma = cmsBuildGamma(nullptr, 1.0);
        cmsToneCurve* srcTRC[3]   = {linearGamma, linearGamma, linearGamma};

        cmsHPROFILE srcProfile = cmsCreateRGBProfile(&srcWhite,
                                                      &srcPrimaries,
                                                      srcTRC);
        cmsFreeToneCurve(linearGamma);

        // ── Destination profile ──────────────────────────────────────
        cmsHPROFILE dstProfile = nullptr;

        if (params.displaySpace == ColorSpace::DISPLAY_P3) {
            // Display P3 with sRGB-like OETF (gamma ≈ 2.2)
            cmsCIExyY dstWhite = {0.3127, 0.3290, 1.0};  // D65
            cmsCIExyYTRIPLE p3Primaries = {
                {0.680, 0.320, 1.0},  // Red
                {0.265, 0.690, 1.0},  // Green
                {0.150, 0.060, 1.0}   // Blue
            };
            // IEC 61966-2-1 sRGB parametric curve (type 4):
            //   y = (a*x + b)^g + c  for x >= d
            //   y = e*x + f          for x < d
            constexpr cmsFloat64Number srgbCurveParams[7] = {
                2.4, 1.0/1.055, 0.055/1.055, 1.0/12.92, 0.04045, 0.0, 0.0
            };
            cmsToneCurve* srgbTRC = cmsBuildParametricToneCurve(
                nullptr, 4, srgbCurveParams);
            cmsToneCurve* dstTRC[3] = {srgbTRC, srgbTRC, srgbTRC};

            dstProfile = cmsCreateRGBProfile(&dstWhite, &p3Primaries, dstTRC);
            cmsFreeToneCurve(srgbTRC);
        } else {
            // Default: sRGB
            dstProfile = cmsCreate_sRGBProfile();
        }

        // ── Create transform ─────────────────────────────────────────
        if (srcProfile && dstProfile) {
            transform = cmsCreateTransform(
                srcProfile, TYPE_RGB_FLT,
                dstProfile, TYPE_RGB_FLT,
                INTENT_PERCEPTUAL,
                cmsFLAGS_NOCACHE | cmsFLAGS_HIGHRESPRECALC);
        }

        if (srcProfile) cmsCloseProfile(srcProfile);
        if (dstProfile) cmsCloseProfile(dstProfile);

        if (!transform) {
            std::cerr << "[OutputTransformStage] Failed to create "
                         "LittleCMS transform.\n";
        }
    }
};

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────

OutputTransformStage::OutputTransformStage()
    : m_impl{std::make_unique<Impl>()} {
    m_impl->buildTransform();
}

OutputTransformStage::~OutputTransformStage() {
    m_impl->destroyTransform();
}

// ─────────────────────────────────────────────────────────────────────
// Introspection
// ─────────────────────────────────────────────────────────────────────

StageId OutputTransformStage::id() const noexcept {
    return StageId::OutputTransform;
}

bool OutputTransformStage::supportsGPU() const noexcept {
    return true;
}

std::string_view OutputTransformStage::description() const noexcept {
    return "ProPhoto Linear → Display (sRGB/P3) via LittleCMS";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void OutputTransformStage::setParams(const StageParams& p) {
    if (const auto* otp = std::get_if<OutputTransformParams>(&p)) {
        m_impl->params = *otp;
        m_impl->buildTransform();  // Rebuild on param change.
    }
}

// ─────────────────────────────────────────────────────────────────────
// Clamp helper
// ─────────────────────────────────────────────────────────────────────

static void clampBuffer01(float* data, size_t count) noexcept {
#if AETHER_HAS_NEON
    const float32x4_t zero = vdupq_n_f32(0.0F);
    const float32x4_t one  = vdupq_n_f32(1.0F);
    const size_t simdEnd   = (count / 4) * 4;

    for (size_t i = 0; i < simdEnd; i += 4) {
        float32x4_t v = vld1q_f32(data + i);
        v = vmaxq_f32(v, zero);
        v = vminq_f32(v, one);
        vst1q_f32(data + i, v);
    }
    for (size_t i = simdEnd; i < count; ++i) {
        data[i] = std::clamp(data[i], 0.0F, 1.0F);
    }
#else
    for (size_t i = 0; i < count; ++i) {
        data[i] = std::clamp(data[i], 0.0F, 1.0F);
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer OutputTransformStage::process(const ImageBuffer& in) {
    if (MetalContext::shared().isAvailable()) {
        return processGPU(in);
    }
    return processCPU(in);
}

ImageBuffer OutputTransformStage::processCPU(const ImageBuffer& in) {
    // ── Input validation ─────────────────────────────────────────────
    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[OutputTransformStage] Warning: input is not "
                     "PROPHOTO_LINEAR (got "
                  << static_cast<int>(in.colorSpace)
                  << "). Returning input unchanged.\n";
        return in;
    }

    if (!m_impl->transform) {
        std::cerr << "[OutputTransformStage] No valid transform — "
                     "returning input unchanged.\n";
        return in;
    }

    // ── Allocate output ──────────────────────────────────────────────
    auto result = ImageBuffer::create(in.width, in.height,
                                      m_impl->params.displaySpace);
    if (!result) {
        std::cerr << "[OutputTransformStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        return in;
    }
    ImageBuffer& out = *result;
    out.whitePoint = WhitePoint::D65();
    out.isLinear   = false;

    // ── Apply LittleCMS transform ────────────────────────────────────
    cmsDoTransform(m_impl->transform,
                   in.ptr(),
                   out.ptr(),
                   static_cast<cmsUInt32Number>(in.pixelCount()));

    // ── Clamp [0, 1] (out-of-gamut ProPhoto colours) ─────────────────
    const size_t totalFloats = out.data.size();
    clampBuffer01(out.ptr(), totalFloats);

    return out;
}

ImageBuffer OutputTransformStage::processGPU(const ImageBuffer& in) {
    if (in.colorSpace != ColorSpace::PROPHOTO_LINEAR) {
        std::cerr << "[OutputTransformStage] Warning: input is not PROPHOTO_LINEAR\n";
        return in;
    }

    auto& ctx = MetalContext::shared();
    void* inTex = ctx.uploadTexture(in);
    if (!inTex) return processCPU(in);

    auto result = ImageBuffer::create(in.width, in.height, m_impl->params.displaySpace);
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

    struct alignas(16) OutputUniforms {
        float matrix[12];
        float gamma;
        float padding[3];
    } uniforms;

    // ProPhoto to sRGB (D65)
    static constexpr float kProPhotoToSRGB[9] = {
         2.034130F, -0.727409F, -0.306721F,
        -0.228807F,  1.231713F, -0.002905F,
        -0.008565F, -0.153272F,  1.161838F
    };
    
    // ProPhoto to P3 (D65)
    static constexpr float kProPhotoToP3[9] = {
         1.510344F, -0.354670F, -0.155675F,
        -0.052044F,  1.057396F, -0.005352F,
        -0.004246F, -0.046343F,  1.050588F
    };

    const float* m = (m_impl->params.displaySpace == ColorSpace::DISPLAY_P3) 
                     ? kProPhotoToP3 
                     : kProPhotoToSRGB;

    uniforms.matrix[0] = m[0]; uniforms.matrix[1] = m[3]; uniforms.matrix[2] = m[6]; uniforms.matrix[3] = 0.0F;
    uniforms.matrix[4] = m[1]; uniforms.matrix[5] = m[4]; uniforms.matrix[6] = m[7]; uniforms.matrix[7] = 0.0F;
    uniforms.matrix[8] = m[2]; uniforms.matrix[9] = m[5]; uniforms.matrix[10]= m[8]; uniforms.matrix[11]= 0.0F;

    uniforms.gamma = 2.2F; // Approx sRGB/P3 gamma for the shader

    ctx.dispatchKernel("outputTransformKernel", inTex, outTex, &uniforms, sizeof(uniforms));
    ImageBuffer outDl = ctx.downloadTexture(outTex, m_impl->params.displaySpace);

    ctx.releaseTexture(inTex);
    ctx.releaseTexture(outTex);

    outDl.whitePoint = WhitePoint::D65();
    outDl.isLinear = false;

    return outDl;
}

} // namespace aether
