#pragma once

/// @file RawInputStage.hpp
/// @brief First concrete pipeline stage — decodes a RAW file via LibRaw
///        into a linear-light ImageBuffer.
/// @details LibRaw is used only in the .cpp translation unit; this header
///          deliberately avoids including <libraw/libraw.h> to keep
///          compile times low and the dependency contained.

#include "../ImageBuffer.hpp"
#include "../IPipelineStage.hpp"
#include "../Types.hpp"

#include <expected>
#include <filesystem>
#include <string_view>

namespace aether {

/// @brief Decodes a camera RAW file into a linear-light RGB ImageBuffer.
///
/// This stage wraps LibRaw 0.21+ and configures it for **pure linear
/// output** — no auto-brightness, no built-in tone curve, no colour-space
/// conversion.  The resulting `ImageBuffer` is in `ColorSpace::LINEAR_RAW`
/// with values normalised to [0, 1].
///
/// Typical usage:
/// @code
///   RawInputStage stage;
///   if (auto err = stage.loadFile("/path/to/photo.ARW"); !err) {
///       // handle err.error()
///   }
///   ImageBuffer linear = stage.process(ImageBuffer{} /* ignored */);
/// @endcode
class RawInputStage final : public IPipelineStage {
public:
    RawInputStage();
    ~RawInputStage() override;

    // Non-copyable, non-movable (LibRaw internal state is not movable).
    RawInputStage(const RawInputStage&)            = delete;
    RawInputStage& operator=(const RawInputStage&) = delete;
    RawInputStage(RawInputStage&&)                 = delete;
    RawInputStage& operator=(RawInputStage&&)      = delete;

    // ── IPipelineStage interface ─────────────────────────────────────

    /// @brief Execute the RAW decode and return a linear ImageBuffer.
    /// @param in  Ignored — this stage generates its own data.
    /// @return    A linear-light ImageBuffer, or an empty buffer on error.
    ImageBuffer process(const ImageBuffer& in) override;

    /// @brief Accept RawIngestParams; silently ignores other types.
    void setParams(const StageParams& p) override;

    /// @brief Returns StageId::RawIngest.
    [[nodiscard]] StageId id() const noexcept override;

    /// @brief Always false — RAW decoding is CPU-only.
    [[nodiscard]] bool supportsGPU() const noexcept override;

    /// @brief Human-readable stage description.
    [[nodiscard]] std::string_view description() const noexcept override;

    // ── RawInputStage-specific API ───────────────────────────────────

    /// @brief Open and prepare a RAW file for decoding.
    /// @param path  Filesystem path to a supported camera RAW file.
    /// @return void on success, or an ImageError describing the failure.
    [[nodiscard]] std::expected<void, ImageError>
    loadFile(std::filesystem::path path);

    /// @brief Access the camera metadata extracted during process().
    /// @pre process() has been called at least once after a successful loadFile().
    [[nodiscard]] const CameraMetadata& metadata() const noexcept;

private:
    /// @brief Extract CameraMetadata from the loaded LibRaw state.
    [[nodiscard]] CameraMetadata extractMetadata() const;

    // ── Data members ─────────────────────────────────────────────────
    // The PIMPL-like forward-declared Impl hides LibRaw from this header.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aether
