#include "AIMaskController.hpp"
#include <iostream>

namespace aether {

AIMaskController::AIMaskController(std::filesystem::path modelsDir) {
    auto encPath = modelsDir / "mobile_sam_image_encoder.onnx";
    auto decPath = modelsDir / "sam_mask_decoder_single.onnx";

    if (std::filesystem::exists(encPath) && std::filesystem::exists(decPath)) {
        m_encoder = std::make_unique<SAMEncoder>(encPath);
        m_decoder = std::make_unique<SAMDecoder>(decPath);
        m_available = true;
    } else {
        std::cerr << "[AIMaskController] Models missing in " << modelsDir << std::endl;
    }
}

AIMaskController::~AIMaskController() = default;

bool AIMaskController::isAvailable() const noexcept {
    return m_available;
}

bool AIMaskController::isEncoderReady() const noexcept {
    return m_available && m_encoder && m_encoder->isReady();
}

std::future<void> AIMaskController::prepareImage(const ImageBuffer& image) {
    if (!m_available) return std::async(std::launch::deferred, [](){});
    
    m_imageW = image.width;
    m_imageH = image.height;
    m_currentPoints.clear();
    
    return m_encoder->encodeAsync(image);
}

void AIMaskController::prepareImageAsync() {
    // Placeholder: in a real scenario, this might fetch the last buffer from the engine
    // or wait for the next preview render to grab the buffer.
}

std::future<MaskLayer> AIMaskController::segmentAtPoint(float normX, float normY, bool positive) {
    if (!isEncoderReady()) return std::async(std::launch::deferred, [](){ return MaskLayer{}; });

    return std::async(std::launch::async, [this, normX, normY, positive]() {
        m_currentPoints.push_back({normX, normY, positive});
        
        auto res = m_decoder->segmentFromPoints(
            m_encoder->embedding(),
            m_encoder->embeddingShape(),
            m_currentPoints,
            m_imageW,
            m_imageH
        );

        if (res) {
            return res->masks[res->bestMaskIdx];
        } else {
            std::cerr << "[AIMaskController] Error: " << res.error() << std::endl;
            return MaskLayer{};
        }
    });
}

std::future<MaskLayer> AIMaskController::segmentSubject(FashionSubject subject) {
    if (!isEncoderReady()) return std::async(std::launch::deferred, [](){ return MaskLayer{}; });

    return std::async(std::launch::async, [this, subject]() {
        auto points = generatePromptPoints(subject);
        
        auto res = m_decoder->segmentFromPoints(
            m_encoder->embedding(),
            m_encoder->embeddingShape(),
            points,
            m_imageW,
            m_imageH
        );

        if (res) {
            return res->masks[res->bestMaskIdx];
        }
        return MaskLayer{};
    });
}

std::vector<SAMDecoder::PromptPoint> AIMaskController::generatePromptPoints(FashionSubject subject) const {
    std::vector<SAMDecoder::PromptPoint> pts;
    switch (subject) {
        case FashionSubject::Person:
            pts.push_back({0.5f, 0.4f, true}); // Upper body
            pts.push_back({0.5f, 0.7f, true}); // Lower body
            break;
        case FashionSubject::Garment:
            pts.push_back({0.5f, 0.5f, true}); // Torso center
            pts.push_back({0.3f, 0.3f, true}); // Left shoulder
            pts.push_back({0.7f, 0.3f, true}); // Right shoulder
            break;
        case FashionSubject::Background:
            pts.push_back({0.01f, 0.01f, true}); // TL
            pts.push_back({0.99f, 0.01f, true}); // TR
            pts.push_back({0.01f, 0.99f, true}); // BL
            pts.push_back({0.99f, 0.99f, true}); // BR
            pts.push_back({0.5f, 0.5f, false});  // Exclude center person
            break;
        default:
            break;
    }
    return pts;
}

MaskLayer AIMaskController::mergeMasks(const std::vector<MaskLayer>& masks) {
    if (masks.empty()) return MaskLayer{};
    if (masks.size() == 1) return masks[0];

    MaskLayer unionMask = masks[0];
    for (size_t i = 1; i < masks.size(); ++i) {
        if (masks[i].alpha.size() != unionMask.alpha.size()) continue;
        for (size_t j = 0; j < unionMask.alpha.size(); ++j) {
            // Union of soft masks: 1 - (1-a)*(1-b)
            unionMask.alpha[j] = 1.0f - (1.0f - unionMask.alpha[j]) * (1.0f - masks[i].alpha[j]);
        }
    }
    return unionMask;
}

} // namespace aether
