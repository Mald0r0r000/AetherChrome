#pragma once

/// @file Types.hpp
/// @brief Foundational types for the AetherChrome image processing engine.
/// @details Pure C++20, no external dependencies. Defines color spaces,
///          pipeline stage identifiers, white points, camera metadata,
///          and stage parameters.

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// ColorSpace
// ─────────────────────────────────────────────────────────────────────

/// @brief Identifies the colour space of an ImageBuffer.
///
/// Linear variants store scene-referred data before any display transform.
/// Display variants store output-referred data in the appropriate transfer
/// function / gamut.
enum class ColorSpace : uint8_t {
    LINEAR_RAW,         ///< Camera-native primaries, linear light.
    PROPHOTO_LINEAR,    ///< ProPhoto RGB (ROMM), linear light, D50 white.
    REC2020_LINEAR,     ///< ITU-R BT.2020, linear light, D65 white.
    DISPLAY_SRGB,       ///< sRGB / Rec.709 gamut with sRGB OETF, D65 white.
    DISPLAY_P3           ///< Display P3 gamut with sRGB OETF, D65 white.
};

// ─────────────────────────────────────────────────────────────────────
// StageId
// ─────────────────────────────────────────────────────────────────────

/// @brief Enumerates every stage in the processing pipeline, in order.
///
/// The numeric values reflect execution order and are used as keys
/// in the dirty-tracking and cache maps.
enum class StageId : uint8_t {
    RawIngest       = 0, ///< Decode the raw file into a Bayer buffer.
    Demosaic        = 1, ///< Interpolate the Bayer mosaic to full RGB.
    ColorMatrix     = 2, ///< Apply camera → working-space colour matrix.
    Exposure        = 3, ///< Linear exposure compensation (EV stops).
    ToneMapping     = 4, ///< Scene-to-display tone curve.
    MaskComposite   = 5, ///< Blend parametric / painted masks.
    OutputTransform = 6  ///< Working-space → display colour space + OETF.
};

/// Total number of pipeline stages — keep in sync with StageId.
inline constexpr std::size_t kStageCount = 7;

// ─────────────────────────────────────────────────────────────────────
// WhitePoint
// ─────────────────────────────────────────────────────────────────────

/// @brief CIE 1931 xy chromaticity coordinates of a white point.
struct WhitePoint {
    float x{};  ///< CIE x chromaticity coordinate.
    float y{};  ///< CIE y chromaticity coordinate.

    /// @brief Standard illuminant D50 (printing, ProPhoto reference).
    [[nodiscard]] static constexpr WhitePoint D50() noexcept {
        return {.x = 0.3457F, .y = 0.3585F};
    }

    /// @brief Standard illuminant D55 (daylight compromise).
    [[nodiscard]] static constexpr WhitePoint D55() noexcept {
        return {.x = 0.3324F, .y = 0.3474F};
    }

    /// @brief Standard illuminant D65 (sRGB / Rec.709 / Rec.2020 reference).
    [[nodiscard]] static constexpr WhitePoint D65() noexcept {
        return {.x = 0.3127F, .y = 0.3290F};
    }

