/// @file PipelineBridge.cpp
/// @brief Implementation of PipelineBridge — QML ↔ PipelineEngine adapter.

#include "core/PipelineEngine.hpp"
#include "core/pipeline/DCPStage.hpp"
#include "core/ai/AIMaskController.hpp"
#include "bridge/PipelineBridge.hpp"
#include "bridge/PreviewProvider.hpp"

#include <QFileInfo>
#include <QtConcurrent>

using namespace aether;

// ─────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────

PipelineBridge::PipelineBridge(QObject* parent)
    : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(80);  // 80 ms debounce.
    connect(m_debounceTimer, &QTimer::timeout, this, &PipelineBridge::triggerPreviewUpdate);

    m_aiController = std::make_unique<aether::AIMaskController>(std::filesystem::path("models"));
    m_engine = std::make_unique<aether::PipelineEngine>();
}

PipelineBridge::~PipelineBridge() = default;

void PipelineBridge::setPreviewProvider(PreviewProvider* provider) {
    m_provider = provider;
}

// ─────────────────────────────────────────────────────────────────────
// loadFile
// ─────────────────────────────────────────────────────────────────────

void PipelineBridge::loadFile(const QString& path) {
    QString localPath = path;
    if (localPath.startsWith("file://")) {
        localPath = QUrl(localPath).toLocalFile();
    }
 
    qDebug() << "[PipelineBridge] Requesting load for:" << localPath;
 
    (void)QtConcurrent::run([this, localPath]() {
        if (m_engine) {
            qDebug() << "[PipelineBridge] Starting m_engine->loadRaw in background...";
            m_engine->loadRaw(localPath.toStdString());
            qDebug() << "[PipelineBridge] m_engine->loadRaw finished.";
        }
 
        QMetaObject::invokeMethod(this, [this]() {
            if (m_engine && m_engine->isReady()) {
                qDebug() << "[PipelineBridge] Loader finished, engine is ready. Emitting signals.";
                
                // ── Construire le chemin DCP depuis les métadonnées ────────
                const auto& meta = m_engine->metadata();
                std::string make  = meta.cameraMake;
                std::string model = meta.cameraModel;
                while (!make.empty()  && (make.back()  == ' ' || make.back() == '\0')) make.pop_back();
                while (!model.empty() && (model.back() == ' ' || model.back() == '\0')) model.pop_back();
                
                m_dcpProfilePath = QString::fromStdString(
                    "/Library/Application Support/Adobe/CameraRaw/"
                    "CameraProfiles/Camera/" + make + " " + model +
                    "/" + make + " " + model + " Adobe Standard.dcp");
                
                qDebug() << "[PipelineBridge] DCP profile:" << m_dcpProfilePath;
                
                // Pousser les params DCP initiaux
                m_engine->updateParam(aether::StageId::ColorMatrix, buildDCPParams());

                clearAIMasks();
                emit cameraInfoChanged();
                emit readyChanged();
 
                qDebug() << "[PipelineBridge] Requesting initial preview.";
                requestPreview(m_previewW, m_previewH);
            } else {
                qDebug() << "[PipelineBridge] Loader finished, but engine is NOT ready.";
            }
        }, Qt::QueuedConnection);
    });
}

bool PipelineBridge::isAIAvailable() const {
    return m_aiController && m_aiController->isAvailable();
}

bool PipelineBridge::isAIEncoding() const {
    return m_aiEncoding;
}

void PipelineBridge::segmentAtPoint(float normX, float normY, bool positive) {
    if (!m_aiController || !m_aiController->isEncoderReady()) return;

    std::thread([this, normX, normY, positive]() {
        auto fut = m_aiController->segmentAtPoint(normX, normY, positive);
        auto mask = fut.get();
        
        QMetaObject::invokeMethod(this, [this, mask]() {
            aether::MaskCompositeParams params;
            params.aiMask = mask;
            params.aiMask.blend = aether::BlendMode::Normal;
            params.aiMask.opacity = 1.0f;
            params.hasAiMask = true;

            m_engine->updateParam(aether::StageId::MaskComposite, params);
            schedulePreviewUpdate();
            emit aiMaskReady();
        }, Qt::QueuedConnection);
    }).detach();
}

void PipelineBridge::segmentSubject(int subjectIndex) {
    if (!m_aiController || !m_aiController->isEncoderReady()) return;

    std::thread([this, subjectIndex]() {
        auto subj = static_cast<aether::AIMaskController::FashionSubject>(subjectIndex);
        auto fut = m_aiController->segmentSubject(subj);
        auto mask = fut.get();

        QMetaObject::invokeMethod(this, [this, mask]() {
            aether::MaskCompositeParams params;
            params.aiMask = mask;
            params.aiMask.blend = aether::BlendMode::Normal;
            params.hasAiMask = true;

            m_engine->updateParam(aether::StageId::MaskComposite, params);
            schedulePreviewUpdate();
            emit aiMaskReady();
        }, Qt::QueuedConnection);
    }).detach();
}

