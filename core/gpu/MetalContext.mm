/// @file MetalContext.mm
/// @brief Objective-C++ implementation of the MetalContext wrapper.
/// NOTE: METAL_STUB mode is active since the Xcode toolchain is incomplete.

#include "MetalContext.h"
#include <iostream>

namespace aether {

struct MetalContext::Impl {
    // Empty stub implementation
};

// ─────────────────────────────────────────────────────────────────────
// Singleton & Lifecycle
// ─────────────────────────────────────────────────────────────────────

MetalContext& MetalContext::shared() {
    static MetalContext instance;
    return instance;
}

MetalContext::MetalContext() : m_impl(std::make_unique<Impl>()) {}

MetalContext::~MetalContext() = default;

bool MetalContext::isAvailable() const noexcept {
    // METAL_STUB forces false, falling back to CPU gracefully.
    return false;
}

// ─────────────────────────────────────────────────────────────────────
// Textures (No-Ops)
// ─────────────────────────────────────────────────────────────────────

void* MetalContext::uploadTexture(const ImageBuffer&) {
    return nullptr;
}

ImageBuffer MetalContext::downloadTexture(void*, ColorSpace) {
    return ImageBuffer();
}

void MetalContext::releaseTexture(void*) {}

// ─────────────────────────────────────────────────────────────────────
// Dispatch Kernel (No-Ops)
// ─────────────────────────────────────────────────────────────────────

void MetalContext::dispatchKernel(const std::string&,
                                  void*,
                                  void*,
                                  const void*,
                                  size_t) {}

} // namespace aether
