#pragma once

/// @file OutputTransformStage.hpp
/// @brief Pipeline stage that converts PROPHOTO_LINEAR (D50) to a display
///        colour space (sRGB or Display P3) via LittleCMS 2.
/// @details LittleCMS is hidden behind a PIMPL to keep the header clean.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <memory>
#include <string_view>

namespace aether {

/// @brief Converts ProPhoto RGB (linear, D50) → sRGB or Display P3.
///
/// Uses LittleCMS 2 to build an ICC colour transform from a virtual
/// ProPhoto source profile to a standard display destination profile.
/// The final buffer is clamped to [0, 1] using ARM NEON.
class OutputTransformStage final : public IPipelineStage {
public:
    OutputTransformStage();
    ~OutputTransformStage() override;

    OutputTransformStage(const OutputTransformStage&)            = delete;
    OutputTransformStage& operator=(const OutputTransformStage&) = delete;
    OutputTransformStage(OutputTransformStage&&)                 = delete;
    OutputTransformStage& operator=(OutputTransformStage&&)      = delete;

    // ── IPipelineStage ───────────────────────────────────────────────

    /// @brief Apply the LittleCMS colour transform + clamp.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept `OutputTransformParams`; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Retrieve current OutputTransformParams.
    [[nodiscard]] StageParams getParams() const override;

    /// @brief Returns `StageId::OutputTransform`.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — CPU-only LittleCMS path.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable description.
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ImageBuffer processCPU(const ImageBuffer& in);
    ImageBuffer processGPU(const ImageBuffer& in);
};

} // namespace aether
