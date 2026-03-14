#pragma once

/// @file MaskCompositeStage.hpp
/// @brief Pipeline stage that applies parametric masks with local adjustments
///        (exposure, saturation, contrast) to the working-space image.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <string_view>

namespace aether {

/// @brief Parametric mask composite with local adjustments.
///
/// Generates up to 8 parametric masks (luminance + HSL criteria),
/// merges them, and applies per-mask exposure/saturation/contrast
/// adjustments blended by the combined alpha.
///
/// Operates in `PROPHOTO_LINEAR`.  Pass-through when `layerCount == 0`.
class MaskCompositeStage final : public IPipelineStage {
public:
    MaskCompositeStage()  = default;
    ~MaskCompositeStage() override = default;

    MaskCompositeStage(const MaskCompositeStage&)            = delete;
    MaskCompositeStage& operator=(const MaskCompositeStage&) = delete;
    MaskCompositeStage(MaskCompositeStage&&)                 = delete;
    MaskCompositeStage& operator=(MaskCompositeStage&&)      = delete;

    // ── IPipelineStage ───────────────────────────────────────────────

    /// @brief Apply masked local adjustments to the input buffer.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept `MaskCompositeParams`; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Retrieve current MaskCompositeParams.
    [[nodiscard]] StageParams getParams() const override;

    /// @brief Returns `StageId::MaskComposite`.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — CPU-only.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable description.
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    MaskCompositeParams m_params;
};

} // namespace aether
