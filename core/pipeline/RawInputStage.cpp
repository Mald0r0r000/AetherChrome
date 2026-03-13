/// @file RawInputStage.cpp
/// @brief Implementation of RawInputStage — LibRaw-based RAW file decoder.

#include "RawInputStage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include <libraw/libraw.h>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// PIMPL — hides LibRaw from the header
// ─────────────────────────────────────────────────────────────────────

struct RawInputStage::Impl {
    LibRaw                 libraw;           ///< LibRaw processor (RAII).
    std::filesystem::path  path;             ///< Most recently loaded file.
    RawIngestParams        params;           ///< Current ingest parameters.
    CameraMetadata         metadata;         ///< Filled during process().
    bool                   fileLoaded{false};///< True after successful loadFile().
};

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────

RawInputStage::RawInputStage()
    : m_impl{std::make_unique<Impl>()} {}

RawInputStage::~RawInputStage() = default;

// ─────────────────────────────────────────────────────────────────────
// IPipelineStage — introspection
// ─────────────────────────────────────────────────────────────────────

StageId RawInputStage::id() const noexcept {
    return StageId::RawIngest;
}

bool RawInputStage::supportsGPU() const noexcept {
    return false;
}

std::string_view RawInputStage::description() const noexcept {
    return "RAW file ingest via LibRaw (linear, no tone curve)";
}

// ─────────────────────────────────────────────────────────────────────
// setParams
// ─────────────────────────────────────────────────────────────────────

void RawInputStage::setParams(const StageParams& p) {
    if (const auto* rip = std::get_if<RawIngestParams>(&p)) {
        m_impl->params = *rip;
    }
    // Silently ignore unrelated types.
}

// ─────────────────────────────────────────────────────────────────────
// loadFile
// ─────────────────────────────────────────────────────────────────────

std::expected<void, ImageError>
RawInputStage::loadFile(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) {
        return std::unexpected(ImageError{
            .message = "File not found: " + path.string()
        });
    }
    m_impl->path = std::move(path);
    m_impl->fileLoaded = true;
    return {};
}

// ─────────────────────────────────────────────────────────────────────
// extractMetadata  (private)
// ─────────────────────────────────────────────────────────────────────

CameraMetadata RawInputStage::extractMetadata() const {
    const auto& idata = m_impl->libraw.imgdata.idata;
    const auto& other = m_impl->libraw.imgdata.other;
    const auto& color = m_impl->libraw.imgdata.color;

    CameraMetadata meta;
    meta.cameraMake  = idata.make;
    meta.cameraModel = idata.model;
    meta.isoValue    = other.iso_speed;
    meta.shutterSpeed = other.shutter;
    // Aperture — priority to CurAp from makernotes, fallback to standard aperture.
    if (m_impl->libraw.imgdata.lens.makernotes.CurAp > 0.1f) {
        meta.aperture = m_impl->libraw.imgdata.lens.makernotes.CurAp;
    } else {
        meta.aperture = other.aperture;
    }

    // Lens model — may be empty on some cameras.
    if (std::strlen(m_impl->libraw.imgdata.lens.Lens) > 0) {
        meta.lensModel = m_impl->libraw.imgdata.lens.Lens;
    }

    // ── colorMatrix1 from cam_xyz (3×4 row-major → 3×3) ─────────────
    // LibRaw's cam_xyz is a float[3][4] matrix mapping camera RGB to
    // XYZ (calculated from DNG/EXIF). We extract the 3×3 sub-matrix.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            meta.colorMatrix1[static_cast<size_t>(r * 3 + c)] =
                color.cam_xyz[r][c];
        }
    }

    // colorMatrix2 — not directly available from LibRaw at runtime;
    // leave zeroed (will be populated from DNG tags in a later stage).
    meta.colorMatrix2.fill(0.0F);

    // ── shotWhitePoint — approximate CIE xy from camera multipliers ──
    // cam_mul[0..2] are per-channel gain factors.  We treat them as a
    // rough indicator: normalise, invert to get relative energy, then
    // project through an sRGB→XYZ matrix to XYZ, and finally compute
    // xy chromaticity.  This is a V1 approximation.
    const float rMul = color.cam_mul[0];
    const float gMul = color.cam_mul[1];
    const float bMul = color.cam_mul[2];

    if (rMul > 0.0F && gMul > 0.0F && bMul > 0.0F) {
        // Invert and normalise so that green = 1.
        const float invR = gMul / rMul;
        const float invG = 1.0F;
        const float invB = gMul / bMul;

        // sRGB linear → XYZ (D65), row-major.
        const float X = 0.4124564F * invR + 0.3575761F * invG + 0.1804375F * invB;
        const float Y = 0.2126729F * invR + 0.7151522F * invG + 0.0721750F * invB;
        const float Z = 0.0193339F * invR + 0.1191920F * invG + 0.9503041F * invB;

        const float sum = X + Y + Z;
        if (sum > 1e-6F) {
            meta.shotWhitePoint = WhitePoint{.x = X / sum, .y = Y / sum};
        } else {
            meta.shotWhitePoint = WhitePoint::D65();
        }
    } else {
        meta.shotWhitePoint = WhitePoint::D65();
    }

    return meta;
}