void PipelineBridge::clearAIMasks() {
    aether::MaskCompositeParams params; // Empty
    m_engine->updateParam(aether::StageId::MaskComposite, params);
    schedulePreviewUpdate();
    emit aiMaskReady();
}

const aether::ImageBuffer& PipelineBridge::lastPreviewBuffer() const noexcept {
    return m_lastPreview;
}

void PipelineBridge::prepareAI() {
    if (!m_aiController || !m_aiController->isAvailable()) return;
    if (m_aiEncoding) return;

    m_aiEncoding = true;
    emit aiEncodingChanged();

    (void)QtConcurrent::run([this]() {
        m_aiController->prepareImageAsync();
        QMetaObject::invokeMethod(this, [this]() {
            m_aiEncoding = false;
            emit aiEncodingChanged();
            emit aiAvailableChanged();
        }, Qt::QueuedConnection);
    });
}

// ─────────────────────────────────────────────────────────────────────
// requestPreview
// ─────────────────────────────────────────────────────────────────────

void PipelineBridge::requestPreview(int w, int h) {
    if (!m_engine || !m_engine->isReady()) return;

    m_previewW = w;
    m_previewH = h;

    // Cancel any in-flight render.
    m_cancelSource.request_stop();
    m_cancelSource = std::stop_source{};

    // Set half-size for fast preview if requested dimensions are small enough.
    // This acts as our "Preview Proxy" logic.
    RawIngestParams rip;
    rip.halfSize = (w < 2048 && h < 2048);
    m_engine->updateParam(StageId::RawIngest, rip);

    auto future = m_engine->requestPreview(
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h),
        m_cancelSource.get_token());

    // Move the future into an async watcher.
    (void)QtConcurrent::run([this, fut = std::move(future)]() mutable {
        ImageBuffer buf = fut.get();
        if (buf.width == 0) return;

        m_lastPreview = buf;
        if (m_provider) {
            m_provider->updatePreview(buf);
        }

        // Signal on the main thread.
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraInfoChanged();
            emit previewReady();
        }, Qt::QueuedConnection);
    });
}

// ─────────────────────────────────────────────────────────────────────
// exportFullRes (stub — session 8)
// ─────────────────────────────────────────────────────────────────────

void PipelineBridge::exportFullRes(const QString& /*outputPath*/) {
    // TODO: session 8 — full-res render + save to TIFF/JPEG.
}

// ─────────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────────

bool    PipelineBridge::isReady()         const { return m_engine && m_engine->isReady(); }
float   PipelineBridge::exposure()        const { return m_exposure; }
float   PipelineBridge::contrast()        const { return m_contrast; }
float   PipelineBridge::highlightComp()   const { return m_highlightComp; }
float   PipelineBridge::shadowLift()      const { return m_shadowLift; }
float   PipelineBridge::saturation()      const { return m_saturation; }
int     PipelineBridge::toneOp()          const { return m_toneOp; }
int     PipelineBridge::toneNorm()        const { return m_toneNorm; }
float   PipelineBridge::sigmoidSkew()     const { return m_sigmoidSkew; }
float    PipelineBridge::temperature()     const { return m_temperature; }
float    PipelineBridge::tint()            const { return m_tint; }
float    PipelineBridge::illuminantBlend() const { return m_illuminantBlend; }

QString PipelineBridge::cameraInfo() const {
    if (!m_engine || !m_engine->isReady()) return QStringLiteral("No file loaded");

    const auto& meta = m_engine->metadata();
    return QString::fromStdString(meta.cameraMake) + " " +
           QString::fromStdString(meta.cameraModel) +
           " | ISO " + QString::number(static_cast<int>(meta.isoValue)) +
           " | 1/" + QString::number(static_cast<int>(1.0F / meta.shutterSpeed)) + "s" +
           " | f/" + QString::number(static_cast<double>(meta.aperture), 'f', 1);
}

// ─────────────────────────────────────────────────────────────────────
// Setters — push params into engine + schedule debounced preview
// ─────────────────────────────────────────────────────────────────────

