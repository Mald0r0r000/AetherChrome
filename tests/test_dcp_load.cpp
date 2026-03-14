#include "../core/dcp/DCPProfile.hpp"
#include <iostream>

int main() {
    auto result = aether::DCPProfile::load(
        "/Library/Application Support/Adobe/CameraRaw/CameraProfiles/"
        "Adobe Standard/Nikon D850 Adobe Standard.dcp");

    if (!result) {
        std::cerr << "ERREUR: " << result.error() << "\n";
        return 1;
    }
    std::cout << "OK — profil chargé\n";
    return 0;
}
