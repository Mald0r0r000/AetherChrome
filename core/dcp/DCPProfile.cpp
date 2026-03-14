#include "DCPProfile.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace aether {

// ─────────────────────────────────────────────────────────────────────
// TIFF / DCP tag constants
// ─────────────────────────────────────────────────────────────────────

static constexpr uint16_t TIFF_BYTE_ORDER_LE    = 0x4949;
static constexpr uint16_t TIFF_BYTE_ORDER_BE    = 0x4D4D;
static constexpr uint16_t TIFF_MAGIC            = 42;
static constexpr uint16_t DNG_MAGIC             = 0x4352; // 'CR'

// DCP Tags
static constexpr uint16_t TAG_CALIBRATION_ILLUMINANT1 = 50778;
static constexpr uint16_t TAG_CALIBRATION_ILLUMINANT2 = 50779;
static constexpr uint16_t TAG_COLOR_MATRIX1           = 50721;
static constexpr uint16_t TAG_COLOR_MATRIX2           = 50722;
static constexpr uint16_t TAG_HUESATMAP_DIMS          = 50937;
static constexpr uint16_t TAG_HUESATMAP_DATA1         = 50938;
static constexpr uint16_t TAG_HUESATMAP_DATA2         = 50939;
static constexpr uint16_t TAG_PROFILE_TONE_CURVE      = 50940;

// TIFF types
static constexpr uint16_t TIFF_SHORT    = 3;
static constexpr uint16_t TIFF_LONG     = 4;
static constexpr uint16_t TIFF_RATIONAL = 5;
static constexpr uint16_t TIFF_SRATIONAL= 10;
static constexpr uint16_t TIFF_FLOAT    = 11;

// ─────────────────────────────────────────────────────────────────────
// Minimal TIFF reader
// ─────────────────────────────────────────────────────────────────────

struct TIFFReader {
    std::vector<uint8_t> data;
    bool littleEndian = true;

    bool load(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        size_t size = f.tellg();
        f.seekg(0);
        data.resize(size);
        f.read(reinterpret_cast<char*>(data.data()), size);
        return f.good();
    }

    template<typename T>
    T read(size_t offset) const {
        if (offset + sizeof(T) > data.size()) return T{};
        T val;
        std::memcpy(&val, data.data() + offset, sizeof(T));
        if (!littleEndian && sizeof(T) > 1) {
            // Byte swap pour big endian
            uint8_t* p = reinterpret_cast<uint8_t*>(&val);
            std::reverse(p, p + sizeof(T));
        }
        return val;
    }

    uint16_t u16(size_t off) const { return read<uint16_t>(off); }
    uint32_t u32(size_t off) const { return read<uint32_t>(off); }
    int32_t  i32(size_t off) const { return read<int32_t>(off); }
    float    f32(size_t off) const { return read<float>(off); }

    float rational(size_t off) const {
        int32_t num = i32(off);
        int32_t den = i32(off + 4);
        if (den == 0) return 0.0f;
        return static_cast<float>(num) / static_cast<float>(den);
    }
};

// ─────────────────────────────────────────────────────────────────────
// load
// ─────────────────────────────────────────────────────────────────────

