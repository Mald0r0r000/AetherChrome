#pragma once

/// @file ColorMatrixStage.hpp
/// @brief Pipeline stage that transforms a LINEAR_RAW buffer (camera
///        native RGB) into PROPHOTO_LINEAR (large-gamut working space, D50).
/// @details Uses a composed Camera→XYZ→ProPhoto matrix with ARM NEON
///          SIMD acceleration.  LittleCMS is reserved for later stages;
///          this header does not expose it.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <memory>
#include <string_view>

namespace aether {

/// @brief Transforms camera-native linear RGB into ProPhoto RGB (linear, D50).
///
/// The stage interpolates two camera-to-XYZ matrices (D50 & D65)
/// according to `ColorMatrixParams::illuminantBlend`, composes the
/// result with the fixed XYZ→ProPhoto matrix, and applies the final
/// 3×3 transform per pixel using ARM NEON intrinsics with a scalar
/// fallback.
class ColorMatrixStage final : public IPipelineStage {
public:
    ColorMatrixStage();
    ~ColorMatrixStage() override;

    // Non-copyable, non-movable.
    ColorMatrixStage(const ColorMatrixStage&)            = delete;
    ColorMatrixStage& operator=(const ColorMatrixStage&) = delete;
    ColorMatrixStage(ColorMatrixStage&&)                 = delete;
    ColorMatrixStage& operator=(ColorMatrixStage&&)      = delete;

    // ── IPipelineStage interface ─────────────────────────────────────

    /// @brief Apply the camera→ProPhoto colour matrix to every pixel.
    /// @param in  Input buffer in `ColorSpace::LINEAR_RAW`.
    /// @return    A new buffer in `ColorSpace::PROPHOTO_LINEAR`, D50.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept `ColorMatrixParams`; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Retrieve the current parameter block of this stage.
    [[nodiscard]] StageParams getParams() const override;

    /// @brief Returns `StageId::ColorMatrix`.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — CPU-only matrix multiply for now.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable stage description.
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ImageBuffer processCPU(const ImageBuffer& in);
    ImageBuffer processGPU(const ImageBuffer& in);
};

} // namespace aether
