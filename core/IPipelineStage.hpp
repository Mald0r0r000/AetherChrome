#pragma once

/// @file IPipelineStage.hpp
/// @brief Abstract interface for a single pipeline processing stage.
/// @details Every concrete stage (Demosaic, Exposure, ToneMapping …)
///          implements this interface.  The PipelineEngine owns an
///          ordered array of `std::unique_ptr<IPipelineStage>`.

#include "ImageBuffer.hpp"
#include "Types.hpp"

#include <string_view>

namespace aether {

/// @brief Abstract base class for a pipeline processing stage.
///
/// Stages are executed in the order defined by `StageId`.
/// Each stage receives an immutable reference to the previous stage's
/// output and returns a new `ImageBuffer` containing its result.
class IPipelineStage {
public:
    /// @brief Virtual destructor (rule of five — polymorphic base).
    virtual ~IPipelineStage() = default;

    // ── Core operations ──────────────────────────────────────────────

    /// @brief Execute this stage's processing on the input buffer.
    /// @param in  Immutable reference to the preceding stage's output.
    /// @return    A new `ImageBuffer` containing the processed result.
    virtual ImageBuffer process(const ImageBuffer& in) = 0;

    /// @brief Replace this stage's parameter block.
    /// @param p  Serialised parameter variant.
    virtual void setParams(const StageParams& p) = 0;

    // ── Introspection ────────────────────────────────────────────────

    /// @brief Return the unique stage identifier.
    [[nodiscard]] virtual StageId id() const noexcept = 0;

    /// @brief Indicate whether this stage has a GPU (Metal / Vulkan) path.
    [[nodiscard]] virtual bool supportsGPU() const noexcept { return false; }

    /// @brief Short, human-readable description of this stage.
    [[nodiscard]] virtual std::string_view description() const noexcept = 0;
};

} // namespace aether
