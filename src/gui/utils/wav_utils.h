#pragma once

#include <QByteArrayView>
#include <QFile>
#include <QString>

#include <limits>

namespace bookhub::gui::wav {

// Parse duration from the first 44 bytes of a standard PCM WAV stream.
// Returns milliseconds on success, or 0 on any parse failure:
//   - fewer than 44 bytes supplied
//   - wrong RIFF/WAVE magic or "fmt " subchunk tag
//   - audio format is not PCM (1)  [ADPCM, IEEE float give wrong results]
//   - claimed data size exceeds the supplied buffer
//   - bytesPerSecond computed as zero
//
// NOTE: Files that embed JUNK/LIST chunks between "fmt " and "data" shift
// the data-chunk offset past byte 40, yielding an incorrect duration.
// Acceptable for NativeTTSService output (always writes canonical 44-byte
// headers) and for MVP voice uploads (expected to be plain recorder output).
[[nodiscard]] inline qint64 durationMsFromBytes(QByteArrayView wav) noexcept
{
    if (wav.size() < 44)
        return 0;
    if (wav.sliced(0, 4) != "RIFF" || wav.sliced(8, 4) != "WAVE")
        return 0;
    if (wav.sliced(12, 4) != "fmt " || wav.sliced(20, 2) != QByteArrayView("\x01\x00", 2))
        return 0;

    auto u16 = [&wav](int off) noexcept -> quint32 {
        return static_cast<quint32>(static_cast<uchar>(wav[off])) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 1])) << 8);
    };
    auto u32 = [&wav](int off) noexcept -> quint32 {
        return static_cast<quint32>(static_cast<uchar>(wav[off])) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 1])) << 8) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 2])) << 16) |
               (static_cast<quint32>(static_cast<uchar>(wav[off + 3])) << 24);
    };

    const quint32 sampleRate = u32(24);
    const quint32 channels = u16(22);
    const quint32 bitsPerSample = u16(34);
    const quint32 dataBytes = u32(40);
    // When the caller supplies the full audio buffer (not just the 44-byte header),
    // reject a header that claims more PCM data than the buffer actually contains.
    // The size() == 44 case is intentionally skipped: durationMsFromFile passes only
    // the header and performs its own file-size cross-check before calling this.
    if (wav.size() > 44 && static_cast<qsizetype>(dataBytes) > wav.size() - 44)
        return 0;

    // Promote to quint64 before multiplying to avoid quint32 overflow for
    // high sample-rate / multi-channel files (e.g. 192 kHz stereo 24-bit).
    const quint64 bytesPerSecond =
        static_cast<quint64>(sampleRate) * channels * bitsPerSample / 8;
    if (bytesPerSecond == 0)
        return 0;
    return static_cast<qint64>(static_cast<quint64>(dataBytes) * 1000 / bytesPerSecond);
}

// Read the WAV header from filePath and return the PCM duration in milliseconds.
// Returns -1 if the file cannot be opened, is too short, has an invalid header,
// the claimed data size exceeds the actual file size, or the computed duration
// would overflow int. Returns 0 for a valid but empty WAV.
[[nodiscard]] inline int durationMsFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray header = file.read(44);
    if (header.size() < 44)
        return -1;

    // A crafted header can claim a large dataBytes value to inflate the computed
    // duration past the 120-second gate while the actual file is nearly empty.
    // Cross-check against the real file size before trusting the header field.
    const auto claimedDataBytes =
        static_cast<quint32>(static_cast<uchar>(header[40])) |
        (static_cast<quint32>(static_cast<uchar>(header[41])) << 8) |
        (static_cast<quint32>(static_cast<uchar>(header[42])) << 16) |
        (static_cast<quint32>(static_cast<uchar>(header[43])) << 24);
    if (static_cast<qint64>(claimedDataBytes) > file.size() - 44)
        return -1;

    const qint64 ms = durationMsFromBytes(header);
    if (ms < 0 || ms > static_cast<qint64>(std::numeric_limits<int>::max()))
        return -1;
    // durationMsFromBytes returns 0 for parse failures AND for genuine 0-sample WAVs.
    // Both are unusable for voice upload (minimum 10 s required); callers treat 0 as invalid.
    return static_cast<int>(ms);
}

} // namespace bookhub::gui::wav
