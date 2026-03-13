#pragma once
#include "SAMEncoder.hpp"
#include "SAMDecoder.hpp"
#include "../../core/ImageBuffer.hpp"
#include "../../core/Types.hpp"
#include <filesystem>
#include <memory>
#include <future>
#include <vector>

namespace aether {

/**
 * @class AIMaskController
 * @brief High-level controller for SAM 2 segmentation.
 */
class AIMaskController {
public:
    /**
     * @brief Initializes the controller and sessions.
     * @param modelsDir Directory containing encoder.onnx and decoder.onnx.
     */
    explicit AIMaskController(std::filesystem::path modelsDir);
    ~AIMaskController();

    [[nodiscard]] bool isAvailable() const noexcept;
    [[nodiscard]] bool isEncoderReady() const noexcept;

    /**
     * @brief Submits an image for encoding.
     */
    std::future<void> prepareImage(const ImageBuffer& image);
                                            
    /**
     * @brief Placeholder for async image preparation without explicit buffer.
     */
    void prepareImageAsync();

    /**
     * @brief Interactive segmentation from points.
     */
    std::future<MaskLayer> segmentAtPoint(float normX, float normY, bool positive = true);

    /**
     * @brief High-level subject segmentation.
     */
    enum class FashionSubject : uint8_t {
        Person      = 0,
        Garment     = 1,
        Accessory   = 2,
        Background  = 3
    };

    std::future<MaskLayer> segmentSubject(FashionSubject subject);

    /**
     * @brief Utility for merging masks (logical union).
     */
    static MaskLayer mergeMasks(const std::vector<MaskLayer>& masks);

private:
    std::unique_ptr<SAMEncoder> m_encoder;
    std::unique_ptr<SAMDecoder> m_decoder;
    bool                        m_available{false};
    uint32_t                    m_imageW{0}, m_imageH{0};
    
    std::vector<SAMDecoder::PromptPoint> m_currentPoints;

    std::vector<SAMDecoder::PromptPoint> generatePromptPoints(FashionSubject subject) const;
};

} // namespace aether
