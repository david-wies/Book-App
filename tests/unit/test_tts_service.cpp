#include "gui/services/native_tts_service.h"
#include "gui/services/pocket_tts_service.h"
#include "gui/services/sherpa_onnx_tts_service.h"
#include "gui/services/tts_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QThreadPool>
#include <QTimer>

#include <QtTest>
#include <algorithm>

namespace bookhub::gui {

class TtsServiceTest : public QObject
{
    Q_OBJECT

private slots:
    // ---------------------------------------------------------------------------
    // NativeTTSService
    // ---------------------------------------------------------------------------
    void native_generatePreview_emitsRiffData();
    void native_generateAudiobook_writesWavFileAndEmitsSuccess();
    void native_generateAudiobook_emitsNoProgressOnWriteFailure();
    void native_cancel_beforeGeneration_isSafe();
    void native_cancel_duringGeneration_suppressesCompletion();

    // ---------------------------------------------------------------------------
    // SherpaOnnxTTSService
    // ---------------------------------------------------------------------------
    void sherpa_libraryAvailable_matchesCompiledIn();
    void sherpa_modelAvailable_returnsFalseWhenFileAbsent();
    void sherpa_generatePreview_whenUnavailable_emitsEmptyData();
    void sherpa_generateAudiobook_whenUnavailable_emitsFailureNoProgress();
    void sherpa_cancel_isSafe();

