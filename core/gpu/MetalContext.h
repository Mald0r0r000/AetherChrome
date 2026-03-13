#pragma once

/// @file MetalContext.h
/// @brief Pure C++ interface to Apple Metal hardware acceleration.

#include "../ImageBuffer.hpp"
#include <memory>
#include <string>

namespace aether {

/// @brief Singleton managing Metal device, queue, and library connection.
///
/// Hides Objective-C++ Metal APIs from the rest of the C++ pipeline.
class MetalContext {
public:
    /// @brief Global instance.
    static MetalContext& shared();

    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;

    /// @brief True if a Metal device is available and library loaded.
    [[nodiscard]] bool isAvailable() const noexcept;

    /// @brief Uploads a CPU ImageBuffer to an MTLTexture.
    /// @return Opaque handle to an id<MTLTexture>, or nullptr on failure.
    [[nodiscard]] void* uploadTexture(const ImageBuffer& buf);

    /// @brief Downloads an MTLTexture back to a CPU ImageBuffer.
    /// @param textureHandle Opaque handle created by uploadTexture.
    /// @param cs The logical color space for the resulting buffer.
    [[nodiscard]] ImageBuffer downloadTexture(void* textureHandle, ColorSpace cs);

    /// @brief Dispatches a compute kernel over an input and output texture.
    /// @param kernelName Function name inside the .metallib.
    /// @param inTexture Opaque handle to input id<MTLTexture>.
    /// @param outTexture Opaque handle to output id<MTLTexture>.
    /// @param paramsBuffer Pointer to a CPU struct of uniforms.
    /// @param paramsSize Size of the uniforms struct in bytes.
    void dispatchKernel(const std::string& kernelName,
                        void* inTexture,
                        void* outTexture,
                        const void* paramsBuffer,
                        size_t paramsSize);

    /// @brief Releases an opaque texture handle.
    void releaseTexture(void* handle);

private:
    MetalContext();
    ~MetalContext();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aether
