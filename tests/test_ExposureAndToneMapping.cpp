/// @file test_ExposureAndToneMapping.cpp
/// @brief Catch2 v3 unit tests for ExposureStage and ToneMappingStage.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../core/pipeline/ExposureStage.hpp"
#include "../core/pipeline/ToneMappingStage.hpp"

using namespace aether;
using Catch::Matchers::WithinAbs;

// ── Helpers ──────────────────────────────────────────────────────────

/// Build a constant-colour PROPHOTO_LINEAR buffer.
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

// ═════════════════════════════════════════════════════════════════════
// ExposureStage Tests
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("Exposure: EV=0, no adjustments → pixels unchanged",
          "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{};  // all zeroes
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.3F, 0.7F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.colorSpace == ColorSpace::PROPHOTO_LINEAR);
    REQUIRE_THAT(out.data[0], WithinAbs(0.5, 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(0.3, 0.001));
    REQUIRE_THAT(out.data[2], WithinAbs(0.7, 0.001));
}

TEST_CASE("Exposure: EV=+1 → pixels × 2.0", "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{.exposureEV = 1.0F};
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.25F, 0.25F, 0.25F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.5, 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(0.5, 0.001));
    REQUIRE_THAT(out.data[2], WithinAbs(0.5, 0.001));
}

TEST_CASE("Exposure: EV=-1 → pixels × 0.5", "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{.exposureEV = -1.0F};
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.25, 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(0.25, 0.001));
    REQUIRE_THAT(out.data[2], WithinAbs(0.25, 0.001));
}

TEST_CASE("Exposure: blackPoint=0.1 → pixel at 0.1 becomes 0",
          "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{.blackPoint = 0.1F};
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.1F, 0.0F, 0.5F);
    ImageBuffer out = stage.process(in);

    // 0.1 - 0.1 = 0.0
    REQUIRE_THAT(out.data[0], WithinAbs(0.0, 0.001));
    // 0.0 - 0.1 → clamped to 0
    REQUIRE_THAT(out.data[1], WithinAbs(0.0, 0.001));
    // (0.5 - 0.1) / 0.9 ≈ 0.4444
    REQUIRE_THAT(out.data[2], WithinAbs(0.4444, 0.002));
}

TEST_CASE("Exposure: shadowLift=0.2 → black pixel becomes (0.2,0.2,0.2)",
          "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{.shadowLift = 0.2F};
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.0F, 0.0F, 0.0F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.2, 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(0.2, 0.001));
    REQUIRE_THAT(out.data[2], WithinAbs(0.2, 0.001));
}

TEST_CASE("Exposure: highlight recovery compresses values above knee",
          "[ExposureStage]") {
    ExposureStage stage;
    ExposureParams params{.highlightComp = 1.0F};
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(2.0F, 2.0F, 2.0F);
    ImageBuffer out = stage.process(in);

    // Values should be compressed: > 0.8 but < 2.0
    REQUIRE(out.data[0] > 0.8F);
    REQUIRE(out.data[0] < 2.0F);
}

// ═════════════════════════════════════════════════════════════════════
// ToneMappingStage Tests
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("ToneMap: Linear op → mid-grey unchanged",
          "[ToneMappingStage]") {
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op = ToneMappingParams::Operator::Linear;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.5, 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(0.5, 0.001));
    REQUIRE_THAT(out.data[2], WithinAbs(0.5, 0.001));
}

TEST_CASE("ToneMap: FilmicS → black stays black",
          "[ToneMappingStage]") {
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op = ToneMappingParams::Operator::FilmicS;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.0F, 0.0F, 0.0F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.0, 0.01));
    REQUIRE_THAT(out.data[1], WithinAbs(0.0, 0.01));
    REQUIRE_THAT(out.data[2], WithinAbs(0.0, 0.01));
}

TEST_CASE("ToneMap: FilmicS → white compressed below 1.0",
          "[ToneMappingStage]") {
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op = ToneMappingParams::Operator::FilmicS;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(1.0F, 1.0F, 1.0F);
    ImageBuffer out = stage.process(in);

    // Filmic compresses — mapped value should be < 1.0
    REQUIRE(out.data[0] < 1.0F);
    REQUIRE(out.data[0] > 0.0F);
}

TEST_CASE("ToneMap: ACES → white maps close to 1.0",
          "[ToneMappingStage]") {
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op = ToneMappingParams::Operator::ACES;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(1.0F, 1.0F, 1.0F);
    ImageBuffer out = stage.process(in);

    // ACES at x=1.0: (1*(2.51+0.03))/(1*(2.43+0.59)+0.14) ≈ 0.8045
    REQUIRE(out.data[0] > 0.5F);
    REQUIRE(out.data[0] <= 1.0F);
}

TEST_CASE("ToneMap: saturation=0 → output is grey (R==G==B)",
          "[ToneMappingStage]") {
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op         = ToneMappingParams::Operator::Linear;
    params.saturation = 0.0F;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.8F, 0.4F, 0.2F);
    ImageBuffer out = stage.process(in);

    // All channels should equal the luminance.
    REQUIRE_THAT(out.data[0], WithinAbs(static_cast<double>(out.data[1]), 0.001));
    REQUIRE_THAT(out.data[1], WithinAbs(static_cast<double>(out.data[2]), 0.001));
}

TEST_CASE("ToneMap: LUT matches direct formula within tolerance",
          "[ToneMappingStage]") {
    // Verify the LUT approximation is close to the analytic function.
    ToneMappingStage stage;
    ToneMappingParams params;
    params.op       = ToneMappingParams::Operator::FilmicS;
    params.contrast = 1.0F;
    stage.setParams(params);

    // Test at 0.5 via the pipeline.
    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);
    ImageBuffer out = stage.process(in);

    // Direct Hable formula for reference:
    // f(x) = ((x*(A*x+C*B)+D*E) / (x*(A*x+B)+D*F)) - E/F
    constexpr float A = 0.15F, B = 0.50F, C = 0.10F;
    constexpr float D = 0.20F, E = 0.02F, F = 0.30F;
    constexpr float W = 11.2F;
    auto hable = [&](float x) {
        return ((x*(A*x+C*B)+D*E) / (x*(A*x+B)+D*F)) - E/F;
    };
    const float expected = hable(0.5F * 2.0F) / hable(W);

    REQUIRE_THAT(out.data[0], WithinAbs(static_cast<double>(expected), 0.002));
}
