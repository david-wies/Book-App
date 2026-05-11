#include "gui/utils/wav_utils.h"
#include "support/wav_utils.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <QtTest>

namespace bookhub::gui {

class WavUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    // durationMsFromBytes
    void fromBytes_validPcm_returnsCorrectDuration();
    void fromBytes_tooShort_returnsZero();
    void fromBytes_badRiffMagic_returnsZero();
    void fromBytes_badWaveMagic_returnsZero();
    void fromBytes_nonPcmFormat_returnsZero();
    void fromBytes_zeroBytesPerSecond_returnsZero();

    // durationMsFromFile
    void fromFile_validWav_returnsCorrectDuration();
    void fromFile_nonexistentFile_returnsMinusOne();
    void fromFile_craftedDataBytesExceedsFileSize_returnsMinusOne();
    void fromFile_shortWavBelowMinimum_returnsSmallDuration();
};

// ---------------------------------------------------------------------------
// durationMsFromBytes
// ---------------------------------------------------------------------------

void WavUtilsTest::fromBytes_validPcm_returnsCorrectDuration()
{
    // writeSilentWav produces 8000 Hz, 1-ch, 16-bit PCM.
    // bytesPerSecond = 8000 * 1 * 2 = 16000; 12 s → dataBytes = 192000.
    const QString path = bookhub::tests::writeSilentWav(12000);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray header = f.read(44);
    f.remove();

    const qint64 ms = bookhub::gui::wav::durationMsFromBytes(header);
    QCOMPARE(ms, qint64{12000});
}

void WavUtilsTest::fromBytes_tooShort_returnsZero()
{
    QCOMPARE(bookhub::gui::wav::durationMsFromBytes(QByteArrayView("RIFF", 4)), qint64{0});
}

void WavUtilsTest::fromBytes_badRiffMagic_returnsZero()
{
    const QString path = bookhub::tests::writeSilentWav(1000);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QByteArray header = f.read(44);
    // Corrupt the RIFF FourCC.
    header[0] = 'X';
    f.remove();

    QCOMPARE(bookhub::gui::wav::durationMsFromBytes(header), qint64{0});
}

void WavUtilsTest::fromBytes_badWaveMagic_returnsZero()
{
    const QString path = bookhub::tests::writeSilentWav(1000);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray header = f.read(44);
    f.remove();

    // Bytes 8–11 are the "WAVE" FourCC.
    header[8] = 'X';

    QCOMPARE(bookhub::gui::wav::durationMsFromBytes(header), qint64{0});
}

void WavUtilsTest::fromBytes_nonPcmFormat_returnsZero()
{
    const QString path = bookhub::tests::writeSilentWav(1000);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray header = f.read(44);
    f.remove();

    // Byte 20 is the low byte of the audio-format field (1 = PCM). Set to 3 (IEEE float).
    header[20] = '\x03';

    QCOMPARE(bookhub::gui::wav::durationMsFromBytes(header), qint64{0});
}

void WavUtilsTest::fromBytes_zeroBytesPerSecond_returnsZero()
{
    const QString path = bookhub::tests::writeSilentWav(1000);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray header = f.read(44);
    f.remove();

    // Zero out sample-rate (bytes 24–27) to force bytesPerSecond == 0.
    header[24] = header[25] = header[26] = header[27] = '\0';

    QCOMPARE(bookhub::gui::wav::durationMsFromBytes(header), qint64{0});
}

// ---------------------------------------------------------------------------
// durationMsFromFile
// ---------------------------------------------------------------------------

void WavUtilsTest::fromFile_validWav_returnsCorrectDuration()
{
    const QString path = bookhub::tests::writeSilentWav(12000);
    QVERIFY(!path.isEmpty());
    const int ms = bookhub::gui::wav::durationMsFromFile(path);
    QFile::remove(path);
    QCOMPARE(ms, 12000);
}

void WavUtilsTest::fromFile_nonexistentFile_returnsMinusOne()
{
    QCOMPARE(bookhub::gui::wav::durationMsFromFile(
                 QStringLiteral("/nonexistent_bookhub_dir/no.wav")),
             -1);
}

void WavUtilsTest::fromFile_craftedDataBytesExceedsFileSize_returnsMinusOne()
{
    // Write a tiny PCM payload (40 bytes, 10 ms at 8 kHz 16-bit mono).
    constexpr quint32 realPcmBytes = 40;
    QByteArray pcm(realPcmBytes, '\0');

    QByteArray header;
    bookhub::tests::wavAppendAscii(header, "RIFF");
    bookhub::tests::wavAppendUInt32LE(header, 36 + realPcmBytes);
    bookhub::tests::wavAppendAscii(header, "WAVE");
    bookhub::tests::wavAppendAscii(header, "fmt ");
    bookhub::tests::wavAppendUInt32LE(header, 16);
    bookhub::tests::wavAppendUInt16LE(header, 1);      // PCM
    bookhub::tests::wavAppendUInt16LE(header, 1);      // channels
    bookhub::tests::wavAppendUInt32LE(header, 8000);   // sample rate
    bookhub::tests::wavAppendUInt32LE(header, 16000);  // byte rate
    bookhub::tests::wavAppendUInt16LE(header, 2);      // block align
    bookhub::tests::wavAppendUInt16LE(header, 16);     // bits per sample
    bookhub::tests::wavAppendAscii(header, "data");
    // Lie: claim 96 MB of audio data while only writing realPcmBytes.
    bookhub::tests::wavAppendUInt32LE(header, 0x05B8D800U);

    QByteArray wav = header;
    wav.append(pcm);

    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    QVERIFY(tmpFile.open());
    tmpFile.write(wav);
    tmpFile.flush();

    QCOMPARE(bookhub::gui::wav::durationMsFromFile(tmpFile.fileName()), -1);
}

void WavUtilsTest::fromFile_shortWavBelowMinimum_returnsSmallDuration()
{
    // 5-second WAV is below the 10-second voice-upload gate.
    // The function should return the correct duration (5000), not an error.
    // Callers (VoiceUploadDialog) are responsible for rejecting it.
    const QString path = bookhub::tests::writeSilentWav(5000);
    QVERIFY(!path.isEmpty());
    const int ms = bookhub::gui::wav::durationMsFromFile(path);
    QFile::remove(path);
    QCOMPARE(ms, 5000);
}

} // namespace bookhub::gui

using WavUtilsTest = bookhub::gui::WavUtilsTest;
QTEST_MAIN(WavUtilsTest)
#include "test_wav_utils.moc"
