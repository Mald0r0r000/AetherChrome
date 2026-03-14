/// @file PreviewProvider.cpp
/// @brief Implementation of PreviewProvider — ImageBuffer → QImage conversion
///        with optional ARM NEON float-to-uint8 acceleration.

#include "PreviewProvider.hpp"

#include <algorithm>
#include <cmath>


#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AETHER_HAS_NEON 1
#else
#define AETHER_HAS_NEON 0
#endif

// ─────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────

PreviewProvider::PreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

// ─────────────────────────────────────────────────────────────────────
// requestImage — called by QML on the GUI thread
// ─────────────────────────────────────────────────────────────────────

QImage PreviewProvider::requestImage(const QString& /*id*/, QSize* size,
                                      const QSize& /*requestedSize*/) {
    QMutexLocker lock(&m_mutex);
    if (size) {
        *size = m_current.size();
    }
    return m_current;
}

// ─────────────────────────────────────────────────────────────────────
// updatePreview — called from the render thread
// ─────────────────────────────────────────────────────────────────────

void PreviewProvider::updatePreview(const aether::ImageBuffer& buf) {
    if (buf.width == 0 || buf.height == 0) return;

    const int w = static_cast<int>(buf.width);
    const int h = static_cast<int>(buf.height);
    QImage img(w, h, QImage::Format_RGB888);

    const float* src   = buf.ptr();
    const size_t px    = buf.pixelCount();

    // Convert float [0,1] RGB → uint8 RGB888.
    uint8_t* dst = img.bits();

    const size_t totalFloats = px * 3;
    for (size_t i = 0; i < totalFloats; ++i) {
        float v = std::clamp(src[i], 0.0F, 1.0F);
        dst[i] = static_cast<uint8_t>(
            std::pow(v, 1.0F / 2.2F) * 255.0F + 0.5F);
    }



    QMutexLocker lock(&m_mutex);
    m_current = std::move(img);
}
