#pragma once

/// @file PipelineEngine.hpp
/// @brief Declaration of the PipelineEngine — the orchestrator that chains
///        pipeline stages, manages caching, and services render requests.
/// @details This header is declaration-only; the implementation will live in
///          a corresponding `.cpp` translation unit.

#include "IPipelineStage.hpp"
#include "Types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <unordered_map>

namespace aether {

/// @brief Orchestrates the full RAW → display pipeline.
///
/// Owns an ordered array of `IPipelineStage` pointers, a per-stage
/// cache of intermediate `ImageBuffer`s, and dirty flags that enable
/// partial re-computation when a user adjusts a single parameter.
///
/// All public render methods return `std::future` so that the UI
/// thread is never blocked.
class PipelineEngine {
public:
    PipelineEngine();
    ~PipelineEngine();

    // Prevent accidental copies; moves also disabled due to mutex.
    PipelineEngine(const PipelineEngine&)            = delete;
    PipelineEngine& operator=(const PipelineEngine&) = delete;
    PipelineEngine(PipelineEngine&&)                 = delete;
    PipelineEngine& operator=(PipelineEngine&&)      = delete;

    // ── Loading ──────────────────────────────────────────────────────

    /// @brief Decode a RAW file and populate the initial pipeline buffer.
    /// @param path  Filesystem path to a supported raw image.
    void loadRaw(std::filesystem::path path);

    // ── Parameter updates ────────────────────────────────────────────

    /// @brief Push new parameters to a specific pipeline stage.
    ///
    /// This marks the target stage (and all subsequent stages) as dirty
    /// so that the next render request re-processes only what changed.
    ///
    /// @param stage   Stage to update.
    /// @param params  New parameters for that stage.
    void updateParam(StageId stage, StageParams params);

    // ── Rendering ────────────────────────────────────────────────────

    /// @brief Request a scaled-down preview render.
    ///
    /// The engine runs the full pipeline at reduced resolution.
    /// Cancellation is supported via the provided `std::stop_token`.
    ///
    /// @param w       Target preview width.
    /// @param h       Target preview height.
    /// @param cancel  Cooperative cancellation token.
    /// @return A future that will hold the preview `ImageBuffer`.
    [[nodiscard]] std::future<ImageBuffer>
    requestPreview(uint32_t w, uint32_t h, std::stop_token cancel);

    /// @brief Request a full-resolution render.
    /// @param cancel  Cooperative cancellation token.
    /// @return A future that will hold the final `ImageBuffer`.
    [[nodiscard]] std::future<ImageBuffer>
    requestFullRender(std::stop_token cancel);

    // ── Accessors ────────────────────────────────────────────────────

    /// @brief Access the metadata extracted from the loaded RAW file.
    /// @pre `loadRaw()` has been called successfully.
    [[nodiscard]] const CameraMetadata& metadata() const noexcept;

    /// @brief Check whether a raw file has been loaded and the pipeline
    ///        is ready to accept render requests.
    [[nodiscard]] bool isReady() const noexcept;

private:
    // ── Internal helpers ─────────────────────────────────────────────

    /// @brief Mark @p from and every later stage as dirty.
    void markDirtyFrom(StageId from);

    /// @brief Execute the pipeline, using cached results where possible.
    /// @param cancel  Cooperative cancellation token.
    /// @return The final output `ImageBuffer`.
    ImageBuffer runPipeline(std::stop_token cancel);

    // ── Data members ─────────────────────────────────────────────────

    /// Ordered array of pipeline stages (one per StageId).
    std::array<std::unique_ptr<IPipelineStage>, kStageCount> m_stages{};

    /// Per-stage cache of intermediate `ImageBuffer` results.
    /// Key: `static_cast<uint8_t>(StageId)`.
    std::unordered_map<uint8_t, ImageBuffer> m_cache;

    /// Per-stage dirty flags.
    /// Key: `static_cast<uint8_t>(StageId)`.
    std::unordered_map<uint8_t, bool> m_dirty;

    /// Metadata extracted from the loaded RAW file.
    CameraMetadata m_metadata{};

    /// Indicates that a RAW file has been loaded successfully.
    bool m_ready{false};

    /// Protects stages, cache, and dirty state from concurrent access.
    mutable std::recursive_mutex m_renderMutex;
};

} // namespace aether