    // ---------------------------------------------------------------------------
    // PocketTTSService
    // ---------------------------------------------------------------------------
    void pocket_libraryAvailable_matchesCompiledIn();
    void pocket_languageSupported_acceptsAllSixLanguages();
    void pocket_languageSupported_rejectsUnsupportedLanguage();
    void pocket_languageSupported_isCaseInsensitive();
    void pocket_modelsAvailable_returnsFalseWhenDirAbsent();
    void pocket_generatePreview_unsupportedLanguage_emitsEmptyData();
    void pocket_generatePreview_missingModels_emitsEmptyData();
    void pocket_generateAudiobook_unsupportedLanguage_emitsFailure();
    void pocket_generateAudiobook_missingModels_emitsFailure();
    void pocket_cancel_isSafe();
};

// ---------------------------------------------------------------------------
// NativeTTSService tests
// ---------------------------------------------------------------------------

void TtsServiceTest::native_generatePreview_emitsRiffData()
{
    NativeTTSService service;
    QSignalSpy spy(&service, &TTSService::previewGenerated);

    service.generatePreview(
        1, QStringLiteral("Classic Storyteller"), QStringLiteral("A preview sentence."));

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    const QByteArray data = spy.first().at(1).toByteArray();
    QVERIFY(data.size() >= 44);
    QCOMPARE(data.left(4), QByteArray("RIFF"));
}

void TtsServiceTest::native_generateAudiobook_writesWavFileAndEmitsSuccess()
{
    NativeTTSService service;
    QSignalSpy completed(&service, &TTSService::generationCompleted);
    QSignalSpy progress(&service, &TTSService::generationProgress);

    const QString path =
        QDir::tempPath() +
        QStringLiteral("/bookhub-unit-tts-%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
    service.generateAudiobook(
        1, QStringLiteral("Warm Listener"), QStringLiteral("Short test."), path);

    QVERIFY(completed.wait(3000));
    QCOMPARE(completed.first().at(0).toBool(), true);
    QCOMPARE(completed.first().at(1).toString(), path);

    // Progress must have reached 100 on success.
    const bool saw100 = std::ranges::any_of(
        progress, [](const QList<QVariant> &args) { return args.at(0).toInt() == 100; });
    QVERIFY(saw100);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(4), QByteArray("RIFF"));
    f.remove();
}

void TtsServiceTest::native_generateAudiobook_emitsNoProgressOnWriteFailure()
{
    NativeTTSService service;
    QSignalSpy completed(&service, &TTSService::generationCompleted);
    QSignalSpy progress(&service, &TTSService::generationProgress);

    // A path in a non-existent directory will fail the file write.
    const QString badPath = QStringLiteral("/nonexistent_dir_bookhub/audiobook.wav");
    service.generateAudiobook(
        1, QStringLiteral("Classic Storyteller"), QStringLiteral("Test."), badPath);

    QVERIFY(completed.wait(3000));
    QCOMPARE(completed.first().at(0).toBool(), false);

    // generationProgress(100) must NOT be emitted on write failure — only on success.
    // Intermediate values (10..90) are emitted by the progress-animation timer regardless.
    const bool saw100 = std::ranges::any_of(
        progress, [](const QList<QVariant> &args) { return args.at(0).toInt() == 100; });
    QVERIFY(!saw100);
}

void TtsServiceTest::native_cancel_beforeGeneration_isSafe()
{
    NativeTTSService service;
    service.cancel(); // must not crash
}

void TtsServiceTest::native_cancel_duringGeneration_suppressesCompletion()
{
    NativeTTSService service;
    QSignalSpy completed(&service, &TTSService::generationCompleted);

    const QString path =
        QDir::tempPath() +
        QStringLiteral("/bookhub-cancel-test-%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
    service.generateAudiobook(
        1, QStringLiteral("Crisp Narrator"), QStringLiteral("Cancel test."), path);

    // Cancel immediately — before the pool task can post its result.
    service.cancel();

    // Wait for the pool task to finish (it will check the cancel flag and skip
    // the file write), then drain the event queue.  This is deterministic: we
    // know the task has returned and any queued invokeMethod callbacks have
    // been processed before the assertion runs — no fixed time delay needed.
    QThreadPool::globalInstance()->waitForDone(1000);
    QCoreApplication::processEvents();
    QCOMPARE(completed.count(), 0);

    QFile::remove(path);
}

// ---------------------------------------------------------------------------
// SherpaOnnxTTSService tests
// ---------------------------------------------------------------------------

void TtsServiceTest::sherpa_libraryAvailable_matchesCompiledIn()
{
#ifdef BOOKHUB_HAVE_SHERPA_ONNX
    QVERIFY(SherpaOnnxTTSService::libraryAvailable());
#else
    QVERIFY(!SherpaOnnxTTSService::libraryAvailable());
#endif
}

void TtsServiceTest::sherpa_modelAvailable_returnsFalseWhenFileAbsent()
{
    // In the test environment there are no downloaded model files.
    QVERIFY(!SherpaOnnxTTSService::modelAvailable(9999));
}

void TtsServiceTest::sherpa_generatePreview_whenUnavailable_emitsEmptyData()
{
    SherpaOnnxTTSService service;
    QSignalSpy spy(&service, &TTSService::previewGenerated);

    service.generatePreview(1, QStringLiteral("Preset Voice"), QStringLiteral("Hello."));

    QVERIFY(spy.wait(1000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.first().at(1).toByteArray().isEmpty());
}

void TtsServiceTest::sherpa_generateAudiobook_whenUnavailable_emitsFailureNoProgress()
{
    SherpaOnnxTTSService service;
    QSignalSpy completed(&service, &TTSService::generationCompleted);
    QSignalSpy progress(&service, &TTSService::generationProgress);

    service.generateAudiobook(1,
                              QStringLiteral("Preset Voice"),
                              QStringLiteral("Hello."),
                              QDir::tempPath() + QStringLiteral("/sherpa-test.wav"));

    QVERIFY(completed.wait(1000));
    QCOMPARE(completed.first().at(0).toBool(), false);
    QCOMPARE(progress.count(), 0); // no progress emitted before failure
}

void TtsServiceTest::sherpa_cancel_isSafe()
{
    SherpaOnnxTTSService service;
    service.cancel(); // no crash before or after generation
}

// ---------------------------------------------------------------------------
// PocketTTSService tests
// ---------------------------------------------------------------------------

void TtsServiceTest::pocket_libraryAvailable_matchesCompiledIn()
{
#ifdef BOOKHUB_HAVE_POCKET_TTS
    QVERIFY(PocketTTSService::libraryAvailable());
#else
    QVERIFY(!PocketTTSService::libraryAvailable());
#endif
}

void TtsServiceTest::pocket_languageSupported_acceptsAllSixLanguages()
{
    for (const char *lang : {"en", "fr", "de", "it", "pt", "es"})
        QVERIFY(PocketTTSService::languageSupported(QLatin1String(lang)));
}

void TtsServiceTest::pocket_languageSupported_rejectsUnsupportedLanguage()
{
    QVERIFY(!PocketTTSService::languageSupported(QStringLiteral("zh")));
    QVERIFY(!PocketTTSService::languageSupported(QStringLiteral("ja")));
    QVERIFY(!PocketTTSService::languageSupported(QStringLiteral("ar")));
    QVERIFY(!PocketTTSService::languageSupported(QStringLiteral("he")));
}

void TtsServiceTest::pocket_languageSupported_isCaseInsensitive()
{
    QVERIFY(PocketTTSService::languageSupported(QStringLiteral("EN")));
    QVERIFY(PocketTTSService::languageSupported(QStringLiteral("Fr")));
    QVERIFY(PocketTTSService::languageSupported(QStringLiteral("DE")));
}

void TtsServiceTest::pocket_modelsAvailable_returnsFalseWhenDirAbsent()
{
    // The test environment has no pocket-tts model directory.
    QVERIFY(!PocketTTSService::modelsAvailable());
}

void TtsServiceTest::pocket_generatePreview_unsupportedLanguage_emitsEmptyData()
{
    PocketTTSService service(QStringLiteral("zh"), {});
    QSignalSpy spy(&service, &TTSService::previewGenerated);

    service.generatePreview(1, QStringLiteral("My Voice"), QStringLiteral("Hello."));

    QVERIFY(spy.wait(1000));
    QVERIFY(spy.first().at(1).toByteArray().isEmpty());
}

void TtsServiceTest::pocket_generatePreview_missingModels_emitsEmptyData()
{
    PocketTTSService service(QStringLiteral("en"), {});
    QSignalSpy spy(&service, &TTSService::previewGenerated);

    service.generatePreview(1, QStringLiteral("My Voice"), QStringLiteral("Hello."));

    QVERIFY(spy.wait(1000));
    QVERIFY(spy.first().at(1).toByteArray().isEmpty());
}

void TtsServiceTest::pocket_generateAudiobook_unsupportedLanguage_emitsFailure()
{
    PocketTTSService service(QStringLiteral("zh"), {});
    QSignalSpy completed(&service, &TTSService::generationCompleted);
    QSignalSpy progress(&service, &TTSService::generationProgress);

    service.generateAudiobook(1,
                              QStringLiteral("My Voice"),
                              QStringLiteral("Hello."),
                              QDir::tempPath() + QStringLiteral("/pocket-test.wav"));

    QVERIFY(completed.wait(1000));
    QCOMPARE(completed.first().at(0).toBool(), false);
    QCOMPARE(progress.count(), 0);
}

void TtsServiceTest::pocket_generateAudiobook_missingModels_emitsFailure()
{
    PocketTTSService service(QStringLiteral("en"), {});
    QSignalSpy completed(&service, &TTSService::generationCompleted);
    QSignalSpy progress(&service, &TTSService::generationProgress);

    service.generateAudiobook(1,
                              QStringLiteral("My Voice"),
                              QStringLiteral("Hello."),
                              QDir::tempPath() + QStringLiteral("/pocket-test.wav"));

    QVERIFY(completed.wait(1000));
    QCOMPARE(completed.first().at(0).toBool(), false);
    QCOMPARE(progress.count(), 0);
}

void TtsServiceTest::pocket_cancel_isSafe()
{
    PocketTTSService service(QStringLiteral("en"), {});
    service.cancel();
}

} // namespace bookhub::gui

using TtsServiceTest = bookhub::gui::TtsServiceTest;
QTEST_MAIN(TtsServiceTest)
#include "test_tts_service.moc"
