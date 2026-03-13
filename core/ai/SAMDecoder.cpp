#include "SAMDecoder.hpp"
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace aether {

struct SAMDecoder::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "SAMDecoder"};
    std::unique_ptr<Ort::Session> session;

    Impl(const std::filesystem::path& path) {
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            session = std::make_unique<Ort::Session>(env, path.c_str(), opts);
        } catch (const std::exception& e) {
            std::cerr << "[SAMDecoder] Error: " << e.what() << std::endl;
        }
    }
};

SAMDecoder::SAMDecoder(std::filesystem::path decoderOnnxPath)
    : m_impl(std::make_unique<Impl>(decoderOnnxPath)) {}

SAMDecoder::~SAMDecoder() = default;

std::expected<SAMDecoder::SegmentResult, std::string>
SAMDecoder::segmentFromPoints(
    const std::vector<float>&       embedding,
    std::array<int64_t, 4>          embeddingShape,
    const std::vector<PromptPoint>& points,
    uint32_t originalWidth,
    uint32_t originalHeight) 
{
    if (embedding.empty() || !m_impl->session) {
        return std::unexpected("Decoder not ready or embedding empty");
    }

    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // 1. image_embeddings [1, 256, 64, 64]
    Ort::Value embTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, const_cast<float*>(embedding.data()), embedding.size(),
        embeddingShape.data(), embeddingShape.size());

    // 2. point_coords [1, N, 2]
    int64_t numPoints = static_cast<int64_t>(points.size());
    std::vector<float> coords(numPoints * 2);
    for (int64_t i = 0; i < numPoints; ++i) {
        coords[i * 2 + 0] = points[i].x * 1024.0f;
        coords[i * 2 + 1] = points[i].y * 1024.0f;
    }
    std::array<int64_t, 3> coordsShape{1, numPoints, 2};
    Ort::Value coordsTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, coords.data(), coords.size(), coordsShape.data(), coordsShape.size());

    // 3. point_labels [1, N]
    std::vector<int32_t> labels(numPoints);
    for (int64_t i = 0; i < numPoints; ++i) {
        labels[i] = points[i].positive ? 1 : 0;
    }
    std::array<int64_t, 2> labelsShape{1, numPoints};
    Ort::Value labelsTensor = Ort::Value::CreateTensor<int32_t>(
        memoryInfo, labels.data(), labels.size(), labelsShape.data(), labelsShape.size());

    // 4. mask_input [1, 1, 256, 256] (zeros for first call)
    std::vector<float> maskIn(1 * 1 * 256 * 256, 0.0f);
    std::array<int64_t, 4> maskInShape{1, 1, 256, 256};
    Ort::Value maskInTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, maskIn.data(), maskIn.size(), maskInShape.data(), maskInShape.size());

    // 5. has_mask_input [1]
    float hasMask = 0.0f;
    std::array<int64_t, 1> hasMaskShape{1};
    Ort::Value hasMaskTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, &hasMask, 1, hasMaskShape.data(), hasMaskShape.size());

    // 6. orig_im_size [2]
    std::array<int32_t, 2> origSize{static_cast<int32_t>(originalHeight), static_cast<int32_t>(originalWidth)};
    std::array<int64_t, 1> origSizeShape{2};
    Ort::Value origSizeTensor = Ort::Value::CreateTensor<int32_t>(
        memoryInfo, origSize.data(), origSize.size(), origSizeShape.data(), origSizeShape.size());

    const char* inputNames[] = {
        "image_embeddings", "point_coords", "point_labels", 
        "mask_input", "has_mask_input", "orig_im_size"
    };
    Ort::Value inputs[] = {
        std::move(embTensor), std::move(coordsTensor), std::move(labelsTensor),
        std::move(maskInTensor), std::move(hasMaskTensor), std::move(origSizeTensor)
    };

    const char* outputNames[] = {"masks", "iou_predictions", "low_res_masks"};

    try {
        auto outputs = m_impl->session->Run(
            Ort::RunOptions{nullptr}, inputNames, inputs, 6, outputNames, 3);

        SegmentResult res;
        
        // masks [1, 3, H, W]
        float* maskData = outputs[0].GetTensorMutableData<float>();
        auto maskShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int64_t H = maskShape[2];
        int64_t W = maskShape[3];

        // iou_predictions [1, 3]
        float* iouData = outputs[1].GetTensorMutableData<float>();

        float maxIou = -1.0f;
        for (uint8_t i = 0; i < 3; ++i) {
            res.iouScores[i] = iouData[i];
            res.masks[i] = postprocessMask(maskData + i * H * W, H, W, originalWidth, originalHeight);
            if (res.iouScores[i] > maxIou) {
                maxIou = res.iouScores[i];
                res.bestMaskIdx = i;
            }
        }

        return res;

    } catch (const std::exception& e) {
        return std::unexpected(std::string("Decoder execution failed: ") + e.what());
    }
}

MaskLayer SAMDecoder::postprocessMask(
    const float* maskData, 
    int64_t maskH, int64_t maskW, 
    uint32_t targetW, uint32_t targetH) 
{
    MaskLayer ml;
    ml.width = targetW;
    ml.height = targetH;
    ml.alpha.resize(targetW * targetH);

    float scaleX = static_cast<float>(maskW) / targetW;
    float scaleY = static_cast<float>(maskH) / targetH;

    auto sigmoid = [](float x) { return 1.0f / (1.0f + std::exp(-x)); };

    for (uint32_t y = 0; y < targetH; ++y) {
        for (uint32_t x = 0; x < targetW; ++x) {
            // Nearest neighbor for speed in V1, could be bilinear
            int64_t sx = std::min(static_cast<int64_t>(x * scaleX), maskW - 1);
            int64_t sy = std::min(static_cast<int64_t>(y * scaleY), maskH - 1);
            
            float logit = maskData[sy * maskW + sx];
            ml.alpha[y * targetW + x] = sigmoid(logit);
        }
    }

    return ml;
}

} // namespace aether