std::expected<DCPProfile, std::string>
DCPProfile::load(const std::filesystem::path& path) {

    TIFFReader tiff;
    if (!tiff.load(path)) {
        return std::unexpected("Cannot read file: " + path.string());
    }

    if (tiff.data.size() < 8) {
        return std::unexpected("File too small");
    }

    // ── Byte order ───────────────────────────────────────────────────
    uint16_t byteOrder = tiff.u16(0);
    if (byteOrder == TIFF_BYTE_ORDER_LE) {
        tiff.littleEndian = true;
    } else if (byteOrder == TIFF_BYTE_ORDER_BE) {
        tiff.littleEndian = false;
    } else {
        return std::unexpected("Not a TIFF file");
    }

    // ── IFD offset ───────────────────────────────────────────────────
    uint32_t ifdOffset = tiff.u32(4);
    if (ifdOffset >= tiff.data.size()) {
        return std::unexpected("Invalid IFD offset");
    }

    // ── Parse IFD entries ────────────────────────────────────────────
    uint16_t entryCount = tiff.u16(ifdOffset);
    size_t entryBase = ifdOffset + 2;

    DCPProfile profile;

    for (uint16_t i = 0; i < entryCount; ++i) {
        size_t e = entryBase + i * 12;
        if (e + 12 > tiff.data.size()) break;

        uint16_t tag    = tiff.u16(e + 0);
        uint16_t type   = tiff.u16(e + 2);
        uint32_t count  = tiff.u32(e + 4);
        uint32_t offset = tiff.u32(e + 8);

        // Pour les petites valeurs, l'offset est la valeur elle-même
        auto getOffset = [&]() -> size_t {
            // Si les données tiennent dans 4 bytes, elles sont inline
            size_t typeSize = 0;
            switch(type) {
                case TIFF_SHORT:     typeSize = 2; break;
                case TIFF_LONG:      typeSize = 4; break;
                case TIFF_RATIONAL:
                case TIFF_SRATIONAL: typeSize = 8; break;
                case TIFF_FLOAT:     typeSize = 4; break;
                default:             typeSize = 1; break;
            }
            if (typeSize * count <= 4)
                return e + 8;  // inline
            return offset;     // pointeur vers données
        };

        switch (tag) {

        case TAG_CALIBRATION_ILLUMINANT1:
            profile.illuminant1 = tiff.u16(e + 8);
            break;

        case TAG_CALIBRATION_ILLUMINANT2:
            profile.illuminant2 = tiff.u16(e + 8);
            break;

        case TAG_COLOR_MATRIX1: {
            // 9 SRational = 9 paires int32/int32
            if (count != 9) break;
            size_t off = getOffset();
            for (int j = 0; j < 9; ++j)
                profile.colorMatrix1[j] = tiff.rational(off + j * 8);
            break;
        }

        case TAG_COLOR_MATRIX2: {
            if (count != 9) break;
            size_t off = getOffset();
            for (int j = 0; j < 9; ++j)
                profile.colorMatrix2[j] = tiff.rational(off + j * 8);
            profile.hasColorMatrix2 = true;
            break;
        }

        case TAG_HUESATMAP_DIMS: {
            // 3 LONG : hueDivisions, satDivisions, valDivisions
            if (count != 3) break;
            size_t off = getOffset();
            profile.hueDivisions = tiff.u32(off + 0);
            profile.satDivisions = tiff.u32(off + 4);
            profile.valDivisions = tiff.u32(off + 8);
            break;
        }

        case TAG_HUESATMAP_DATA1: {
            // count FLOAT : hueDivisions * satDivisions * valDivisions * 3
            size_t off = getOffset();
            size_t entries = count / 3;
            profile.hsvMap1.resize(entries);
            for (size_t j = 0; j < entries; ++j) {
                profile.hsvMap1[j].hueShift = tiff.f32(off + j * 12 + 0);
                profile.hsvMap1[j].satScale = tiff.f32(off + j * 12 + 4);
                profile.hsvMap1[j].valScale = tiff.f32(off + j * 12 + 8);
            }
            profile.hasHueSatMap = true;
            break;
        }

        case TAG_HUESATMAP_DATA2: {
            size_t off = getOffset();
            size_t entries = count / 3;
            profile.hsvMap2.resize(entries);
            for (size_t j = 0; j < entries; ++j) {
                profile.hsvMap2[j].hueShift = tiff.f32(off + j * 12 + 0);
                profile.hsvMap2[j].satScale = tiff.f32(off + j * 12 + 4);
                profile.hsvMap2[j].valScale = tiff.f32(off + j * 12 + 8);
            }
            break;
        }

        default:
            break;
        }
    }

    // Validation
    bool hasMatrix1 = false;
    for (float v : profile.colorMatrix1)
        if (std::abs(v) > 1e-6f) { hasMatrix1 = true; break; }
    if (!hasMatrix1)
        return std::unexpected("No ColorMatrix1 found in DCP");

    std::cerr << "[DCPProfile] Loaded: " << path.filename().string() << "\n";
    std::cerr << "[DCPProfile] ColorMatrix1 (Cam->XYZ):\n";
    for (int r = 0; r < 3; ++r)
        std::cerr << "  " << profile.colorMatrix1[r*3+0] << "  "
                  << profile.colorMatrix1[r*3+1] << "  "
                  << profile.colorMatrix1[r*3+2] << "\n";
    if (profile.hasColorMatrix2) {
        std::cerr << "[DCPProfile] ColorMatrix2 (Cam->XYZ D65):\n";
        for (int r = 0; r < 3; ++r)
            std::cerr << "  " << profile.colorMatrix2[r*3+0] << "  "
                      << profile.colorMatrix2[r*3+1] << "  "
                      << profile.colorMatrix2[r*3+2] << "\n";
    }
    std::cerr << "[DCPProfile] HueSatMap: " 
              << (profile.hasHueSatMap ? "yes" : "no")
              << " dims=" << profile.hueDivisions 
              << "x" << profile.satDivisions 
              << "x" << profile.valDivisions << "\n";

    return profile;
}

