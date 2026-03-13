#pragma once

/// @file PreviewProvider.hpp
/// @brief QQuickImageProvider that serves the latest preview render to QML.

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include "../../core/ImageBuffer.hpp"

/// @brief Thread-safe image provider for QML `Image { source: "image://preview/..." }`.
///
/// PipelineBridge pushes the latest `ImageBuffer` via `updatePreview()`.
/// QML requests the image via `requestImage()` on the GUI thread.
class PreviewProvider : public QQuickImageProvider {
public:
    PreviewProvider();

    /// @brief Called by the QML engine to fetch the current preview.
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

    /// @brief Push a new preview buffer (called from the render thread).
    void updatePreview(const aether::ImageBuffer& buf);

private:
    QImage       m_current;
    mutable QMutex m_mutex;
};
