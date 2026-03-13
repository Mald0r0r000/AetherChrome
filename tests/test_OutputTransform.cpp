/// @file test_OutputTransform.cpp
/// @brief Catch2 v3 unit tests for OutputTransformStage.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../core/pipeline/OutputTransformStage.hpp"

using namespace aether;
using Catch::Matchers::WithinAbs;

// ── Helper ───────────────────────────────────────────────────────────

static ImageBuffer makeProPhotoBuffer(float r, float g, float b,
                                       uint32_t w = 4, uint32_t h = 4) {
    auto result = ImageBuffer::create(w, h, ColorSpace::PROPHOTO_LINEAR);
    auto& buf   = *result;
    buf.whitePoint = WhitePoint::D50();
    buf.isLinear   = true;
    for (size_t i = 0; i < buf.pixelCount(); ++i) {
        buf.data[i * 3 + 0] = r;
        buf.data[i * 3 + 1] = g;
        buf.data[i * 3 + 2] = b;
    }
    return buf;
}

// ─────────────────────────────────────────────────────────────────────
// Test 1 — Black stays black
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: black (0,0,0) → (0,0,0) sRGB",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    ImageBuffer in  = makeProPhotoBuffer(0.0F, 0.0F, 0.0F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.0, 0.01));
    REQUIRE_THAT(out.data[1], WithinAbs(0.0, 0.01));
    REQUIRE_THAT(out.data[2], WithinAbs(0.0, 0.01));
}

// ─────────────────────────────────────────────────────────────────────
// Test 2 — White stays close to white
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: white (1,1,1) ProPhoto → close to (1,1,1) sRGB",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    ImageBuffer in  = makeProPhotoBuffer(1.0F, 1.0F, 1.0F);
    ImageBuffer out = stage.process(in);

    // Chromatic adaptation D50→D65 + gamut mapping may shift slightly.
    REQUIRE_THAT(out.data[0], WithinAbs(1.0, 0.05));
    REQUIRE_THAT(out.data[1], WithinAbs(1.0, 0.05));
    REQUIRE_THAT(out.data[2], WithinAbs(1.0, 0.05));
}

// ─────────────────────────────────────────────────────────────────────
// Test 3 — Output colorSpace is DISPLAY_SRGB
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: output colorSpace == DISPLAY_SRGB",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.colorSpace == ColorSpace::DISPLAY_SRGB);
}

// ─────────────────────────────────────────────────────────────────────
// Test 4 — Output isLinear is false
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: output isLinear == false",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.isLinear == false);
}

// ─────────────────────────────────────────────────────────────────────
// Test 5 — Output whitePoint is D65
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: output whitePoint == D65",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.whitePoint == WhitePoint::D65());
}

// ─────────────────────────────────────────────────────────────────────
// Test 6 — All values clamped to [0, 1]
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: all output values in [0.0, 1.0]",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    // Saturated ProPhoto colour — may be out-of-gamut in sRGB.
    ImageBuffer in  = makeProPhotoBuffer(0.9F, 0.1F, 0.2F);
    ImageBuffer out = stage.process(in);

    for (size_t i = 0; i < out.data.size(); ++i) {
        REQUIRE(out.data[i] >= 0.0F);
        REQUIRE(out.data[i] <= 1.0F);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Test 7 — Switch to DISPLAY_P3
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: DISPLAY_P3 output",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    OutputTransformParams params;
    params.displaySpace = ColorSpace::DISPLAY_P3;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.colorSpace == ColorSpace::DISPLAY_P3);
}

// ─────────────────────────────────────────────────────────────────────
// Test 8 — Non-PROPHOTO input returns unchanged
// ─────────────────────────────────────────────────────────────────────

TEST_CASE("OutputTransform: non-PROPHOTO input → returned unchanged",
          "[OutputTransformStage]") {
    OutputTransformStage stage;

    auto result = ImageBuffer::create(4, 4, ColorSpace::LINEAR_RAW);
    auto& in    = *result;
    in.data[0]  = 0.42F;

    ImageBuffer out = stage.process(in);

    // Should be returned as-is.
    REQUIRE(out.colorSpace == ColorSpace::LINEAR_RAW);
    REQUIRE(out.data[0] == 0.42F);
}
