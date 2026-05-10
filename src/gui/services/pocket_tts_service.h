#pragma once

#include "tts_service.h"

#include <QString>

namespace bookhub::gui {

// TTS service backed by PocketTTS.cpp (MIT) for zero-shot voice cloning.
//
// Clones the user's voice from a short reference audio sample. Supported
// only for a subset of languages; unsupported languages emit failure.
//
// Five shared ONNX model files must be present at:
//   QStandardPaths::AppDataLocation/models/pocket-tts/
// These are managed by ModelManager (Task 22).
//
// Build with -DENABLE_POCKET_TTS=ON to link PocketTTS.cpp and enable real
// synthesis. Without it the service degrades gracefully to a failure signal.
//
// NOTE: GPU acceleration — inference runs on CPU by default. Enabling CUDA
// requires (a) a CUDA-enabled ONNX Runtime build and (b) passing
// OrtCUDAProviderOptions when constructing the inference session. Wire
// this in during Task 22; a GPU present on the host is not sufficient alone.

class PocketTTSService final : public TTSService
{
    Q_OBJECT
public:
    // language:          BCP-47 language tag for the book (e.g. "en", "fr").
    // referenceAudioPath: path to the user's uploaded .wav/.mp3/.flac sample.
    explicit PocketTTSService(const QString &language,
                              QString referenceAudioPath,
                              QObject *parent = nullptr);
    ~PocketTTSService() override;

    // Language tags supported by PocketTTS.cpp voice cloning.
    static bool languageSupported(const QString &language) noexcept;

    // True when the PocketTTS.cpp library was compiled in.
    static bool libraryAvailable() noexcept;

    // True when the shared ONNX model files exist on disk.
    static bool modelsAvailable();

    // Root directory for the shared PocketTTS model files.
    static QString modelDir();

    void generatePreview(int voiceId, const QString &voiceName, const QString &text) override;
    void generateAudiobook(int voiceId,
                           const QString &voiceName,
                           const QString &text,
                           const QString &outputPath) override;
    void cancel() override;

private:
    // Returns false and emits the appropriate failure signal when the service
    // is not ready (unsupported language, missing models, or library absent).
    bool checkAvailabilityAndEmitFailure(int voiceId, bool isPreview);

    QString m_language;
    QString m_referenceAudioPath;
    // Owned timer for async generation progress ticks. Null when idle.
    // Created in generateAudiobook() once real synthesis is wired in (Task 22).
    QTimer *m_generationTimer{};
};

} // namespace bookhub::gui