// ─────────────────────────────────────────────────────────────────────
// interpolateMatrix
// ─────────────────────────────────────────────────────────────────────

std::array<float, 9>
DCPProfile::interpolateMatrix(float kelvin) const noexcept {

    if (!hasColorMatrix2) return colorMatrix1;

    // Températures de référence des illuminants
    float t1 = (illuminant1 == 17) ? 2856.0f : 6504.0f;  // StdA ou D65
    float t2 = (illuminant2 == 21) ? 6504.0f : 2856.0f;

    // Interpolation en inverse de température (comme DNG spec)
    float blend = 0.0f;
    if (std::abs(t2 - t1) > 1.0f) {
        float invT  = 1.0f / kelvin;
        float invT1 = 1.0f / t1;
        float invT2 = 1.0f / t2;
        blend = std::clamp((invT - invT1) / (invT2 - invT1), 0.0f, 1.0f);
    }

    std::array<float, 9> result{};
    for (int i = 0; i < 9; ++i)
        result[i] = colorMatrix1[i] * (1.0f - blend) + colorMatrix2[i] * blend;

    return result;
}

// ─────────────────────────────────────────────────────────────────────
// applyHueSatMap
// ─────────────────────────────────────────────────────────────────────

void DCPProfile::applyHueSatMap(float& h, float& s, float& v,
                                 float blend) const noexcept {
    if (!hasHueSatMap || hueDivisions == 0 || satDivisions == 0) return;

    // Choisir la map selon blend
    const auto& map = (blend < 0.5f || hsvMap2.empty()) ? hsvMap1 : hsvMap2;
    if (map.empty()) return;

    // Normaliser h dans [0, hueDivisions)
    float hNorm = h / 360.0f * static_cast<float>(hueDivisions);
    float sNorm = s * static_cast<float>(satDivisions - 1);
    float vNorm = v * static_cast<float>(valDivisions > 1 ? valDivisions - 1 : 1);

    // Index et fraction pour interpolation trilinéaire
    int hi = static_cast<int>(hNorm) % hueDivisions;
    int si = std::clamp(static_cast<int>(sNorm), 0, (int)satDivisions - 2);
    int vi = std::clamp(static_cast<int>(vNorm), 0, 
                        (int)(valDivisions > 1 ? valDivisions - 2 : 0));

    float hf = hNorm - std::floor(hNorm);
    float sf = sNorm - static_cast<float>(si);
    float vf = (valDivisions > 1) ? (vNorm - static_cast<float>(vi)) : 0.0f;

    int hi2 = (hi + 1) % hueDivisions;
    int si2 = std::min(si + 1, (int)satDivisions - 1);
    int vi2 = std::min(vi + 1, (int)(valDivisions > 1 ? valDivisions - 1 : 0));

    auto idx = [&](int h_, int s_, int v_) -> size_t {
        return static_cast<size_t>(
            v_ * hueDivisions * satDivisions +
            h_ * satDivisions + s_);
    };

    auto safeGet = [&](size_t i) -> const HSVDelta& {
        static const HSVDelta identity{};
        return (i < map.size()) ? map[i] : identity;
    };

    // Trilinear interpolation
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    auto interpDelta = [&](auto getter) -> float {
        float v000 = getter(safeGet(idx(hi,  si,  vi )));
        float v100 = getter(safeGet(idx(hi2, si,  vi )));
        float v010 = getter(safeGet(idx(hi,  si2, vi )));
        float v110 = getter(safeGet(idx(hi2, si2, vi )));
        float v001 = getter(safeGet(idx(hi,  si,  vi2)));
        float v101 = getter(safeGet(idx(hi2, si,  vi2)));
        float v011 = getter(safeGet(idx(hi,  si2, vi2)));
        float v111 = getter(safeGet(idx(hi2, si2, vi2)));

        float c00 = lerp(v000, v100, hf);
        float c10 = lerp(v010, v110, hf);
        float c01 = lerp(v001, v101, hf);
        float c11 = lerp(v011, v111, hf);
        float c0  = lerp(c00,  c10,  sf);
        float c1  = lerp(c01,  c11,  sf);
        return lerp(c0, c1, vf);
    };

    float dH = interpDelta([](const HSVDelta& d){ return d.hueShift; });
    float dS = interpDelta([](const HSVDelta& d){ return d.satScale;  });
    float dV = interpDelta([](const HSVDelta& d){ return d.valScale;  });

    h = std::fmod(h + dH + 360.0f, 360.0f);
    s = std::clamp(s * dS, 0.0f, 1.0f);
    v = std::clamp(v * dV, 0.0f, 1.0f);
}

} // namespace aether
