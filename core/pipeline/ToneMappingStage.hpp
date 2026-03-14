#pragma once

/// @file ToneMappingStage.hpp
/// @brief Pipeline stage for scene-to-display tone mapping with 1D LUT
///        acceleration.  Supports Linear, Filmic S-curve, and ACES operators.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <array>
#include <string_view>

namespace aether {

/// @brief Scene-to-display tone mapping with LUT-accelerated curves.
///
/// Operates in `PROPHOTO_LINEAR`.  After tone-mapping each channel
/// independently (to preserve hue), an optional saturation adjustment
/// is applied.
///
/// A 4096-entry 1D LUT is rebuilt whenever parameters change, covering
/// `[0, whiteClip]` → `[0, 1]`.  `process()` simply looks up + lerps
/// into the LUT instead of evaluating the curve math per pixel.
class ToneMappingStage final : public IPipelineStage {
public:
    ToneMappingStage();
    ~ToneMappingStage() override = default;

    ToneMappingStage(const ToneMappingStage&)            = delete;
    ToneMappingStage& operator=(const ToneMappingStage&) = delete;
    ToneMappingStage(ToneMappingStage&&)                 = delete;
    ToneMappingStage& operator=(ToneMappingStage&&)      = delete;

    // ── IPipelineStage ───────────────────────────────────────────────

    /// @brief Apply tone mapping to the input buffer.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept `ToneMappingParams`; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Retrieve current ToneMappingParams.
    [[nodiscard]] StageParams getParams() const override;

    /// @brief Returns `StageId::ToneMapping`.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — CPU-only for now.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable description.
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    static constexpr size_t kLUTSize = 4096;

    /// Rebuild the 1D LUT from current parameters.
    void buildLUT1D();

    /// Evaluate the LUT with linear interpolation.
    [[nodiscard]] float applyLUT(float v) const noexcept;

    ImageBuffer processCPU(const ImageBuffer& in);
    ImageBuffer processGPU(const ImageBuffer& in);

    ToneMappingParams                    m_params;
    std::array<float, kLUTSize>          m_lut{};
    float                                m_lutScale{1.0F}; ///< kLUTSize / whiteClip
    bool                                 m_lutDirty{true};
};

} // namespace aether
