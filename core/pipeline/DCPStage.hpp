#pragma once

#include "../IPipelineStage.hpp"
#include "../ImageBuffer.hpp"
#include "../dcp/DCPProfile.hpp"
#include <optional>
#include <filesystem> // Ensure <filesystem> is included as it was in the original and might be needed by other includes.
#include "../Types.hpp" // Assuming DCPParams is now defined here.

namespace aether {

/// @brief Stage appliquant un profil DCP Adobe.
/// Remplace ColorMatrixStage par une approche plus précise basée sur les matrices
/// d'illuminants et la HueSatMap.
class DCPStage : public IPipelineStage {
public:
    DCPStage() = default;

    void setParams(const StageParams& p) override {
        if (auto* dcp = std::get_if<DCPParams>(&p)) {
            if (dcp->profilePath != m_params.profilePath) {
                m_profile.reset();
                auto res = DCPProfile::load(std::filesystem::path(dcp->profilePath));
                if (res) m_profile = std::move(*res);
            }
            m_params = *dcp;
        }
    }

    [[nodiscard]] StageParams getParams() const override {
        return m_params;
    }

    [[nodiscard]] StageId id() const noexcept override {
        return StageId::ColorMatrix;
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Adobe Digital Camera Profile (DCP)";
    }

    [[nodiscard]] ImageBuffer process(const ImageBuffer& in) override;

private:
    DCPParams m_params;
    std::optional<DCPProfile> m_profile;
};

} // namespace aether
