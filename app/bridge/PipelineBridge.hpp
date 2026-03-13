#pragma once

/// @file PipelineBridge.hpp
/// @brief QObject wrapper exposing PipelineEngine to QML.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QImage> // Added
#include <memory> // Added
#include <vector> // Added

#include <stop_token>

#include "core/PipelineEngine.hpp"
#include "core/ai/AIMaskController.hpp"
#include "core/ImageBuffer.hpp"

namespace aether {
    class PipelineEngine;
    class AIMaskController;
} // Corrected namespace block

class PreviewProvider;   // forward declaration

/// @brief QObject bridge between the aether::PipelineEngine and QML.
///
/// Exposes pipeline parameters as Q_PROPERTYs that QML can bind to.
/// Each property setter pushes parameters into the engine and schedules
/// an 80 ms debounced preview refresh.
class PipelineBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool    ready            READ isReady           NOTIFY readyChanged)
    Q_PROPERTY(bool    aiAvailable      READ isAIAvailable     NOTIFY aiAvailableChanged) // Added
    Q_PROPERTY(bool    aiEncoding       READ isAIEncoding      NOTIFY aiEncodingChanged) // Added
    Q_PROPERTY(float   exposure         READ exposure          WRITE setExposure
                                                               NOTIFY exposureChanged)
    Q_PROPERTY(float   contrast         READ contrast          WRITE setContrast
                                                               NOTIFY contrastChanged)
    Q_PROPERTY(float   highlightComp    READ highlightComp     WRITE setHighlightComp
                                                               NOTIFY highlightCompChanged)
    Q_PROPERTY(float   shadowLift       READ shadowLift        WRITE setShadowLift
                                                               NOTIFY shadowLiftChanged)
    Q_PROPERTY(float   saturation       READ saturation        WRITE setSaturation
                                                               NOTIFY saturationChanged)
    Q_PROPERTY(int     toneOp           READ toneOp            WRITE setToneOp
                                                               NOTIFY toneOpChanged)
    Q_PROPERTY(float   illuminantBlend  READ illuminantBlend   WRITE setIlluminantBlend
                                                               NOTIFY illuminantBlendChanged)
    Q_PROPERTY(QString cameraInfo       READ cameraInfo        NOTIFY cameraInfoChanged) // Changed NOTIFY

public:
    explicit PipelineBridge(QObject* parent = nullptr);
    ~PipelineBridge() override;

    /// @brief Assign the image provider so we can push buffers to it.
    void setPreviewProvider(PreviewProvider* provider);

    // ── Q_INVOKABLEs (callable from QML) ─────────────────────────────

    /// @brief Load a RAW file from disk.
    Q_INVOKABLE void loadFile(const QString& path);

    /// @brief Request a preview render at the given dimensions.
    Q_INVOKABLE void requestPreview(int w = 1280, int h = 720);

    /// @brief Export the full-resolution render (placeholder — session 8).
    Q_INVOKABLE void exportFullRes(const QString& outputPath);

    Q_INVOKABLE void segmentAtPoint(float normX, float normY, bool positive = true); // Added
    Q_INVOKABLE void segmentSubject(int subjectIndex); // Added
    Q_INVOKABLE void clearAIMasks(); // Added
    Q_INVOKABLE void prepareAI();
    const aether::ImageBuffer& lastPreviewBuffer() const noexcept;

    // ── Getters ──────────────────────────────────────────────────────

    [[nodiscard]] bool    isReady()         const;
    [[nodiscard]] bool    isAIAvailable()   const; // Added
    [[nodiscard]] bool    isAIEncoding()    const; // Added
    [[nodiscard]] float   exposure()        const;
    [[nodiscard]] float   contrast()        const;
    [[nodiscard]] float   highlightComp()   const;
    [[nodiscard]] float   shadowLift()      const;
    [[nodiscard]] float   saturation()      const;
    [[nodiscard]] int     toneOp()          const;
    [[nodiscard]] float   illuminantBlend() const;
    [[nodiscard]] QString cameraInfo()      const;

    // ── Setters ──────────────────────────────────────────────────────

    void setExposure(float ev);
    void setContrast(float c);
    void setHighlightComp(float h);
    void setShadowLift(float s);
    void setSaturation(float s);
    void setToneOp(int op);
    void setIlluminantBlend(float b);

signals:
    void readyChanged();
    void exposureChanged();
    void contrastChanged();
    void highlightCompChanged();
    void shadowLiftChanged();
    void saturationChanged();
    void toneOpChanged();
    void illuminantBlendChanged();
    void previewReady();
    void cameraInfoChanged(); // Added
    void aiAvailableChanged(); // Added
    void aiEncodingChanged(); // Added
    void aiMaskReady(); // Added

private:
    void schedulePreviewUpdate();
    void triggerPreviewUpdate();

    bool                    m_aiEncoding = false;
    std::unique_ptr<aether::PipelineEngine>   m_engine; // Changed to unique_ptr
    std::unique_ptr<aether::AIMaskController> m_aiController; // Added
    aether::ImageBuffer m_lastPreview;
    PreviewProvider*        m_provider{nullptr};
    std::stop_source        m_cancelSource;
    QTimer*                 m_debounceTimer{nullptr};

    // Cached preview dimensions.
    int m_previewW{1920};
    int m_previewH{1280};

    // Local parameter mirrors.
    float m_exposure        = 0.0F;
    float m_contrast        = 1.0F;
    float m_highlightComp   = 0.0F;
    float m_shadowLift      = 0.0F;
    float m_blackPoint      = 0.0F;
    float m_saturation      = 1.0F;
    int   m_toneOp          = 1;     // FilmicS
    float m_illuminantBlend = 0.5F;   // Mid-point default
};
