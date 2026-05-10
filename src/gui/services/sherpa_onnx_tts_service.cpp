#include "sherpa_onnx_tts_service.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#ifdef BOOKHUB_HAVE_SHERPA_ONNX
// When sherpa-onnx is compiled in, include its C++ API header.
// The actual synthesis implementation will be filled in once model
// packaging is available (Task 22).
// #include <sherpa-onnx/c-api/c-api.h>
#endif

namespace bookhub::gui {

SherpaOnnxTTSService::SherpaOnnxTTSService(QObject *parent)
    : TTSService(parent)
{
}

SherpaOnnxTTSService::~SherpaOnnxTTSService()
{
    cancel();
}

bool SherpaOnnxTTSService::libraryAvailable() noexcept
{
#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    return true;
#else
    return false;
#endif
}

QString SherpaOnnxTTSService::modelDir(int voiceId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/models/sherpa/%1").arg(voiceId);
}

bool SherpaOnnxTTSService::modelAvailable(int voiceId)
{
    return QFileInfo::exists(modelDir(voiceId) + QStringLiteral("/model.onnx"));
}

void SherpaOnnxTTSService::generatePreview(int voiceId, const QString &voiceName,
                                            const QString &text)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)

#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    if (modelAvailable(voiceId)) {
        // TODO (Task 22): load model from modelDir(voiceId) and synthesize preview WAV.
        // The sherpa-onnx C++ API call goes here once model packaging is in place.
        // Until then, fall through to the "not available" path below.
    }
#endif

    // Emit empty data so the dialog shows the preview button in a ready state
    // without audio. The user will see duration 0:00 and can retry after
    // downloading models via ModelManager (Task 22).
    QTimer::singleShot(0, this, [this, voiceId] {
        emit previewGenerated(voiceId, QByteArray{});
    });
}

void SherpaOnnxTTSService::generateAudiobook(int voiceId, const QString &voiceName,
                                              const QString &text,
                                              const QString &outputPath)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)
    Q_UNUSED(outputPath)
    cancel();

#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    if (modelAvailable(voiceId)) {
        // TODO (Task 22): synthesize full audiobook using sherpa-onnx inference.
        // Report progress via generationProgress() and emit generationCompleted()
        // on finish. Until model packaging is in place, fall through below.
        Q_UNUSED(voiceId)
    }
#endif

    // Model not available — emit failure immediately so the dialog surfaces an error.
    QTimer::singleShot(0, this, [this] {
        emit generationCompleted(false, {});
    });
}

void SherpaOnnxTTSService::cancel()
{
    if (m_generationTimer) {
        m_generationTimer->stop();
        m_generationTimer->deleteLater();
        m_generationTimer = nullptr;
    }
}

} // namespace bookhub::gui