// ─────────────────────────────────────────────────────────────────────
// process
// ─────────────────────────────────────────────────────────────────────

ImageBuffer RawInputStage::process(const ImageBuffer& /* in */) {
    if (!m_impl->fileLoaded || m_impl->path.empty()) {
        std::cerr << "[RawInputStage] Warning: process() called before "
                     "a file was loaded — returning empty buffer.\n";
        return {};
    }

    // ── Open and Config LibRaw (Atomic sequence) ─────────────────────
    m_impl->libraw.recycle();
    
    const int openErr = m_impl->libraw.open_file(m_impl->path.c_str());
    if (openErr != LIBRAW_SUCCESS) {
        std::cerr << "[RawInputStage] open_file failed: " << libraw_strerror(openErr) << '\n';
        return {};
    }

    auto& params = m_impl->libraw.imgdata.params;
    params.output_bps     = 16;
    params.no_auto_bright = 1;
    params.use_auto_wb    = 0;
    params.use_camera_wb  = m_impl->params.useCameraWB ? 1 : 0;
    params.output_color   = 0;         // Raw colour space — no conversion.
    params.gamm[0]        = 1.0;       // Gamma numerator   → linear.
    params.gamm[1]        = 1.0;       // Gamma denominator  → linear.
    params.bright         = 1.0;
    params.highlight      = 0;         // No highlight recovery.
    params.half_size      = m_impl->params.halfSize ? 1 : 0;

    // ── Unpack ───────────────────────────────────────────────────────
    const int unpackErr = m_impl->libraw.unpack();
    if (unpackErr != LIBRAW_SUCCESS) {
        std::cerr << "[RawInputStage] unpack() failed: "
                  << libraw_strerror(unpackErr) << '\n';
        return {};
    }

    // ── DCRaw Process ────────────────────────────────────────────────
    const int processErr = m_impl->libraw.dcraw_process();
    if (processErr != LIBRAW_SUCCESS) {
        std::cerr << "[RawInputStage] dcraw_process() failed: "
                  << libraw_strerror(processErr) << '\n';
        return {};
    }

    // ── Obtain the decoded image ─────────────────────────────────────
    int memErr = 0;
    libraw_processed_image_t* img =
        m_impl->libraw.dcraw_make_mem_image(&memErr);
    if (img == nullptr || memErr != LIBRAW_SUCCESS) {
        std::cerr << "[RawInputStage] dcraw_make_mem_image() failed: "
                  << libraw_strerror(memErr) << '\n';
        return {};
    }

    // ── Verify channel count ─────────────────────────────────────────
    if (img->colors != 3) {
        std::cerr << "[RawInputStage] Unexpected channel count: "
                  << img->colors << " — expected 3 (RGB). Returning empty.\n";
        m_impl->libraw.dcraw_clear_mem(img);
        return {};
    }

    // ── Extract camera metadata ──────────────────────────────────────
    m_impl->metadata = extractMetadata();

    // ── Build the ImageBuffer ────────────────────────────────────────
    // Use the actual processed image dimensions (handles half-size correctly).
    const auto w = static_cast<uint32_t>(img->width);
    const auto h = static_cast<uint32_t>(img->height);

    auto result = ImageBuffer::create(w, h, ColorSpace::LINEAR_RAW);
    if (!result) {
        std::cerr << "[RawInputStage] ImageBuffer::create failed: "
                  << result.error().message << '\n';
        m_impl->libraw.dcraw_clear_mem(img);
        return {};
    }
    ImageBuffer& buf = *result;
    buf.whitePoint = WhitePoint::D65();
    buf.isLinear   = true;

    // img->data contains RGB interleaved 16-bit unsigned values.
    // Normalise to [0, 1] float.
    const auto* src = reinterpret_cast<const uint16_t*>(img->data);
    float*      dst = buf.ptr();

    constexpr float kNorm = 1.0F / 65535.0F;

    const size_t totalComponents = static_cast<size_t>(w) * h * 3;
    for (size_t i = 0; i < totalComponents; ++i) {
        dst[i] = static_cast<float>(src[i]) * kNorm;
    }

    // ── Clean up LibRaw allocated memory ─────────────────────────────
    m_impl->libraw.dcraw_clear_mem(img);

    return buf;
}

// ─────────────────────────────────────────────────────────────────────
// metadata accessor
// ─────────────────────────────────────────────────────────────────────

const CameraMetadata& RawInputStage::metadata() const noexcept {
    return m_impl->metadata;
}

} // namespace aether
