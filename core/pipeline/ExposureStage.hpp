#pragma once

/// @file ExposureStage.hpp
/// @brief Pipeline stage for linear exposure compensation, black point,
///        shadow lift, and soft-knee highlight recovery.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <array>
#include <string_view>

namespace aether {

/// @brief Linear exposure compensation with highlight recovery and shadow lift.
///
/// Operates in `PROPHOTO_LINEAR` colour space.  Processing order:
/// 1. Black point subtraction + renormalisation
/// 2. Linear gain  (2^EV)
/// 3. Soft-knee highlight compression (tanh-based, LUT-accelerated)
/// 4. Shadow lift
class ExposureStage final : public IPipelineStage {
public:
    ExposureStage();
    ~ExposureStage() override = default;

    ExposureStage(const ExposureStage&)            = delete;
    ExposureStage& operator=(const ExposureStage&) = delete;
    ExposureStage(ExposureStage&&)                 = delete;
    ExposureStage& operator=(ExposureStage&&)      = delete;

    // ── IPipelineStage ───────────────────────────────────────────────

    /// @brief Apply exposure compensation to the input buffer.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept `ExposureParams`; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Returns `StageId::Exposure`.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — CPU-only for now.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable description.
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    /// Rebuild the tanh LUT for highlight recovery.
    void buildLUT();

    ExposureParams                m_params;
    std::array<float, 256>        m_tanhLUT{};  ///< LUT covering [0, 3.0].
    bool                          m_lutDirty{true};
};

} // namespace aether
