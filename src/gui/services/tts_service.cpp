#include "tts_service.h"

#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bookhub::gui {

TTSService::~TTSService() = default;

NativeTTSService::NativeTTSService(QObject *parent) : TTSService(parent) {}

void NativeTTSService::generatePreview(int voiceId, const QString &voiceName, const QString &text)
{
    const VoiceProfile profile = profileForVoice(voiceId, voiceName);
    const QString sample = text.trimmed().isEmpty()
                               ? QStringLiteral("This is a short preview of %1.").arg(voiceName)
                               : text;

    // NOTE: synthesizeWav runs on the GUI thread. Fast enough for the sinusoidal
    // MVP (~88 K samples); move to QThreadPool when integrating Sherpa-ONNX.
    QTimer::singleShot(0, this, [this, voiceId, sample, profile] {
        emit previewGenerated(voiceId, synthesizeWav(sample, profile, kPreviewDurationMs));
    });
}

void NativeTTSService::generateAudiobook(int voiceId,
                                         const QString &voiceName,
                                         const QString &text,
                                         const QString &outputPath)
{
    cancel();

    const VoiceProfile profile = profileForVoice(voiceId, voiceName);
    const QString script =
        text.trimmed().isEmpty()
            ? QStringLiteral("BookHub audiobook generated with %1.").arg(voiceName)
            : text;

    m_generationStep = 0;
    m_generationTimer = new QTimer(this);
    m_generationTimer->setInterval(35);
    connect(m_generationTimer, &QTimer::timeout, this, [this, script, profile, outputPath] {
        m_generationStep += 10;
        emit generationProgress(std::min(m_generationStep, 90));

        if (m_generationStep < 100)
            return;

        m_generationTimer->stop();
        m_generationTimer->deleteLater();
        m_generationTimer = nullptr;

        QSaveFile file(outputPath);
        const int duration = std::clamp(static_cast<int>(script.size()) * 65, 7000, 45000);
        const QByteArray wav = synthesizeWav(script, profile, duration);
        const bool success =
            file.open(QIODevice::WriteOnly) && file.write(wav) == wav.size() && file.commit();

        if (success)
            emit generationProgress(100);
        emit generationCompleted(success, success ? outputPath : QString{});
    });
    m_generationTimer->start();
}

void NativeTTSService::cancel()
{
    if (m_generationTimer) {
        m_generationTimer->stop();
        m_generationTimer->deleteLater();
        m_generationTimer = nullptr;
    }
    m_generationStep = 0;
}

NativeTTSService::VoiceProfile NativeTTSService::profileForVoice(int voiceId,
                                                                 const QString &voiceName)
{
    const QString normalized = voiceName.toLower();
    if (normalized.contains(QStringLiteral("warm")))
        return {165.0, 6.4, 0.25};
    if (normalized.contains(QStringLiteral("crisp")))
        return {220.0, 10.2, 0.48};
    if (normalized.contains(QStringLiteral("classic")))
        return {185.0, 7.8, 0.34};

    const double offset = static_cast<double>((voiceId % 7) - 3) * 8.0;
    return {190.0 + offset, 7.5, 0.35};
}

QByteArray NativeTTSService::synthesizeWav(const QString &text,
                                           const VoiceProfile &profile,
                                           int durationMs)
{
    constexpr int kSampleRate = 22050;
    constexpr int kChannels = 1;
    constexpr int kBitsPerSample = 16;
    constexpr double kTwoPi = 6.28318530717958647692;

    const int sampleCount = std::max(1, (kSampleRate * durationMs) / 1000);
    QByteArray pcm;
    pcm.reserve(static_cast<qsizetype>(sampleCount) * 2);

    const QString source = text.isEmpty() ? QStringLiteral("BookHub") : text;
    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const int charIndex =
            static_cast<int>((static_cast<qint64>(i) * source.size()) / sampleCount);
        const int safeIndex = std::clamp(charIndex, 0, static_cast<int>(source.size()) - 1);
        const ushort code = source.at(safeIndex).unicode();
        const double wordShape = 1.0 + (static_cast<double>(code % 17) / 75.0);
        const double syllable = 0.62 + (0.38 * std::sin(kTwoPi * profile.cadence * t));
        const double phrase = 0.82 + (0.18 * std::sin(kTwoPi * 0.55 * t));
        const double frequency =
            (profile.baseFrequency * wordShape) + (18.0 * std::sin(kTwoPi * 1.7 * t));
        const double carrier = std::sin(kTwoPi * frequency * t);
        const double overtone = std::sin(kTwoPi * frequency * 2.0 * t) * profile.brightness;
        const double consonant = std::sin(kTwoPi * frequency * 3.0 * t) * 0.08;
        const double envelope = std::clamp(syllable * phrase, 0.0, 1.0);
        const double sample =
            std::clamp((carrier + overtone + consonant) * envelope * 0.28, -0.95, 0.95);
        const auto value =
            static_cast<qint16>(sample * static_cast<double>(std::numeric_limits<qint16>::max()));
        appendUInt16LE(pcm, static_cast<quint16>(value));
    }

    QByteArray wav;
    appendAscii(wav, "RIFF");
    appendUInt32LE(wav, static_cast<quint32>(36 + pcm.size()));
    appendAscii(wav, "WAVE");
    appendAscii(wav, "fmt ");
    appendUInt32LE(wav, 16);
    appendUInt16LE(wav, 1);
    appendUInt16LE(wav, kChannels);
    appendUInt32LE(wav, kSampleRate);
    appendUInt32LE(wav, (kSampleRate * kChannels * kBitsPerSample) / 8);
    appendUInt16LE(wav, (kChannels * kBitsPerSample) / 8);
    appendUInt16LE(wav, kBitsPerSample);
    appendAscii(wav, "data");
    appendUInt32LE(wav, static_cast<quint32>(pcm.size()));
    wav.append(pcm);
    return wav;
}

void NativeTTSService::appendAscii(QByteArray &data, const char *text)
{
    data.append(text, 4);
}

void NativeTTSService::appendUInt16LE(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

void NativeTTSService::appendUInt32LE(QByteArray &data, quint32 value)
{
    appendUInt16LE(data, static_cast<quint16>(value & 0xffff));
    appendUInt16LE(data, static_cast<quint16>((value >> 16) & 0xffff));
}

} // namespace bookhub::gui
