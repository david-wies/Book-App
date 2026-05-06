#pragma once

#include "tts_types.h"
#include <QObject>
#include <QList>
#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// TTSService — abstract interface for text-to-speech synthesis.
//
// Concrete implementations handle voice synthesis, audio generation, and
// file output. MVP version is a no-op stub; Phase 3+ will integrate with
// Sherpa-ONNX or PocketTTS.cpp for real TTS.
// ---------------------------------------------------------------------------

class TTSService : public QObject {
    Q_OBJECT
public:
    explicit TTSService(QObject *parent = nullptr);
    virtual ~TTSService() = default;

    // Generate preview audio snippet for a voice (non-blocking)
    // Emits previewGenerated(voiceId, audioData) on completion
    virtual void generatePreview(int voiceId, const QString &voiceName, const QString &text) = 0;

    // Generate full audiobook audio from text and save to outputPath (non-blocking)
    // Emits generationProgress(percent) during generation
    // Emits generationCompleted(success, outputPath) on completion
    virtual void generateAudiobook(int voiceId, const QString &voiceName, const QString &text,
                                   const QString &outputPath) = 0;

signals:
    // Preview generation completed with audio bytes
    void previewGenerated(int voiceId, const QByteArray &audioData);

    // Audiobook generation progress and completion
    void generationProgress(int percent);
    void generationCompleted(bool success, const QString &outputPath);
};

} // namespace bookhub::gui
