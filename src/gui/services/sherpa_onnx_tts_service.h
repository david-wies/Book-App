#pragma once

#include "tts_service.h"

#include <QString>

namespace bookhub::gui {

// TTS service backed by Sherpa-ONNX (Apache 2.0) for preset voice synthesis.
//
// Preset voices use Piper-format .onnx + .json model files stored at:
//   QStandardPaths::AppDataLocation/models/sherpa/<voiceId>/model.onnx
//
// Model files are managed by ModelManager (Task 22). When a model is missing,
// generatePreview emits an empty QByteArray and generateAudiobook emits
// generationCompleted(false, {}) — the dialog's error state surfaces this to
// the user so they can trigger a download.
//
// Build with -DENABLE_SHERPA_ONNX=ON to link the sherpa-onnx library and
// enable real synthesis. Without it, the service degrades gracefully.
//
// NOTE: GPU acceleration — Task 22 selects the ONNX execution provider based
// on whichever backend was detected at configure time (BOOKHUB_HAVE_GPU is set
// whenever any backend is found):
//   BOOKHUB_HAVE_CUDA     → provider = "cuda"     (NVIDIA, Linux/Windows)
//   BOOKHUB_HAVE_COREML   → provider = "coreml"   (Apple, macOS)
//   BOOKHUB_HAVE_DIRECTML → provider = "dml"      (Windows, all GPU vendors)
//   BOOKHUB_HAVE_ROCM     → provider = "rocm"     (AMD, Linux)
//   BOOKHUB_HAVE_OPENVINO → provider = "openvino" (Intel iGPU, Linux)
//   (none)                → provider = "cpu"      (default)
// The sherpa-onnx library must itself be built with the matching backend;
// finding the toolkit is necessary but not sufficient for GPU inference.

class SherpaOnnxTTSService final : public TTSService
{
    Q_OBJECT
public:
    explicit SherpaOnnxTTSService(QObject *parent = nullptr);
    ~SherpaOnnxTTSService() override;

    // True when the sherpa-onnx library was compiled in (BOOKHUB_HAVE_SHERPA_ONNX defined).
    static bool libraryAvailable() noexcept;

    // Expected root directory for a voice's model files.
    static QString modelDir(int voiceId);

    // True when the model files for voiceId exist on disk.
    static bool modelAvailable(int voiceId);

    void generatePreview(int voiceId, const QString &voiceName, const QString &text) override;
    void generateAudiobook(int voiceId,
                           const QString &voiceName,
                           const QString &text,
                           const QString &outputPath) override;
    void cancel() override;

private:
    // Task 22: add m_generationTimer and m_cancelFlag here once real synthesis is wired in.
};

} // namespace bookhub::gui
