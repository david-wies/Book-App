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

SherpaOnnxTTSService::SherpaOnnxTTSService(QObject *parent) : TTSService(parent) {}

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
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/models/sherpa/%1").arg(voiceId);
}

bool SherpaOnnxTTSService::modelAvailable(int voiceId)
{
    return QFileInfo::exists(modelDir(voiceId) + QStringLiteral("/model.onnx"));
}

void SherpaOnnxTTSService::generatePreview(int voiceId,
                                           const QString &voiceName,
                                           const QString &text)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)

#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    if (modelAvailable(voiceId)) {
        // TODO (Task 22): load model from modelDir(voiceId) and synthesize preview WAV,
        // then emit previewGenerated(voiceId, wavBytes). Return here once done.
        // Until model packaging is in place, fall through to the failure path.
#ifdef BOOKHUB_HAVE_GPU
        // TODO (Task 22): set SherpaOnnxOfflineTtsConfig::provider based on backend:
        //   CUDA→"cuda"  CoreML→"coreml"  DirectML→"dml"  ROCm→"rocm"  OpenVINO→"openvino"
#endif
    }
#endif
    // Library not linked or model absent — emit empty data so the dialog can show
    // duration 0:00 and let the user retry after models are downloaded (Task 22).
    QTimer::singleShot(0, this, [this, voiceId] { emit previewGenerated(voiceId, QByteArray{}); });
}

void SherpaOnnxTTSService::generateAudiobook(int voiceId,
                                             const QString &voiceName,
                                             const QString &text,
                                             const QString &outputPath)
{
    Q_UNUSED(voiceName)
    Q_UNUSED(text)
    Q_UNUSED(outputPath)
    cancel();

#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    if (modelAvailable(voiceId)) {
        // TODO (Task 22): load model from modelDir(voiceId), synthesize the full audiobook,
        // report progress via generationProgress(), emit generationCompleted(true, outputPath),
        // and return. Until then fall through to the failure emit below.
#    ifdef BOOKHUB_HAVE_GPU
        // TODO (Task 22): set SherpaOnnxOfflineTtsConfig::provider based on backend:
        //   CUDA→"cuda"  CoreML→"coreml"  DirectML→"dml"  ROCm→"rocm"  OpenVINO→"openvino"
#    endif
    }
    // Model not downloaded yet (or synthesis not yet wired in) — fall through to failure.
#else
    Q_UNUSED(voiceId)
#endif

    // Library not linked or model absent — surface an error so the user can trigger a download.
    QTimer::singleShot(0, this, [this] { emit generationCompleted(false, {}); });
}

void SherpaOnnxTTSService::cancel()
{
    // Task 22: stop and null m_generationTimer here once real synthesis is wired in.
}

} // namespace bookhub::gui
