#pragma once

/// @file ParametricMaskBuilder.hpp
/// @brief Utility class that generates alpha masks from PROPHOTO_LINEAR
///        image data based on luminance, hue, and saturation parameters.

#include "../ImageBuffer.hpp"
#include "../Types.hpp"

namespace aether {

/// @brief Builds pixel-level alpha masks from parametric criteria.
///
/// Generates a `MaskLayer` by evaluating each pixel of the input image
/// against luminance, hue, and saturation ranges with smooth feathering.
/// The luminance pass is NEON-accelerated; HSL evaluation is scalar.
class ParametricMaskBuilder {
public:
    /// @brief Generate a mask from an image and parametric criteria.
    /// @param image  Source image in PROPHOTO_LINEAR.
    /// @param p      Parametric mask parameters.
    /// @return A MaskLayer with per-pixel alpha values.
    [[nodiscard]] static MaskLayer build(const ImageBuffer& image,
                                          const ParametricMaskParams& p);

private:
    /// @brief Convert linear RGB to HSL.
    /// @param[out] h  Hue in [0, 360].
    /// @param[out] s  Saturation in [0, 1].
    /// @param[out] l  Lightness in [0, 1].
    static void rgbToHSL(float r, float g, float b,
                         float& h, float& s, float& l) noexcept;

    /// @brief Smooth trapezoidal ramp with feathered edges.
    [[nodiscard]] static float smoothStep(float v, float low, float high,
                                           float feather) noexcept;

    /// @brief Circular hue distance in degrees (handles 360→0 wrap).
    [[nodiscard]] static float hueDistance(float h, float center) noexcept;
};

} // namespace aether
