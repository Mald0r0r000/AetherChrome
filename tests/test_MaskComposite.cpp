/// @file test_MaskComposite.cpp
/// @brief Catch2 v3 unit tests for ParametricMaskBuilder and MaskCompositeStage.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../core/pipeline/MaskCompositeStage.hpp"
#include "../core/mask/ParametricMaskBuilder.hpp"

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

// ═════════════════════════════════════════════════════════════════════
// Test 1 — layerCount=0 → pass-through
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: layerCount=0 → output == input",
          "[MaskCompositeStage]") {
    MaskCompositeStage stage;
    MaskCompositeParams params;
    params.layerCount = 0;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.5F, 0.3F, 0.7F);
    ImageBuffer out = stage.process(in);

    REQUIRE(out.data[0] == in.data[0]);
    REQUIRE(out.data[1] == in.data[1]);
    REQUIRE(out.data[2] == in.data[2]);
}

// ═════════════════════════════════════════════════════════════════════
// Test 2 — full luma range → alpha ≈ 1 everywhere
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: lumaMin=0 lumaMax=1 → alpha ≈ 1",
          "[ParametricMaskBuilder]") {
    ImageBuffer in = makeProPhotoBuffer(0.5F, 0.5F, 0.5F);

    ParametricMaskParams p;
    p.lumaMin = 0.0F;
    p.lumaMax = 1.0F;
    p.lumaFeather = 0.0F;
    // Allow full hue + sat range.
    p.hueWidth = 360.0F;
    p.satMin = 0.0F;
    p.satMax = 1.0F;

    MaskLayer mask = ParametricMaskBuilder::build(in, p);

    for (size_t i = 0; i < mask.alpha.size(); ++i) {
        REQUIRE_THAT(mask.alpha[i], WithinAbs(1.0, 0.01));
    }
}

// ═════════════════════════════════════════════════════════════════════
// Test 3 — lumaMin=0.9 → dark pixel alpha ≈ 0
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: lumaMin=0.9 → dark pixel (0.1) alpha ≈ 0",
          "[ParametricMaskBuilder]") {
    ImageBuffer in = makeProPhotoBuffer(0.1F, 0.1F, 0.1F);

    ParametricMaskParams p;
    p.lumaMin = 0.9F;
    p.lumaMax = 1.0F;
    p.lumaFeather = 0.0F;
    p.hueWidth = 360.0F;

    MaskLayer mask = ParametricMaskBuilder::build(in, p);
    REQUIRE_THAT(mask.alpha[0], WithinAbs(0.0, 0.01));
}

// ═════════════════════════════════════════════════════════════════════
// Test 4 — lumaMax=0.1 → dark pixel alpha ≈ 1, bright alpha ≈ 0
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: lumaMax=0.1 → dark alpha≈1, bright alpha≈0",
          "[ParametricMaskBuilder]") {
    // Dark pixel.
    ImageBuffer dark = makeProPhotoBuffer(0.05F, 0.05F, 0.05F, 2, 2);
    ParametricMaskParams p;
    p.lumaMin = 0.0F;
    p.lumaMax = 0.1F;
    p.lumaFeather = 0.0F;
    p.hueWidth = 360.0F;

    MaskLayer maskDark = ParametricMaskBuilder::build(dark, p);
    REQUIRE_THAT(maskDark.alpha[0], WithinAbs(1.0, 0.01));

    // Bright pixel.
    ImageBuffer bright = makeProPhotoBuffer(0.8F, 0.8F, 0.8F, 2, 2);
    MaskLayer maskBright = ParametricMaskBuilder::build(bright, p);
    REQUIRE_THAT(maskBright.alpha[0], WithinAbs(0.0, 0.01));
}

// ═════════════════════════════════════════════════════════════════════
// Test 5 — exposureInMask=+1, full mask → pixels × 2.0
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: exposureInMask=+1 with full mask → ×2",
          "[MaskCompositeStage]") {
    MaskCompositeStage stage;
    MaskCompositeParams params;
    params.layerCount = 1;
    params.layerEnabled[0] = true;
    params.layers[0].lumaMin = 0.0F;
    params.layers[0].lumaMax = 1.0F;
    params.layers[0].lumaFeather = 0.0F;
    params.layers[0].hueWidth = 360.0F;
    params.exposureInMask = 1.0F;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.25F, 0.25F, 0.25F);
    ImageBuffer out = stage.process(in);

    REQUIRE_THAT(out.data[0], WithinAbs(0.5, 0.02));
    REQUIRE_THAT(out.data[1], WithinAbs(0.5, 0.02));
}

// ═════════════════════════════════════════════════════════════════════
// Test 6 — saturationInMask=0 with full mask → grey output
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: saturationInMask=0 → greyscale",
          "[MaskCompositeStage]") {
    MaskCompositeStage stage;
    MaskCompositeParams params;
    params.layerCount = 1;
    params.layerEnabled[0] = true;
    params.layers[0].lumaMin = 0.0F;
    params.layers[0].lumaMax = 1.0F;
    params.layers[0].lumaFeather = 0.0F;
    params.layers[0].hueWidth = 360.0F;
    params.saturationInMask = 0.0F;
    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.8F, 0.4F, 0.2F);
    ImageBuffer out = stage.process(in);

    // R == G == B (greyscale).
    REQUIRE_THAT(out.data[0], WithinAbs(static_cast<double>(out.data[1]), 0.01));
    REQUIRE_THAT(out.data[1], WithinAbs(static_cast<double>(out.data[2]), 0.01));
}

