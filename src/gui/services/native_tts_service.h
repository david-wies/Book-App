#pragma once

#include "tts_service.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <atomic>
#include <memory>

class QTimer;

namespace bookhub::gui {

class NativeTTSService final : public TTSService
{
    Q_OBJECT
public:
    explicit NativeTTSService(QObject *parent = nullptr);
    ~NativeTTSService() override;

    void generatePreview(int voiceId, const QString &voiceName, const QString &text) override;
    void generateAudiobook(int voiceId,
                           const QString &voiceName,
                           const QString &text,
                           const QString &outputPath) override;
    void cancel() override;

private:
    static constexpr int kPreviewDurationMs = 10000;
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
