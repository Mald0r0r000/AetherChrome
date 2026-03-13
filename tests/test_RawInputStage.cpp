/// @file test_RawInputStage.cpp
/// @brief Catch2 v3 unit tests for RawInputStage.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "../core/pipeline/RawInputStage.hpp"

using namespace aether;

// ─────────────────────────────────────────────────────────────────────
// Test 1 — Loading a non-existent file returns an error
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("loadFile with non-existent path returns error",
          "[RawInputStage]") {
    RawInputStage stage;
    auto result = stage.loadFile("/tmp/__nonexistent_file__.ARW");

    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(result.error().message.empty());
}

// ─────────────────────────────────────────────────────────────────────
// Test 2 — process() on an unloaded stage returns an empty buffer
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("process on unloaded stage returns empty buffer",
          "[RawInputStage]") {
    RawInputStage stage;
    ImageBuffer empty;
    ImageBuffer out = stage.process(empty);

    REQUIRE(out.width  == 0);
    REQUIRE(out.height == 0);
    REQUIRE(out.data.empty());
}

// ─────────────────────────────────────────────────────────────────────
// Test 3 — setParams accepts RawIngestParams
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("setParams accepts RawIngestParams without error",
          "[RawInputStage]") {
    RawInputStage stage;
    RawIngestParams params{.halfSize = true, .useCameraWB = false};
    REQUIRE_NOTHROW(stage.setParams(params));
}

// ─────────────────────────────────────────────────────────────────────
// Test 4 — setParams silently ignores monostate
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("setParams ignores unrelated variant types",
          "[RawInputStage]") {
    RawInputStage stage;
    StageParams mono = std::monostate{};
    REQUIRE_NOTHROW(stage.setParams(mono));
}

// ─────────────────────────────────────────────────────────────────────
// Test 5 — Introspection
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("Stage introspection returns expected values",
          "[RawInputStage]") {
    RawInputStage stage;
    REQUIRE(stage.id() == StageId::RawIngest);
    REQUIRE(stage.supportsGPU() == false);
    REQUIRE_FALSE(stage.description().empty());
}

// ─────────────────────────────────────────────────────────────────────
// Test 6 — (Commented) Full integration test with a real ARW file
// ─────────────────────────────────────────────────────────────────────

/*
TEST_CASE("Load a real Sony ARW and verify linear output",
          "[RawInputStage][integration]") {
    RawInputStage stage;

    auto loadResult = stage.loadFile("/path/to/test_image.ARW");
    REQUIRE(loadResult.has_value());

    ImageBuffer buf = stage.process(ImageBuffer{});

    REQUIRE(buf.width > 0);
    REQUIRE(buf.height > 0);
    REQUIRE(buf.colorSpace == ColorSpace::LINEAR_RAW);
    REQUIRE(buf.isLinear == true);
    REQUIRE(buf.data.size() == buf.pixelCount() * buf.channelCount());

    // All values should be normalised to [0.0, 1.0].
    for (size_t i = 0; i < buf.data.size(); ++i) {
        REQUIRE(buf.data[i] >= 0.0F);
        REQUIRE(buf.data[i] <= 1.0F);
    }

    // Metadata should be populated.
    const auto& meta = stage.metadata();
    REQUIRE_FALSE(meta.cameraMake.empty());
    REQUIRE_FALSE(meta.cameraModel.empty());
    REQUIRE(meta.isoValue > 0.0F);
}
*/