// ═════════════════════════════════════════════════════════════════════
// Test 7 — inverted mask (via lumaMin trick — TODO: proper inversion)
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: excluding bright pixels via lumaMax=0.3",
          "[ParametricMaskBuilder]") {
    ImageBuffer bright = makeProPhotoBuffer(0.9F, 0.9F, 0.9F, 2, 2);
    ParametricMaskParams p;
    p.lumaMin = 0.0F;
    p.lumaMax = 0.3F;
    p.lumaFeather = 0.0F;
    p.hueWidth = 360.0F;

    MaskLayer mask = ParametricMaskBuilder::build(bright, p);
    REQUIRE_THAT(mask.alpha[0], WithinAbs(0.0, 0.01));
}

// ═════════════════════════════════════════════════════════════════════
// Test 8 — feather produces intermediate alpha
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: feather gives intermediate alpha",
          "[ParametricMaskBuilder]") {
    // Pixel luminance ≈ 0.5. Range = [0.55, 1.0] with feather 0.2.
    // Pixel at 0.5 is in the ramp zone [0.55 - 0.2, 0.55] = [0.35, 0.55].
    // Expected alpha ≈ (0.5 - 0.35) / 0.2 = 0.75.
    ImageBuffer in = makeProPhotoBuffer(0.5F, 0.5F, 0.5F, 2, 2);
    ParametricMaskParams p;
    p.lumaMin = 0.55F;
    p.lumaMax = 1.0F;
    p.lumaFeather = 0.2F;
    p.hueWidth = 360.0F;

    MaskLayer mask = ParametricMaskBuilder::build(in, p);
    // Alpha should be between 0 and 1, not at the extremes.
    REQUIRE(mask.alpha[0] > 0.1F);
    REQUIRE(mask.alpha[0] < 0.9F);
}

// ═════════════════════════════════════════════════════════════════════
// Test 9 — hue targeting: red pixel selected, blue pixel rejected
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: hueCenter=0 (red) selects red, rejects blue",
          "[ParametricMaskBuilder]") {
    ParametricMaskParams p;
    p.lumaMin = 0.0F;
    p.lumaMax = 1.0F;
    p.lumaFeather = 0.0F;
    p.hueCenter = 0.0F;   // Red.
    p.hueWidth = 30.0F;
    p.hueFeather = 0.0F;
    p.satMin = 0.0F;
    p.satMax = 1.0F;

    // Red pixel.
    ImageBuffer red = makeProPhotoBuffer(0.8F, 0.1F, 0.1F, 2, 2);
    MaskLayer redMask = ParametricMaskBuilder::build(red, p);
    REQUIRE(redMask.alpha[0] > 0.5F);

    // Blue pixel (hue ≈ 240°).
    ImageBuffer blue = makeProPhotoBuffer(0.1F, 0.1F, 0.8F, 2, 2);
    MaskLayer blueMask = ParametricMaskBuilder::build(blue, p);
    REQUIRE(blueMask.alpha[0] < 0.1F);
}

// ═════════════════════════════════════════════════════════════════════
// Test 10 — two layers in Multiply mode
// ═════════════════════════════════════════════════════════════════════

TEST_CASE("MaskComposite: two layers Multiply → alpha ≤ each",
          "[MaskCompositeStage]") {
    // Layer A: luma [0,1] → alpha=1.
    // Layer B: luma [0.8,1.0] → dark pixel alpha=0.
    // Multiply(1, 0) = 0.
    MaskCompositeStage stage;
    MaskCompositeParams params;
    params.layerCount = 2;

    params.layerEnabled[0] = true;
    params.layers[0].lumaMin = 0.0F;
    params.layers[0].lumaMax = 1.0F;
    params.layers[0].lumaFeather = 0.0F;
    params.layers[0].hueWidth = 360.0F;

    params.layerEnabled[1] = true;
    params.layerBlend[1]   = BlendMode::Multiply; // Explicitly set Multiply.
    params.layers[1].lumaMin     = 0.8F;
    params.layers[1].lumaMax     = 1.0F;
    params.layers[1].lumaFeather = 0.0F;
    params.layers[1].hueWidth    = 360.0F;

    params.exposureInMask = 2.0F;  // Strong effect — should be invisible if alpha≈0.

    stage.setParams(params);

    ImageBuffer in  = makeProPhotoBuffer(0.2F, 0.2F, 0.2F);
    ImageBuffer out = stage.process(in);

    // Dark pixel — layer B gives alpha≈0, so Multiply → alpha≈0.
    // Output should be ≈ input (no adjustment applied).
    REQUIRE_THAT(out.data[0], WithinAbs(0.2, 0.05));
}