void PipelineBridge::setExposure(float ev) {
    if (qFuzzyCompare(m_exposure, ev)) return;
    m_exposure = ev;

    ExposureParams ep;
    ep.exposureEV    = m_exposure;
    ep.highlightComp = m_highlightComp;
    ep.shadowLift    = m_shadowLift;
    m_engine->updateParam(aether::StageId::Exposure, ep);

    emit exposureChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setContrast(float c) {
    if (qFuzzyCompare(m_contrast, c)) return;
    m_contrast = c;

    ToneMappingParams tp;
    tp.op          = static_cast<ToneMappingParams::Operator>(m_toneOp);
    tp.norm        = static_cast<ToneMappingParams::PreservationNorm>(m_toneNorm);
    tp.contrast    = m_contrast;
    tp.saturation  = m_saturation;
    tp.sigmoidSkew = m_sigmoidSkew;
    m_engine->updateParam(aether::StageId::ToneMapping, tp);

    emit contrastChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setHighlightComp(float h) {
    if (qFuzzyCompare(m_highlightComp, h)) return;
    m_highlightComp = h;

    ExposureParams ep;
    ep.exposureEV    = m_exposure;
    ep.highlightComp = m_highlightComp;
    ep.shadowLift    = m_shadowLift;
    m_engine->updateParam(aether::StageId::Exposure, ep);

    emit highlightCompChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setShadowLift(float s) {
    if (qFuzzyCompare(m_shadowLift, s)) return;
    m_shadowLift = s;

    ExposureParams ep;
    ep.exposureEV    = m_exposure;
    ep.highlightComp = m_highlightComp;
    ep.shadowLift    = m_shadowLift;
    m_engine->updateParam(aether::StageId::Exposure, ep);

    emit shadowLiftChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setSaturation(float s) {
    if (qFuzzyCompare(m_saturation, s)) return;
    m_saturation = s;

    ToneMappingParams tp;
    tp.op          = static_cast<ToneMappingParams::Operator>(m_toneOp);
    tp.norm        = static_cast<ToneMappingParams::PreservationNorm>(m_toneNorm);
    tp.contrast    = m_contrast;
    tp.saturation  = m_saturation;
    tp.sigmoidSkew = m_sigmoidSkew;
    m_engine->updateParam(aether::StageId::ToneMapping, tp);

    emit saturationChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setToneOp(int op) {
    if (m_toneOp == op) return;
    m_toneOp = op;

    ToneMappingParams tp;
    tp.op          = static_cast<ToneMappingParams::Operator>(m_toneOp);
    tp.norm        = static_cast<ToneMappingParams::PreservationNorm>(m_toneNorm);
    tp.contrast    = m_contrast;
    tp.saturation  = m_saturation;
    tp.sigmoidSkew = m_sigmoidSkew;
    m_engine->updateParam(aether::StageId::ToneMapping, tp);

    emit toneOpChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setToneNorm(int norm) {
    if (m_toneNorm == norm) return;
    m_toneNorm = norm;

    ToneMappingParams tp;
    tp.op          = static_cast<ToneMappingParams::Operator>(m_toneOp);
    tp.norm        = static_cast<ToneMappingParams::PreservationNorm>(m_toneNorm);
    tp.contrast    = m_contrast;
    tp.saturation  = m_saturation;
    tp.sigmoidSkew = m_sigmoidSkew;
    m_engine->updateParam(aether::StageId::ToneMapping, tp);

    emit toneNormChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setSigmoidSkew(float skew) {
    if (qFuzzyCompare(m_sigmoidSkew, skew)) return;
    m_sigmoidSkew = skew;

    ToneMappingParams tp;
    tp.op          = static_cast<ToneMappingParams::Operator>(m_toneOp);
    tp.norm        = static_cast<ToneMappingParams::PreservationNorm>(m_toneNorm);
    tp.contrast    = m_contrast;
    tp.saturation  = m_saturation;
    tp.sigmoidSkew = m_sigmoidSkew;
    m_engine->updateParam(aether::StageId::ToneMapping, tp);

    emit sigmoidSkewChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setTemperature(float t) {
    if (qFuzzyCompare(m_temperature, t)) return;
    m_temperature = t;
    m_engine->updateParam(aether::StageId::ColorMatrix, buildDCPParams());
    emit temperatureChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setTint(float t) {
    if (qFuzzyCompare(m_tint, t)) return;
    m_tint = t;
    m_engine->updateParam(aether::StageId::ColorMatrix, buildDCPParams());
    emit tintChanged();
    schedulePreviewUpdate();
}

void PipelineBridge::setIlluminantBlend(float b) {
    if (qFuzzyCompare(m_illuminantBlend, b)) return;
    m_illuminantBlend = b;
    // Simple linear interpolate between A (2856K) and D65 (6504K)
    m_temperature = 2856.0f + b * (6504.0f - 2856.0f);
    m_engine->updateParam(aether::StageId::ColorMatrix, buildDCPParams());
    
    emit illuminantBlendChanged();
    emit temperatureChanged();
    schedulePreviewUpdate();
}

aether::DCPParams PipelineBridge::buildDCPParams() const {
    aether::DCPParams dp;
    dp.temperature = m_temperature;
    dp.tint        = m_tint;
    dp.profilePath = m_dcpProfilePath.toStdString();
    dp.enableHueSatMap = true;
    return dp;
}

// ─────────────────────────────────────────────────────────────────────
// schedulePreviewUpdate — debounced
// ─────────────────────────────────────────────────────────────────────

void PipelineBridge::schedulePreviewUpdate() {
    m_debounceTimer->start();
}

void PipelineBridge::triggerPreviewUpdate() {
    requestPreview(m_previewW, m_previewH);
}
