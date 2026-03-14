/// @file PipelineEngine.cpp
/// @brief Implementation of PipelineEngine — the dirty-cache orchestrator
///        that chains all pipeline stages from RAW ingest to display output.

#include "PipelineEngine.hpp"

#include "pipeline/RawInputStage.hpp"
#include "pipeline/ColorMatrixStage.hpp"
#include "pipeline/ExposureStage.hpp"
#include "pipeline/ToneMappingStage.hpp"
#include "pipeline/MaskCompositeStage.hpp"
#include "pipeline/OutputTransformStage.hpp"

#include <iostream>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction / Move
// ─────────────────────────────────────────────────────────────────────

PipelineEngine::PipelineEngine() {
    // Instantiate concrete stages.
    m_stages[0] = std::make_unique<RawInputStage>();
    m_stages[1] = nullptr;  // DemosaicStage — LibRaw handles demosaic internally.
    m_stages[2] = std::make_unique<ColorMatrixStage>();
    m_stages[3] = std::make_unique<ExposureStage>();
    m_stages[4] = std::make_unique<ToneMappingStage>();
    m_stages[5] = std::make_unique<MaskCompositeStage>();
    m_stages[6] = std::make_unique<OutputTransformStage>();

    // Initialise all stages as dirty.
    for (uint8_t i = 0; i < kStageCount; ++i) {
        m_dirty[i] = true;
    }
}

PipelineEngine::~PipelineEngine() = default;

// ─────────────────────────────────────────────────────────────────────
// loadRaw
// ─────────────────────────────────────────────────────────────────────

void PipelineEngine::loadRaw(std::filesystem::path path) {
    std::cerr << "[PipelineEngine] loadRaw: waiting for lock... [" << path.filename().string() << "]\n";
    std::lock_guard lock(m_renderMutex);
    std::cerr << "[PipelineEngine] loadRaw: lock acquired.\n";
    auto* rawStage = static_cast<RawInputStage*>(m_stages[0].get());
    if (!rawStage) {
        std::cerr << "[PipelineEngine] RawInputStage is null.\n";
        return;
    }

    auto err = rawStage->loadFile(std::move(path));
    if (!err) {
        std::cerr << "[PipelineEngine] loadRaw failed: "
                  << err.error().message << '\n';
        return;
    }

    markDirtyFrom(StageId::RawIngest);
    m_ready = true;
}

// ─────────────────────────────────────────────────────────────────────
// updateParam
// ─────────────────────────────────────────────────────────────────────

void PipelineEngine::updateParam(StageId stage, StageParams params) {
    std::lock_guard lock(m_renderMutex);
    std::cerr << "[PipelineEngine] updateParam: stage " << static_cast<int>(stage) << ".\n";
    const auto idx = static_cast<uint8_t>(stage);
    if (idx < kStageCount && m_stages[idx]) {
        m_stages[idx]->setParams(params);
    }
    markDirtyFrom(stage);
}

// ─────────────────────────────────────────────────────────────────────
// markDirtyFrom
// ─────────────────────────────────────────────────────────────────────

void PipelineEngine::markDirtyFrom(StageId from) {
    for (uint8_t i = static_cast<uint8_t>(from); i < kStageCount; ++i) {
        m_dirty[i] = true;
        m_cache.erase(i);
    }
}

// ─────────────────────────────────────────────────────────────────────
// runPipeline — dirty-cache core
// ─────────────────────────────────────────────────────────────────────

ImageBuffer PipelineEngine::runPipeline(std::stop_token cancel) {
    ImageBuffer current{};

    for (uint8_t i = 0; i < kStageCount; ++i) {
        if (cancel.stop_requested()) {
            std::cerr << "[PipelineEngine] runPipeline: stop requested at stage " << static_cast<int>(i) << ".\n";
            return {};
        }

        std::cerr << "[PipelineEngine] runPipeline: stage " << static_cast<int>(i) << " waiting for lock...\n";
        std::lock_guard lock(m_renderMutex);
        std::cerr << "[PipelineEngine] runPipeline: stage " << static_cast<int>(i) << " lock acquired.\n";
        if (m_stages[i] == nullptr) continue;

        if (!m_dirty[i]) {
            if (m_cache.contains(i)) {
                current = m_cache[i];
                continue;
            }
        }

        // Recompute.
        std::cerr << "[PipelineEngine] runPipeline: processing stage " << static_cast<int>(i) << "...\n";
        current    = m_stages[i]->process(current);
        std::cerr << "[PipelineEngine] runPipeline: stage " << static_cast<int>(i) << " recomputed.\n";

        if (current.width == 0 || current.height == 0) {
            std::cerr << "[PipelineEngine] runPipeline: stage " << static_cast<int>(i) << " failed (empty buffer). Aborting.\n";
            return {};
        }

        m_cache[i] = current;
        m_dirty[i] = false;

        if (i == static_cast<uint8_t>(StageId::RawIngest)) {
            auto* raw = static_cast<RawInputStage*>(m_stages[0].get());
            m_metadata = raw->metadata();
            std::cerr << "[PipelineEngine] Captured metadata: ISO " << m_metadata.isoValue << "\n";

            // Non-destructively push camera matrix to ColorMatrixStage if it's set to use camera WB.
            auto* colorStage = m_stages[static_cast<uint8_t>(StageId::ColorMatrix)].get();
            if (colorStage) {
                auto params = colorStage->getParams();
                if (auto* cmp = std::get_if<ColorMatrixParams>(&params)) {
                    if (cmp->useCameraWB) {
                        cmp->cameraToXYZ_D50 = m_metadata.colorMatrix1;
                        colorStage->setParams(*cmp);
                    }
                }
            }
        }
    }

    return current;
}

// ─────────────────────────────────────────────────────────────────────
// requestPreview
// ─────────────────────────────────────────────────────────────────────

std::future<ImageBuffer>
PipelineEngine::requestPreview(uint32_t w, uint32_t h,
                               std::stop_token cancel) {
    return std::async(std::launch::async,
        [this, w, h, cancel]() -> ImageBuffer {
            {
                std::lock_guard lock(m_renderMutex);
                // Enable half-size decoding for fast preview.
                RawIngestParams fastParams{.halfSize = true, .useCameraWB = true};
                m_stages[0]->setParams(fastParams);
            }

            ImageBuffer full = runPipeline(cancel);
            if (full.width == 0) return {};

            auto scaled = full.downscale(w, h);
            return scaled.value_or(ImageBuffer{});
        });
}

// ─────────────────────────────────────────────────────────────────────
// requestFullRender
// ─────────────────────────────────────────────────────────────────────

std::future<ImageBuffer>
PipelineEngine::requestFullRender(std::stop_token cancel) {
    return std::async(std::launch::async,
        [this, cancel]() -> ImageBuffer {
            {
                std::lock_guard lock(m_renderMutex);
                RawIngestParams fullParams{.halfSize = false, .useCameraWB = true};
                m_stages[0]->setParams(fullParams);
                markDirtyFrom(StageId::RawIngest);
            }

            return runPipeline(cancel);
        });
}

// ─────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────

const CameraMetadata& PipelineEngine::metadata() const noexcept {
    std::lock_guard lock(m_renderMutex);
    return m_metadata;
}

bool PipelineEngine::isReady() const noexcept {
    std::lock_guard lock(m_renderMutex);
    return m_ready;
}

} // namespace aether
