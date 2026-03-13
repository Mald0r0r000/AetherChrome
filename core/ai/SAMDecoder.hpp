#pragma once
#include "../../core/Types.hpp"
#include "../../core/ImageBuffer.hpp"
#include <filesystem>
#include <memory>
#include <vector>
#include <expected>
#include <string>

namespace aether {

/**
 * @class SAMDecoder
 * @brief Runs the SAM 2 decoder for prompt-based segmentation.
 */
class SAMDecoder {
public:
    explicit SAMDecoder(std::filesystem::path decoderOnnxPath);
    ~SAMDecoder();

    SAMDecoder(const SAMDecoder&)            = delete;
    SAMDecoder& operator=(const SAMDecoder&) = delete;

    /**
     * @brief A prompt point for SAM.
     */
    struct PromptPoint {
        float x, y;      ///< Normalized coordinates [0, 1].
        bool  positive;  ///< true = foreground, false = background.
    };

    /**
     * @brief Result of a segmentation request.
     */
    struct SegmentResult {
        std::array<MaskLayer, 3> masks;      ///< 3 candidate masks.
        std::array<float, 3>     iouScores;  ///< IoU quality scores.
        uint8_t                  bestMaskIdx;///< Index of the highest IoU mask.
    };

    /**
     * @brief Generates masks from an embedding and prompt points.
     */
    [[nodiscard]] std::expected<SegmentResult, std::string>
    segmentFromPoints(
        const std::vector<float>&       embedding,
        std::array<int64_t, 4>          embeddingShape,
        const std::vector<PromptPoint>& points,
        uint32_t originalWidth,
        uint32_t originalHeight);

private:
    /**
     * @brief Converts SAM logits to a MaskLayer [0, 1] via bilinear sampling and sigmoid.
     */
    MaskLayer postprocessMask(
        const float* maskData,
        int64_t maskH, int64_t maskW,
        uint32_t targetW, uint32_t targetH);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aether
