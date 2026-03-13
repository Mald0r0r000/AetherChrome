#pragma once
#include "../ImageBuffer.hpp"
#include <filesystem>
#include <future>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

namespace aether {

/**
 * @class SAMEncoder
 * @brief Runs the SAM 2 image encoder (Hiera-Tiny backbone).
 * 
 * This class abstracts the ONNX Runtime session for the image encoder.
 * It uses PIMPL to keep ONNX headers out of the public interface.
 */
class SAMEncoder {
public:
    /**
     * @brief Constructs the encoder.
     * @param encoderOnnxPath Path to the encoder.onnx file.
     */
    explicit SAMEncoder(std::filesystem::path encoderOnnxPath);
    ~SAMEncoder();

    SAMEncoder(const SAMEncoder&)            = delete;
    SAMEncoder& operator=(const SAMEncoder&) = delete;

    /**
     * @brief Encodes an image asynchronously.
     * 
     * Resizes the image to 1024x1024, normalizes it, and runs inference.
     * Results are cached based on a fingerprint of the input image.
     * 
     * @param image Input ImageBuffer (scene-linear or display-space).
     * @return A future that resolves when encoding is complete.
     */
    std::future<void> encodeAsync(const ImageBuffer& image);

    /**
     * @brief Checks if the encoder has processed at least one image and has a valid embedding.
     */
    [[nodiscard]] bool isReady() const noexcept;

    /**
     * @brief Returns the raw embedding data [1, 256, 64, 64].
     */
    [[nodiscard]] const std::vector<float>& embedding() const noexcept;

    /**
     * @brief Returns the shape of the embedding tensor.
     */
    [[nodiscard]] std::array<int64_t, 4> embeddingShape() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aether
