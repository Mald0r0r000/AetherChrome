# AetherChrome

Scene-referred RAW developer for macOS (Apple Silicon).
Targets Capture One image quality with a cleaner UI.

## Dependencies

    brew install libraw little-cms2 qt onnxruntime catch2 cmake

## Build

    git clone https://github.com/yourname/AetherChrome
    cd AetherChrome
    cmake -B build -DAETHER_METAL_STUB=ON
    cmake --build build --parallel

## Run tests

    cd build && ctest --output-on-failure

## Run app

    ./build/AetherChrome.app/Contents/MacOS/AetherChrome

## AI Masking (optional)

Download SAM 2 models and place in models/ :
    models/sam2_hiera_tiny_encoder.onnx
    models/sam2_hiera_tiny_decoder.onnx

Source : https://huggingface.co/facebook/sam2-hiera-tiny

Rebuild with : cmake -B build -DAETHER_AI_ENABLED=ON

## Pipeline

    ARW → RawInputStage (LibRaw, linear)
        → ColorMatrixStage (Camera→ProPhoto, NEON)
        → ExposureStage (EV + highlights + shadows)
        → ToneMappingStage (Filmic S / ACES / Linear)
        → MaskCompositeStage (parametric + AI masks)
        → OutputTransformStage (ProPhoto→sRGB/P3, LittleCMS)

## Roadmap

    v0.1  Core pipeline + UI de base         ← Current
    v0.2  Metal GPU path (requires Xcode)
    v0.3  AMaZE/RCD demosaicing
    v0.4  Session management + export TIFF
    v0.5  Tethering libgphoto2
