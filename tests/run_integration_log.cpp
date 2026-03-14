#include "core/PipelineEngine.hpp"
#include <iostream>
#include <stop_token>

int main() {
    aether::PipelineEngine engine;
    std::filesystem::path nefPath = "/Users/antoinebedos/.gemini/antigravity/scratch/build/Testing/_APX4527 1.NEF";
    
    std::cout << "--- Starting Integration Test ---" << std::endl;
    engine.loadRaw(nefPath);
    
    if (engine.isReady()) {
        // Configure DCP Stage with hardcoded path for D850 Adobe Standard
        aether::DCPParams dcp;
        dcp.profilePath = "/Library/Application Support/Adobe/CameraRaw/CameraProfiles/Adobe Standard/Nikon D850 Adobe Standard.dcp";
        engine.updateParam(aether::StageId::ColorMatrix, dcp);

        std::cerr << "Engine ready, requesting preview..." << std::endl;
        std::stop_source ss;
        auto future = engine.requestPreview(1024, 768, ss.get_token());
        auto buffer = future.get();
        
        if (buffer.width > 0) {
            std::cout << "Preview generated successfully: " << buffer.width << "x" << buffer.height << std::endl;
        } else {
            std::cerr << "Preview generation failed!" << std::endl;
        }
    } else {
        std::cerr << "Failed to load RAW file!" << std::endl;
    }
    
    return 0;
}
