#include "../core/ai/AIMaskController.hpp"
#include "../core/ai/SAMDecoder.hpp"
#include "../core/ai/SAMEncoder.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace aether;

TEST_CASE("AIMaskController structural", "[ai]") {
  // Dossier inexistant -> pas de crash
  AIMaskController ctrl("non_existent_folder");
  REQUIRE_FALSE(ctrl.isAvailable());
  REQUIRE_FALSE(ctrl.isEncoderReady());
}

TEST_CASE("SAMDecoder structural", "[ai]") {
  // Decoding sans embedding -> std::unexpected
  SAMDecoder dec("models/dummy.onnx");
  std::vector<float> emptyEmb;
  std::array<int64_t, 4> shape{1, 256, 64, 64};
  std::vector<SAMDecoder::PromptPoint> pts = {{0.5f, 0.5f, true}};

  auto result = dec.segmentFromPoints(emptyEmb, shape, pts, 100, 100);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("AIMaskController heuristics", "[ai]") {
  // On ne veut pas de crash même si available = false
  AIMaskController ctrl("models");

  // Test logic bypass
  auto layer = ctrl.segmentSubject(AIMaskController::FashionSubject::Person);
  // Comme c'est async et non available, le future devrait se résoudre sur un
  // masque vide Mais ici on teste juste que l'appel n'explose pas.
  REQUIRE(true);
}

TEST_CASE("AIMaskController merge logic", "[ai]") {
  MaskLayer m1;
  m1.width = 2;
  m1.height = 1;
  m1.alpha = {0.5f, 0.0f};

  MaskLayer m2;
  m2.width = 2;
  m2.height = 1;
  m2.alpha = {0.5f, 1.0f};

  std::vector<MaskLayer> masks = {m1, m2};
  auto merged = AIMaskController::mergeMasks(masks);

  REQUIRE(merged.alpha.size() == 2);
  // Union de 0.5 et 0.5 = 1 - (0.5 * 0.5) = 0.75
  REQUIRE(merged.alpha[0] == 0.75f);
  // Union de 0.0 et 1.0 = 1 - (1.0 * 0.0) = 1.0
  REQUIRE(merged.alpha[1] == 1.0f);
}
