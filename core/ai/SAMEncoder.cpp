#include "SAMEncoder.hpp"
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <cmath>
#include <numeric>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace aether {

struct SAMEncoder::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "SAMEncoder"};
    std::unique_ptr<Ort::Session> session;
    
    std::vector<float> cachedEmbedding;
    std::array<int64_t, 4> cachedShape{1, 256, 64, 64};
    bool ready{false};
    size_t lastImageHash{0};

    Impl(const std::filesystem::path& path) {
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // CoreML execution provider fallback
#ifdef HAVE_COREML
            // OrtSessionOptionsAppendExecutionProvider_CoreML(opts, 0);
#endif
            session = std::make_unique<Ort::Session>(env, path.c_str(), opts);
        } catch (const std::exception& e) {
            std::cerr << "[SAMEncoder] Error loading session: " << e.what() << std::endl;
        }
    }

    size_t computeHash(const ImageBuffer& img) {
        if (img.data.empty()) return 0;
        // Simple fingerprint: size + first/last pixel sum
        size_t h = img.width ^ (img.height << 1);
        const float* p = img.ptr();
        h ^= (static_cast<size_t>(p[0] * 1000) << 2);
        h ^= (static_cast<size_t>(p[img.data.size() - 1] * 1000) << 3);
        return h;
    }

    std::vector<float> preprocess(const ImageBuffer& img) {
        // SAM 2 expects 1024x1024 NCHW, normalized with ImageNet stats
        const uint32_t targetSize = 1024;
        std::vector<float> nchw(3 * targetSize * targetSize);
        
        float scaleW = static_cast<float>(img.width) / targetSize;
        float scaleY = static_cast<float>(img.height) / targetSize;

        const float mean[] = {0.485f, 0.456f, 0.406f};
        const float std_dev[] = {0.229f, 0.224f, 0.225f};

        for (uint32_t y = 0; y < targetSize; ++y) {
            for (uint32_t x = 0; x < targetSize; ++x) {
                // Bilinear sampling (simplified for V1, nearest if we want fast)
                uint32_t srcX = std::min(static_cast<uint32_t>(x * scaleW), img.width - 1);
                uint32_t srcY = std::min(static_cast<uint32_t>(y * scaleY), img.height - 1);
                
                const float* pix = &img.ptr()[(srcY * img.width + srcX) * 3];
                
                nchw[0 * targetSize * targetSize + y * targetSize + x] = (pix[0] - mean[0]) / std_dev[0];
                nchw[1 * targetSize * targetSize + y * targetSize + x] = (pix[1] - mean[1]) / std_dev[1];
                nchw[2 * targetSize * targetSize + y * targetSize + x] = (pix[2] - mean[2]) / std_dev[2];
            }
        }
        return nchw;
    }
};

SAMEncoder::SAMEncoder(std::filesystem::path encoderOnnxPath)
    : m_impl(std::make_unique<Impl>(encoderOnnxPath)) {}

SAMEncoder::~SAMEncoder() = default;

std::future<void> SAMEncoder::encodeAsync(const ImageBuffer& image) {
    size_t newHash = m_impl->computeHash(image);
    if (newHash == m_impl->lastImageHash && m_impl->ready) {
        std::promise<void> p;
        p.set_value();
        return p.get_future();
    }

    return std::async(std::launch::async, [this, image, newHash]() {
        if (!m_impl->session) return;

        std::vector<float> tensor = m_impl->preprocess(image);
        
        std::array<int64_t, 4> inputShape{1, 3, 1024, 1024};
        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
         Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, tensor.data(), tensor.size(), inputShape.data(), inputShape.size());

        const char* inputNames[] = {"image"};
        const char* outputNames[] = {"image_embeddings"};

        try {
            auto outputs = m_impl->session->Run(
                Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

            float* embData = outputs[0].GetTensorMutableData<float>();
            auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
            
            size_t total = 1;
            for (auto dim : shape) total *= dim;
            
            m_impl->cachedEmbedding.assign(embData, embData + total);
            m_impl->cachedShape = {shape[0], shape[1], shape[2], shape[3]};
            m_impl->ready = true;
            m_impl->lastImageHash = newHash;
        } catch (const std::exception& e) {
            std::cerr << "[SAMEncoder] Inference error: " << e.what() << std::endl;
        }
    });
}

bool SAMEncoder::isReady() const noexcept {
    return m_impl->ready;
}

const std::vector<float>& SAMEncoder::embedding() const noexcept {
    return m_impl->cachedEmbedding;
}

std::array<int64_t, 4> SAMEncoder::embeddingShape() const noexcept {
    return m_impl->cachedShape;
}

} // namespace aether
