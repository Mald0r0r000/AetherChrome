#pragma once

/// @file ImageBuffer.hpp
/// @brief Owner of a flat, interleaved RGB float buffer with colour-space
///        tracking and lightweight image operations.
/// @details Pure C++20 — no external dependencies.

#include "Types.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// Error type for factory / operation failures
// ─────────────────────────────────────────────────────────────────────

/// @brief Lightweight error descriptor returned in `std::expected` paths.
struct ImageError {
    std::string message;  ///< Human-readable description.
};

// ─────────────────────────────────────────────────────────────────────
// ImageBuffer
// ─────────────────────────────────────────────────────────────────────

/// @brief Flat, interleaved RGB 32-bit float image buffer.
///
/// Data layout: `[R0 G0 B0  R1 G1 B1  …]` — three floats per pixel,
/// row-major, no padding.  Values are in linear-light unless `isLinear`
/// is explicitly cleared after an OETF has been applied.
struct ImageBuffer {

    std::vector<float> data;             ///< RGB interleaved pixel data.
    uint32_t           width{};          ///< Image width in pixels.
    uint32_t           height{};         ///< Image height in pixels.
    ColorSpace         colorSpace{ColorSpace::LINEAR_RAW};
    WhitePoint         whitePoint{WhitePoint::D65()};
    bool               isLinear{true};   ///< True while data is scene-linear.

    // ── Queries ──────────────────────────────────────────────────────

    /// @brief Total number of pixels (width × height).
    [[nodiscard]] constexpr size_t pixelCount() const noexcept {
        return static_cast<size_t>(width) * height;
    }

    /// @brief Number of channels per pixel (always 3 — RGB).
    [[nodiscard]] static constexpr size_t channelCount() noexcept {
        return 3;
    }

    // ── Raw data access (for SIMD / compute kernels) ─────────────────

    /// @brief Mutable pointer to the first float in the buffer.
    [[nodiscard]] float* ptr() noexcept {
        return data.data();
    }

    /// @brief Const pointer to the first float in the buffer.
    [[nodiscard]] const float* ptr() const noexcept {
        return data.data();
    }

    /// @brief Mutable span over an entire row of interleaved RGB data.
    /// @param y  Row index (0-based, top-to-bottom).
    [[nodiscard]] std::span<float> row(uint32_t y) noexcept {
        assert(y < height);
        const size_t offset = static_cast<size_t>(y) * width * channelCount();
        return {data.data() + offset, static_cast<size_t>(width) * channelCount()};
    }

    /// @brief Const span over an entire row.
    /// @param y  Row index (0-based, top-to-bottom).
    [[nodiscard]] std::span<const float> row(uint32_t y) const noexcept {
        assert(y < height);
        const size_t offset = static_cast<size_t>(y) * width * channelCount();
        return {data.data() + offset, static_cast<size_t>(width) * channelCount()};
    }

    // ── Factory methods ──────────────────────────────────────────────

    /// @brief Create a zero-initialized buffer of the given dimensions.
    /// @param w   Width  in pixels (must be > 0).
    /// @param h   Height in pixels (must be > 0).
    /// @param cs  Target colour space for the buffer.
    /// @return    An `ImageBuffer` on success, or an `ImageError`.
    [[nodiscard]] static std::expected<ImageBuffer, ImageError>
    create(uint32_t w, uint32_t h, ColorSpace cs) noexcept {
        if (w == 0 || h == 0) {
            return std::unexpected(ImageError{"Width and height must be > 0."});
        }

        const size_t totalFloats =
            static_cast<size_t>(w) * h * channelCount();

        ImageBuffer buf;
        buf.width      = w;
        buf.height     = h;
        buf.colorSpace = cs;
        buf.isLinear   = true;

        // Assign a sensible default white point per colour space.
        switch (cs) {
            case ColorSpace::PROPHOTO_LINEAR:
                buf.whitePoint = WhitePoint::D50();
                break;
            case ColorSpace::REC2020_LINEAR:
            case ColorSpace::DISPLAY_SRGB:
            case ColorSpace::DISPLAY_P3:
                buf.whitePoint = WhitePoint::D65();
                break;
            case ColorSpace::LINEAR_RAW:
            default:
                buf.whitePoint = WhitePoint::D65();
                break;
        }

        try {
            buf.data.resize(totalFloats, 0.0F);
        } catch (...) {
            return std::unexpected(
                ImageError{"Allocation failed for " +
                           std::to_string(totalFloats * sizeof(float)) +
                           " bytes."});
        }

        return buf;
    }

    // ── Operations ───────────────────────────────────────────────────

    /// @brief Bilinear-downscale the image to the requested dimensions.
    ///
    /// Intended for lightweight preview generation — not for
    /// production-quality resampling (no proper low-pass pre-filter).
    ///
    /// @param targetW  Desired width  (must be > 0, ≤ current width).
    /// @param targetH  Desired height (must be > 0, ≤ current height).
    /// @return A downscaled `ImageBuffer`, or an `ImageError`.
    [[nodiscard]] std::expected<ImageBuffer, ImageError>
    downscale(uint32_t targetW, uint32_t targetH) const noexcept {
        if (targetW == 0 || targetH == 0) {
            return std::unexpected(ImageError{"Target dimensions must be > 0."});
        }
        if (targetW > width || targetH > height) {
            return std::unexpected(
                ImageError{"Upscaling is not supported by downscale()."});
        }

        auto result = create(targetW, targetH, colorSpace);
        if (!result) {
            return result;
        }
        ImageBuffer& dst = *result;
        dst.whitePoint = whitePoint;
        dst.isLinear   = isLinear;

        const float xRatio = static_cast<float>(width)  / static_cast<float>(targetW);
        const float yRatio = static_cast<float>(height) / static_cast<float>(targetH);

        for (uint32_t dy = 0; dy < targetH; ++dy) {
            const float srcY  = (static_cast<float>(dy) + 0.5F) * yRatio - 0.5F;
            const auto  sy0   = static_cast<uint32_t>(std::max(0.0F, srcY));
            const uint32_t sy1 = std::min(sy0 + 1, height - 1);
            const float fy    = srcY - static_cast<float>(sy0);

            for (uint32_t dx = 0; dx < targetW; ++dx) {
                const float srcX  = (static_cast<float>(dx) + 0.5F) * xRatio - 0.5F;
                const auto  sx0   = static_cast<uint32_t>(std::max(0.0F, srcX));
                const uint32_t sx1 = std::min(sx0 + 1, width - 1);
                const float fx    = srcX - static_cast<float>(sx0);

                for (size_t c = 0; c < channelCount(); ++c) {
                    // Fetch four neighbours.
                    const float p00 = data[(static_cast<size_t>(sy0) * width + sx0) * channelCount() + c];
                    const float p10 = data[(static_cast<size_t>(sy0) * width + sx1) * channelCount() + c];
                    const float p01 = data[(static_cast<size_t>(sy1) * width + sx0) * channelCount() + c];
                    const float p11 = data[(static_cast<size_t>(sy1) * width + sx1) * channelCount() + c];

                    // Bilinear interpolation.
                    const float top    = p00 + (p10 - p00) * fx;
                    const float bottom = p01 + (p11 - p01) * fx;
                    const float value  = top  + (bottom - top) * fy;

                    dst.data[(static_cast<size_t>(dy) * targetW + dx) * channelCount() + c] = value;
                }
            }
        }

        return dst;
    }
};

} // namespace aether
