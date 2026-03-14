#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace aether {

/// @brief Profil de couleur Adobe DCP (Digital Camera Profile).
/// Parse les matrices ColorMatrix et la HueSatMap depuis un fichier .dcp.
struct DCPProfile {

    // ── Matrices couleur ─────────────────────────────────────────────
    // Camera RGB → XYZ, row-major 3×3, valeurs rationnelles Adobe.
    // colorMatrix1 = illuminant A (2856K)
    // colorMatrix2 = illuminant D65 (6504K)
    std::array<float, 9> colorMatrix1{};
    std::array<float, 9> colorMatrix2{};
    int illuminant1 = 17;   // 17 = Standard A
    int illuminant2 = 21;   // 21 = D65

    // ── HueSatMap ────────────────────────────────────────────────────
    struct HSVDelta {
        float hueShift   = 0.0f;  // degrés [-180, 180]
        float satScale   = 1.0f;  // multiplicateur [0, 4]
        float valScale   = 1.0f;  // multiplicateur [0, 4]
    };

    uint32_t hueDivisions = 0;
    uint32_t satDivisions = 0;
    uint32_t valDivisions = 0;

    std::vector<HSVDelta> hsvMap1;  // pour illuminant1
    std::vector<HSVDelta> hsvMap2;  // pour illuminant2

    bool hasColorMatrix2 = false;
    bool hasHueSatMap    = false;

    // ── Lookup ───────────────────────────────────────────────────────

    /// @brief Interpoler les deux matrices selon la température (Kelvin).
    /// Retourne la matrice Camera→XYZ adaptée à la température WB.
    [[nodiscard]] std::array<float, 9>
    interpolateMatrix(float kelvin) const noexcept;

    /// @brief Appliquer la HueSatMap à un pixel HSV.
    /// @param h  Hue [0, 360]
    /// @param s  Saturation [0, 1]
    /// @param v  Value [0, 1]
    /// @param blend  Interpolation entre map1 et map2 [0, 1]
    void applyHueSatMap(float& h, float& s, float& v,
                        float blend = 0.5f) const noexcept;

    // ── Chargement ───────────────────────────────────────────────────

    /// @brief Charger un fichier .dcp Adobe.
    [[nodiscard]] static std::expected<DCPProfile, std::string>
    load(const std::filesystem::path& path);
};

} // namespace aether
