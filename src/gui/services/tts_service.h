#pragma once

#include "tts_types.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// TTSService — abstract interface for text-to-speech synthesis.
//
// Concrete implementations handle voice synthesis, audio generation, and
// file output. The default implementation (NativeTTSService) is a small
// sinusoidal WAV synthesizer that keeps the flow functional without pulling
// in an external TTS runtime before model packaging is ready.
// ---------------------------------------------------------------------------

class TTSService : public QObject
{
    Q_OBJECT
public:
    explicit TTSService(QObject *parent = nullptr) : QObject(parent) {}
    // Defined out-of-line in tts_service.cpp so the vtable has a single anchor.
    ~TTSService() override;

    // Generate preview audio snippet for a voice (non-blocking)
    // Emits previewGenerated(voiceId, audioData) on completion
    virtual void generatePreview(int voiceId, const QString &voiceName, const QString &text) = 0;

    // Generate full audiobook audio from text and save to outputPath (non-blocking)
    // Emits generationProgress(percent) during generation
    // Emits generationCompleted(success, outputPath) on completion
    virtual void generateAudiobook(int voiceId,
                                   const QString &voiceName,
                                   const QString &text,
                                   const QString &outputPath) = 0;
    virtual void cancel() {}

signals:
    // Preview generation completed with audio bytes
    void previewGenerated(int voiceId, const QByteArray &audioData);

    // Audiobook generation progress and completion
    void generationProgress(int percent);
    void generationCompleted(bool success, const QString &outputPath);
};

} // namespace bookhub::gui
