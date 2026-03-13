/// @file test_PipelineEngine.cpp
/// @brief Catch2 v3 unit tests for PipelineEngine.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "../core/PipelineEngine.hpp"

using namespace aether;

// ─────────────────────────────────────────────────────────────────────
// Test 1 — Default construction, isReady == false
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("PipelineEngine: constructs without crash, isReady false",
          "[PipelineEngine]") {
    PipelineEngine engine;
    REQUIRE(engine.isReady() == false);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2 — updateParam before loadRaw does not crash
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("PipelineEngine: updateParam before loadRaw is safe",
          "[PipelineEngine]") {
    PipelineEngine engine;
    ExposureParams ep{.exposureEV = 1.5F};
    REQUIRE_NOTHROW(engine.updateParam(StageId::Exposure, ep));
}

// ─────────────────────────────────────────────────────────────────────
// Test 3 — markDirtyFrom marks correct stages
// ─────────────────────────────────────────────────────────────────────

// Note: markDirtyFrom is private, so we test its effect indirectly
// through updateParam. After updateParam(Exposure, ...), the engine
// should at minimum NOT crash on subsequent calls.

TEST_CASE("PipelineEngine: updateParam marks downstream stages dirty",
          "[PipelineEngine]") {
    PipelineEngine engine;

    // Update exposure — should not affect upstream stages.
    ExposureParams ep{.exposureEV = 2.0F};
    REQUIRE_NOTHROW(engine.updateParam(StageId::Exposure, ep));

    // Update tone mapping afterwards — also should not crash.
    ToneMappingParams tp;
    tp.op = ToneMappingParams::Operator::ACES;
    REQUIRE_NOTHROW(engine.updateParam(StageId::ToneMapping, tp));
}

// ─────────────────────────────────────────────────────────────────────
// Test 4 — (Commented) Full pipeline integration with a real ARW
// ─────────────────────────────────────────────────────────────────────

/*
TEST_CASE("PipelineEngine: full pipeline from ARW to sRGB preview",
          "[PipelineEngine][integration]") {
    PipelineEngine engine;
    engine.loadRaw("/path/to/test_image.ARW");
    REQUIRE(engine.isReady() == true);

    std::stop_source ss;
    auto future = engine.requestPreview(1920, 1280, ss.get_token());
    ImageBuffer preview = future.get();

    REQUIRE(preview.width  > 0);
    REQUIRE(preview.height > 0);
    REQUIRE(preview.colorSpace == ColorSpace::DISPLAY_SRGB);
    REQUIRE(preview.isLinear == false);

    // Verify all values are in [0, 1].
    for (size_t i = 0; i < preview.data.size(); ++i) {
        REQUIRE(preview.data[i] >= 0.0F);
        REQUIRE(preview.data[i] <= 1.0F);
    }

    // Metadata should be populated.
    const auto& meta = engine.metadata();
    REQUIRE_FALSE(meta.cameraMake.empty());
}
*/

// ─────────────────────────────────────────────────────────────────────
// Test 5 — (Commented) Dirty-cache invalidation on exposure update
// ─────────────────────────────────────────────────────────────────────

/*
TEST_CASE("PipelineEngine: exposure update invalidates only downstream",
          "[PipelineEngine][integration]") {
    PipelineEngine engine;
    engine.loadRaw("/path/to/test_image.ARW");

    // First render.
    std::stop_source ss1;
    auto f1 = engine.requestFullRender(ss1.get_token());
    ImageBuffer r1 = f1.get();

    // Change exposure.
    engine.updateParam(StageId::Exposure, ExposureParams{.exposureEV = 2.0F});

    // Second render — only stages >= Exposure should recompute.
    std::stop_source ss2;
    auto f2 = engine.requestFullRender(ss2.get_token());
    ImageBuffer r2 = f2.get();

    REQUIRE(r2.width == r1.width);
    REQUIRE(r2.height == r1.height);
    // Values should differ due to EV change.
    REQUIRE(r2.data[0] != r1.data[0]);
}
*/
