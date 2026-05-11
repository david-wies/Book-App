#pragma once

#include "tts_types.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QTimer;

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// TTSService — abstract interface for text-to-speech synthesis.
//
// Concrete implementations handle voice synthesis, audio generation, and
// file output. The default implementation is a small native WAV synthesizer
// for the preset MVP voices; it keeps the flow functional without pulling in
// an external TTS runtime before model packaging is ready.
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

class NativeTTSService final : public TTSService
{
    Q_OBJECT
public:
    explicit NativeTTSService(QObject *parent = nullptr);
    ~NativeTTSService() override { cancel(); }

    void generatePreview(int voiceId, const QString &voiceName, const QString &text) override;
    void generateAudiobook(int voiceId,
                           const QString &voiceName,
                           const QString &text,
                           const QString &outputPath) override;
    void cancel() override;

private:
    static constexpr int kPreviewDurationMs = 4000;
    struct VoiceProfile
    {
        double baseFrequency{180.0};
        double cadence{8.0};
        double brightness{0.35};
    };

    static VoiceProfile profileForVoice(int voiceId, const QString &voiceName);
    static QByteArray synthesizeWav(const QString &text,
                                    const VoiceProfile &profile,
                                    int durationMs);
    // tag must be exactly 4 characters (FourCC). Asserted in debug builds.
    static void appendFourCC(QByteArray &data, QByteArrayView tag);
    static void appendUInt16LE(QByteArray &data, quint16 value);
    static void appendUInt32LE(QByteArray &data, quint32 value);

    // Owned progress-animation timer. Runs on the GUI thread while the thread-pool
    // synthesis task executes. Null when no generation is in progress.
    QTimer *m_generationTimer{};
    int m_generationStep{0};
    // Shared cancel flag between the GUI thread (cancel/generate calls) and the
    // QThreadPool task (synthesis + file write). Written on the GUI thread,
    // read on the pool thread — std::atomic guarantees the cross-thread read.
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
};

} // namespace bookhub::gui
