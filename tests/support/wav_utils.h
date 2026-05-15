#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>

namespace bookhub::tests {

inline void wavAppendAscii(QByteArray &data, const char *text)
{
    data.append(text, 4);
}

inline void wavAppendUInt16LE(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

inline void wavAppendUInt32LE(QByteArray &data, quint32 value)
{
    wavAppendUInt16LE(data, static_cast<quint16>(value & 0xffff));
    wavAppendUInt16LE(data, static_cast<quint16>((value >> 16) & 0xffff));
}

// Writes a silent PCM WAV of the given duration to a temp file.
// Returns the file path, or an empty string on failure.
// The caller is responsible for removing the file when done.
inline QString writeSilentWav(int durationMs)
{
    constexpr int sampleRate    = 8000;
    constexpr int channels      = 1;
    constexpr int bitsPerSample = 16;
    const int sampleCount = sampleRate * durationMs / 1000;
    QByteArray pcm(sampleCount * 2, '\0');

    QByteArray wav;
    wavAppendAscii(wav, "RIFF");
    wavAppendUInt32LE(wav, static_cast<quint32>(36 + pcm.size()));
    wavAppendAscii(wav, "WAVE");
    wavAppendAscii(wav, "fmt ");
    wavAppendUInt32LE(wav, 16);
    wavAppendUInt16LE(wav, 1);
    wavAppendUInt16LE(wav, channels);
    wavAppendUInt32LE(wav, sampleRate);
    wavAppendUInt32LE(wav, sampleRate * channels * bitsPerSample / 8);
    wavAppendUInt16LE(wav, channels * bitsPerSample / 8);
    wavAppendUInt16LE(wav, bitsPerSample);
    wavAppendAscii(wav, "data");
    wavAppendUInt32LE(wav, static_cast<quint32>(pcm.size()));
    wav.append(pcm);

    const QString path = QDir::temp().filePath(
        QStringLiteral("bookhub-voice-%1-%2.wav")
            .arg(durationMs)
            .arg(QDateTime::currentMSecsSinceEpoch()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(wav);
    return path;
}

} // namespace bookhub::tests