    /// @brief Equality comparison.
    [[nodiscard]] constexpr bool operator==(const WhitePoint&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────
// CameraMetadata
// ─────────────────────────────────────────────────────────────────────

/// @brief EXIF / DNG-sourced metadata attached to a raw file.
///
/// The two colour matrices come from DNG tags `ColorMatrix1` (illum A / D50)
/// and `ColorMatrix2` (illum D65).  They map *camera-native* RGB into XYZ.
struct CameraMetadata {
    std::string                cameraMake;      ///< Camera manufacturer.
    std::string                cameraModel;     ///< Camera model name.
    float                      isoValue{};      ///< ISO sensitivity.
    float                      shutterSpeed{};  ///< Shutter speed in seconds.
    float                      aperture{};      ///< Aperture f-number.
    std::string                lensModel;       ///< Lens identification string.
    WhitePoint                 shotWhitePoint;  ///< As-shot white balance.
    std::array<float, 9>       colorMatrix1{};  ///< Camera RGB → XYZ D50 (row-major 3×3).
    std::array<float, 9>       colorMatrix2{};  ///< Camera RGB → XYZ D65 (row-major 3×3).
};

// ─────────────────────────────────────────────────────────────────────
// StageParams
// ─────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────
// Per-stage parameter structs
// ─────────────────────────────────────────────────────────────────────

/// @brief Parameters for the RawIngest stage.
struct RawIngestParams {
    bool halfSize    = false;  ///< True → 2×2 binning for fast preview.
    bool useCameraWB = true;   ///< True → use as-shot white balance.
};

/// @brief Parameters for the ColorMatrix stage.
///
/// Controls illuminant blending and the camera-to-working-space
/// colour matrix transformation.
struct ColorMatrixParams {
    /// Illuminant blend factor [0, 1]: 0 = D50 (tungsten), 1 = D65 (daylight).
    float illuminantBlend = 1.0F;

    /// Camera RGB → XYZ D50 (row-major 3×3, from CameraMetadata::colorMatrix1).
    std::array<float, 9> cameraToXYZ_D50{};

    /// Camera RGB → XYZ D65 (row-major 3×3, from CameraMetadata::colorMatrix2).
    std::array<float, 9> cameraToXYZ_D65{};

    /// Target working colour space.
    ColorSpace targetSpace = ColorSpace::PROPHOTO_LINEAR;
};

/// @brief Parameters for the Exposure stage.
struct ExposureParams {
    float exposureEV    =  0.0F;  ///< EV compensation [-5, +5].
    float highlightComp =  0.0F;  ///< Soft-knee highlight recovery [0, 1].
    float shadowLift    =  0.0F;  ///< Shadow lift [0, 0.5].
    float blackPoint    =  0.0F;  ///< Black point offset [0, 0.1].
};

/// @brief Parameters for the ToneMapping stage.
struct ToneMappingParams {
    /// Tone-mapping operator selection.
    enum class Operator : uint8_t {
        Linear  = 0,  ///< No tone mapping — pass-through.
        FilmicS = 1,  ///< Filmic S-curve (Hable / Uncharted 2).
        ACES    = 2,  ///< ACES RRT approximation (Stephen Hill).
    };

    Operator op         = Operator::FilmicS;
    float    contrast   = 1.0F;   ///< Curve contrast multiplier [0.5, 2.0].
    float    saturation = 1.0F;   ///< Output saturation [0, 2].
    float    whiteClip  = 1.0F;   ///< White clip point (scene linear).

    /// Bézier control points for a future custom curve (5 points in [0,1]²).
    std::array<std::array<float, 2>, 5> curvePoints = {{
        {0.0F, 0.0F}, {0.25F, 0.25F}, {0.5F, 0.5F},
        {0.75F, 0.75F}, {1.0F, 1.0F}
    }};
};

/// @brief Parameters for the OutputTransform stage.
struct OutputTransformParams {
    /// Target display colour space.
    ColorSpace displaySpace = ColorSpace::DISPLAY_SRGB;
    /// Apply Bradford chromatic adaptation D50→D65 before display conversion.
    bool applyCAT = true;
    /// Peak luminance in cd/m² (SDR = 100, HDR reserved for future use).
    float peakLuminance = 100.0F;
};

// ─────────────────────────────────────────────────────────────────────
// Mask types
// ─────────────────────────────────────────────────────────────────────

/// @brief Blending mode for mask layers.
enum class BlendMode : uint8_t {
    Normal     = 0,  ///< Alpha composite standard.
    Multiply   = 1,
    Screen     = 2,
    Luminosity = 3,
    Color      = 4
};

/// @brief A single pixel-level mask layer.
struct MaskLayer {
    std::vector<float> alpha;       ///< [0,1] per pixel, same resolution.
    uint32_t           width{};     ///< Mask width.
    uint32_t           height{};    ///< Mask height.
    std::string        label;       ///< Human label ("Skin", "Sky", etc.).
    float              opacity  = 1.0F;
    BlendMode          blend    = BlendMode::Normal;
    bool               inverted = false;
};

/// @brief Parameters for a single parametric mask.
struct ParametricMaskParams {
    // Luminance range [0,1].
    float lumaMin     = 0.0F;
    float lumaMax     = 1.0F;
    float lumaFeather = 0.1F;   ///< Falloff at range edges.

    // HSL hue targeting.
    float hueCenter   = 0.0F;   ///< Degrees [0, 360].
    float hueWidth    = 30.0F;  ///< Width of the hue range in degrees.
    float hueFeather  = 10.0F;

    // Saturation range [0,1].
    float satMin      = 0.0F;
    float satMax      = 1.0F;
    float satFeather  = 0.05F;

    /// If true, luminance AND hue AND saturation (intersect).
    /// If false, luminance OR (hue AND saturation).
    bool  intersect   = true;
};

/// @brief Parameters for the MaskComposite stage.
struct MaskCompositeParams {
    /// Up to 8 stacked parametric mask layers.
    std::array<ParametricMaskParams, 8> layers;
    std::array<bool, 8>                 layerEnabled{false, false, false, false, false, false, false, false};
    std::array<BlendMode, 8>            layerBlend{};    ///< New: per-layer blend mode.
    std::array<bool, 8>                 layerInverted{false, false, false, false, false, false, false, false};
    uint8_t                             layerCount = 0;

    /// @brief Optional AI-generated pixel mask.
    MaskLayer aiMask;
    bool      hasAiMask = false;

    /// Local adjustments applied inside the combined masks.
    float exposureInMask   = 0.0F;
    float saturationInMask = 1.0F;
    float contrastInMask   = 1.0F;
};


// ─────────────────────────────────────────────────────────────────────
// StageParams variant
// ─────────────────────────────────────────────────────────────────────

/// @brief Polymorphic parameter block for any pipeline stage.
///
/// Each concrete stage checks for its own parameter struct via
/// `std::get_if` and silently ignores unrelated types.
using StageParams = std::variant<
    std::monostate,
    RawIngestParams,
    ColorMatrixParams,
    ExposureParams,
    ToneMappingParams,
    OutputTransformParams,
    MaskCompositeParams
>;

} // namespace aether
