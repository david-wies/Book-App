#include "gui/dialogs/audiobook_flow_dialog.h"
#include "gui/dialogs/voice_upload_dialog.h"
#include "gui/services/library_service.h"
#include "gui/services/tts_service.h"
#include "gui/widgets/voice_selector_widget.h"
#include "support/test_database_utils.h"
#include "support/test_query_worker.h"
#include "support/wav_utils.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>

#include <QtTest>

namespace bookhub::gui {

class FailingTTSService final : public TTSService
{
public:
    explicit FailingTTSService(QObject *parent = nullptr) : TTSService(parent) {}

    void generatePreview(int voiceId, const QString &, const QString &) override
    {
        emit previewGenerated(voiceId, QByteArray("RIFF"));
    }

    void generateAudiobook(int, const QString &, const QString &, const QString &) override
    {
        ++generationRequests;
        QTimer::singleShot(0, this, [this] {
            emit generationProgress(20);
            emit generationCompleted(false, {});
        });
    }

    int generationRequests{0};
};

class AudiobookFlowDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Construction
    void construction_isModalAtStep0_backHidden();

    // Navigation
    void backNavigation_clampedAtStep0();
    void backNavigation_decrementsStep();
    void nextNavigation_incrementsStep();

    // Details/formats injection
    void singleEdition_autoSelectsLanguage();
    void multipleEditions_requiresLanguageStep();
    void staleDetailsRequest_isIgnored();
    void staleFormatsRequest_isIgnored();

    // Format list
    void populateFormatList_autoSelectsEpub();
    void populateFormatList_selectsFirstTextFormatWhenNoEpub();
    void populateFormatList_filtersNonTextFormats();

    // Voice list
    void voiceSelector_displaysPresetAndCustomSeparately();
    void voiceUploadDialog_validatesFormatAndWavDuration();
    void previewVoice_generatesWavData();
    void previewVoice_writesTempFile();
    void previewListened_gatesGenerateStep();

    // Generation flow
    void startGeneration_succeedsWithAllSelections();
    void startGeneration_showsSaveAsButton();
    void startGeneration_writesWavFile();
    void startGeneration_failsWithMissingSelections();
    void cancelGeneration_stopsAndEnablesRetry();
    void generationFailure_exposesRetry();
    void onGenerationComplete_updatesCloseButton();
    void onAddToLibraryClicked_emitsAudiobookReadyRequest();

    // Dialog reuse: Next button must return to navigation mode after generation.
    void resetState_rewiresToNextFromClose_afterGeneration();

    // Generation complete with book not in library: "Add to library" stays hidden.
    void onGenerationComplete_hidesAddToLibBtn_whenNotInLibrary();

    // End-to-end: details/formats flow through the real worker chain. Would
    // regress if the dialog stops calling BookDetailsService::connectToWorker.
    void startForBook_populatesLanguagesThroughWorkerChain();

private:
    TestQueryWorker *m_worker{};
    LibraryService *m_libraryService{};
    AudiobookFlowDialog *m_dialog{};
};

namespace {

QPushButton *buttonByText(QWidget &widget, const QString &text)
{
    const auto buttons = widget.findChildren<QPushButton *>();
    for (auto *button : buttons) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

} // namespace

void AudiobookFlowDialogTest::init()
{
    m_worker = new TestQueryWorker();
    m_libraryService = new LibraryService();
    m_libraryService->connectToWorker(m_worker);
    m_dialog = new AudiobookFlowDialog(m_libraryService, m_worker);
}

void AudiobookFlowDialogTest::cleanup()
{
    delete m_dialog;
    m_dialog = nullptr;
    delete m_libraryService;
    m_libraryService = nullptr;
    delete m_worker;
    m_worker = nullptr;
}

// ---------------------------------------------------------------------------

void AudiobookFlowDialogTest::construction_isModalAtStep0_backHidden()
{
    QVERIFY(m_dialog->windowModality() != Qt::NonModal);
    QCOMPARE(m_dialog->m_currentStep, 0);
    QVERIFY(m_dialog->m_backBtn->isHidden());
}

void AudiobookFlowDialogTest::backNavigation_clampedAtStep0()
{
    m_dialog->onBackClicked();
    QCOMPARE(m_dialog->m_currentStep, 0);
    QVERIFY(m_dialog->m_backBtn->isHidden());
}

void AudiobookFlowDialogTest::backNavigation_decrementsStep()
{
    // Two-edition book shows a language step at step 0.
    m_dialog->m_pendingDetailsId = 1;
    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    details.editions.append({2, QStringLiteral("fr")});
    m_dialog->onDetailsCompleted(1, details);
    QCOMPARE(m_dialog->m_currentStep, 0);
    QVERIFY(m_dialog->m_backBtn->isHidden());

    m_dialog->onNextOrGenerateClicked();
    QCOMPARE(m_dialog->m_currentStep, 1);
    QVERIFY(!m_dialog->m_backBtn->isHidden());

    m_dialog->onBackClicked();
    QCOMPARE(m_dialog->m_currentStep, 0);
    QVERIFY(m_dialog->m_backBtn->isHidden());
}

void AudiobookFlowDialogTest::nextNavigation_incrementsStep()
{
    // Single-edition book: language auto-selected, dialog opens at step 1 (format).
    m_dialog->m_pendingDetailsId = 2;
    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    m_dialog->onDetailsCompleted(2, details);
    QCOMPARE(m_dialog->m_currentStep, 1); // skipped language step

    QList<BookFormatEntry> formats;
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    formats.append(epub);
    m_dialog->m_pendingFormatsId = 3;
    m_dialog->onFormatsCompleted(3, formats);

    m_dialog->onNextOrGenerateClicked();
    QCOMPARE(m_dialog->m_currentStep, 2); // advanced to voice selection
    QVERIFY(!m_dialog->m_backBtn->isHidden());
}

void AudiobookFlowDialogTest::singleEdition_autoSelectsLanguage()
{
    m_dialog->m_pendingDetailsId = 5;
    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    m_dialog->onDetailsCompleted(5, details);

    QCOMPARE(m_dialog->m_hasLanguageStep, false);
    QCOMPARE(m_dialog->m_selectedLanguage, QStringLiteral("en"));
    QCOMPARE(m_dialog->m_currentStep, 1); // dialog skips to format step
}

void AudiobookFlowDialogTest::multipleEditions_requiresLanguageStep()
{
    m_dialog->m_pendingDetailsId = 6;
    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    details.editions.append({2, QStringLiteral("fr")});
    m_dialog->onDetailsCompleted(6, details);

    QCOMPARE(m_dialog->m_hasLanguageStep, true);
    QCOMPARE(m_dialog->m_languageList->count(), 2);
}

void AudiobookFlowDialogTest::staleDetailsRequest_isIgnored()
{
    m_dialog->m_pendingDetailsId = 7;
    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    m_dialog->onDetailsCompleted(6, details); // wrong ID
    QVERIFY(m_dialog->m_editions.isEmpty());
}

void AudiobookFlowDialogTest::staleFormatsRequest_isIgnored()
{
    m_dialog->m_pendingFormatsId = 9;
    QList<BookFormatEntry> formats;
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    formats.append(epub);
    m_dialog->onFormatsCompleted(8, formats); // wrong ID
    QVERIFY(m_dialog->m_formats.isEmpty());
}

void AudiobookFlowDialogTest::populateFormatList_autoSelectsEpub()
{
    // txt is first so the test verifies epub is preferred, not just "selects first"
    QList<BookFormatEntry> formats;
    BookFormatEntry txt;
    txt.formatType = QStringLiteral("txt");
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    formats.append(txt);
    formats.append(epub);

    m_dialog->m_pendingFormatsId = 4;
    m_dialog->onFormatsCompleted(4, formats);

    QCOMPARE(m_dialog->m_selectedFormat, QStringLiteral("epub"));
    QCOMPARE(m_dialog->m_formatList->count(), 2);
    QVERIFY(m_dialog->m_formatList->item(1)->text().contains(QStringLiteral("recommended")));
}

void AudiobookFlowDialogTest::populateFormatList_selectsFirstTextFormatWhenNoEpub()
{
    QList<BookFormatEntry> formats;
    BookFormatEntry txt;
    txt.formatType = QStringLiteral("txt");
    BookFormatEntry pdf;
    pdf.formatType = QStringLiteral("pdf");
    formats.append(txt);
    formats.append(pdf);

    m_dialog->m_pendingFormatsId = 5;
    m_dialog->onFormatsCompleted(5, formats);

    QCOMPARE(m_dialog->m_selectedFormat, QStringLiteral("txt"));
    QCOMPARE(m_dialog->m_formatList->count(), 1);
}

void AudiobookFlowDialogTest::populateFormatList_filtersNonTextFormats()
{
    QList<BookFormatEntry> formats;
    BookFormatEntry pdf;
    pdf.formatType = QStringLiteral("pdf");
    BookFormatEntry mobi;
    mobi.formatType = QStringLiteral("mobi");
    BookFormatEntry html;
    html.formatType = QStringLiteral("html");
    formats.append(pdf);
    formats.append(mobi);
    formats.append(html);

    m_dialog->m_pendingFormatsId = 10;
    m_dialog->onFormatsCompleted(10, formats);

    QCOMPARE(m_dialog->m_formatList->count(), 1);
    QCOMPARE(m_dialog->m_selectedFormat, QStringLiteral("html"));
}

void AudiobookFlowDialogTest::voiceSelector_displaysPresetAndCustomSeparately()
{
    QList<VoiceEntry> voices;
    voices.append({1, QStringLiteral("Classic Storyteller"), true});
    voices.append({2, QStringLiteral("Warm Listener"), true});
    voices.append({3, QStringLiteral("My Voice"), false});

    m_dialog->m_voiceSelector->setVoices(voices);

    // VoiceSelectorWidget has two QListWidgets: preset list first, custom second.
    const auto lists = m_dialog->m_voiceSelector->findChildren<QListWidget *>();
    QCOMPARE(lists.size(), 2);
    QCOMPARE(lists[0]->count(), 2); // preset
    QCOMPARE(lists[1]->count(), 1); // custom
}

void AudiobookFlowDialogTest::voiceUploadDialog_validatesFormatAndWavDuration()
{
    VoiceUploadDialog dialog;
    auto *validateButton = buttonByText(dialog, QStringLiteral("Validate & Upload"));
    QVERIFY(validateButton);

    auto *voiceName = dialog.findChild<QLineEdit *>(QStringLiteral("voiceNameEdit"));
    QVERIFY(voiceName);
    voiceName->setText(QStringLiteral("My Voice"));

    const QString invalidPath = QDir::temp().filePath(QStringLiteral("not-a-voice.txt"));
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    invalidFile.write("nope");
    invalidFile.close();

    QVERIFY(QMetaObject::invokeMethod(&dialog, "onFileSelected", Q_ARG(QString, invalidPath)));
    QVERIFY(!validateButton->isEnabled());

    const QString shortWav = bookhub::tests::writeSilentWav(5000);
    QVERIFY(!shortWav.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onFileSelected", Q_ARG(QString, shortWav)));
    QVERIFY(!validateButton->isEnabled());

    const QString validWav = bookhub::tests::writeSilentWav(12000);
    QVERIFY(!validWav.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onFileSelected", Q_ARG(QString, validWav)));
    QVERIFY(validateButton->isEnabled());

    QFile::remove(invalidPath);
    QFile::remove(shortWav);
    QFile::remove(validWav);
}

void AudiobookFlowDialogTest::previewVoice_generatesWavData()
{
    m_dialog->m_bookTitle = QStringLiteral("Pride and Prejudice");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");

    m_dialog->onPreviewVoiceClicked();

    QTRY_VERIFY(!m_dialog->m_previewAudioData.isEmpty());
    QCOMPARE(m_dialog->m_previewAudioData.left(4), QByteArray("RIFF"));
    // After preview is ready the button returns to its idle label so the user
    // can generate a new one or click it again to play/stop.
    QCOMPARE(m_dialog->m_previewVoiceBtn->text(), QStringLiteral("▶ Preview selected voice"));
}

void AudiobookFlowDialogTest::previewVoice_writesTempFile()
{
    m_dialog->m_bookTitle = QStringLiteral("Pride and Prejudice");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");

    m_dialog->onPreviewVoiceClicked();

    QTRY_VERIFY(!m_dialog->m_previewTempPath.isEmpty());
    QVERIFY(QFileInfo::exists(m_dialog->m_previewTempPath));
    QCOMPARE(m_dialog->m_previewAudioData.left(4), QByteArray("RIFF"));

    // Temp file is cleaned up when voice changes.
    const QString oldPath = m_dialog->m_previewTempPath;
    m_dialog->onVoiceSelectionChanged(-1, {});
    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(m_dialog->m_previewTempPath.isEmpty());
}

void AudiobookFlowDialogTest::previewListened_gatesGenerateStep()
{
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 3; // preview step

    // Next is disabled until the user opens the preview.
    m_dialog->updateNextButtonEnabled();
    QVERIFY(!m_dialog->m_nextBtn->isEnabled());

    // Simulate the user clicking play — sets m_previewListened.
    m_dialog->m_previewListened = true;
    m_dialog->updateNextButtonEnabled();
    QVERIFY(m_dialog->m_nextBtn->isEnabled());

    // Changing voice resets the gate.
    m_dialog->onVoiceSelectionChanged(2, QStringLiteral("Warm Listener"));
    m_dialog->updateNextButtonEnabled();
    QVERIFY(!m_dialog->m_nextBtn->isEnabled());
}

void AudiobookFlowDialogTest::startGeneration_succeedsWithAllSelections()
{
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_selectedFormat = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 4;

    m_dialog->onNextOrGenerateClicked();

    QTRY_COMPARE(m_dialog->m_progressBar->value(), 100);
    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));
    QVERIFY(QFileInfo::exists(m_dialog->m_generatedOutputPath));
    QFile::remove(m_dialog->m_generatedOutputPath);
}

void AudiobookFlowDialogTest::startGeneration_showsSaveAsButton()
{
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_selectedFormat = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 4;

    m_dialog->onNextOrGenerateClicked();

    QTRY_COMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));
    QVERIFY(!m_dialog->m_openFileBtn->isHidden());
    QVERIFY(!m_dialog->m_saveAsBtn->isHidden());
    QFile::remove(m_dialog->m_generatedOutputPath);
}

void AudiobookFlowDialogTest::startGeneration_writesWavFile()
{
    NativeTTSService service;
    QSignalSpy done(&service, &TTSService::generationCompleted);

    const QString outputPath = QDir::temp().filePath(
        QStringLiteral("bookhub-tts-test-%1.wav").arg(QDateTime::currentMSecsSinceEpoch()));
    service.generateAudiobook(
        2, QStringLiteral("Warm Listener"), QStringLiteral("A small test audiobook."), outputPath);

    QVERIFY(done.wait(2000));
    QCOMPARE(done.first().at(0).toBool(), true);
    QFile file(outputPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(4), QByteArray("RIFF"));
    file.remove();
}

void AudiobookFlowDialogTest::startGeneration_failsWithMissingSelections()
{
    // Leave language, format, and voice unset so startGeneration() rejects.
    m_dialog->m_currentStep = 4;

    // Dismiss the warning dialog that showErrorState() will block on.
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box)
            box->reject();
    });

    m_dialog->onNextOrGenerateClicked();

    // Generation was blocked — progress bar stays at 0 and result is empty.
    QCOMPARE(m_dialog->m_progressBar->value(), 0);
    QVERIFY(m_dialog->m_resultLabel->text().isEmpty());
}

void AudiobookFlowDialogTest::cancelGeneration_stopsAndEnablesRetry()
{
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_selectedFormat = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 4;

    m_dialog->onNextOrGenerateClicked();
    QVERIFY(!m_dialog->m_cancelGenBtn->isHidden());

    m_dialog->onCancelGenerationClicked();

    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Retry"));
    QVERIFY(m_dialog->m_nextBtn->isEnabled());
    QVERIFY(m_dialog->m_cancelGenBtn->isHidden());
    QVERIFY(m_dialog->m_resultLabel->text().contains(QStringLiteral("cancelled")));
}

void AudiobookFlowDialogTest::generationFailure_exposesRetry()
{
    delete m_dialog;

    FailingTTSService failingService;
    m_dialog = new AudiobookFlowDialog(m_libraryService, m_worker, &failingService);
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_selectedFormat = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 4;

    m_dialog->onNextOrGenerateClicked();

    QTRY_COMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Retry"));
    QCOMPARE(failingService.generationRequests, 1);
    m_dialog->onNextOrGenerateClicked();
    QTRY_COMPARE(failingService.generationRequests, 2);
}

void AudiobookFlowDialogTest::onGenerationComplete_updatesCloseButton()
{
    m_dialog->onGenerationComplete();

    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));
    QVERIFY(!m_dialog->m_resultLabel->text().isEmpty());
}

void AudiobookFlowDialogTest::onAddToLibraryClicked_emitsAudiobookReadyRequest()
{
    m_dialog->m_bookId = QStringLiteral("gutenberg:1342");
    m_dialog->m_libraryItemId = 1; // non-zero triggers the status update path

    QSignalSpy spy(m_libraryService, &LibraryService::setAudiobookReadyRequested);
    m_dialog->onGenerationComplete(); // shows "Add to library" button — no status write
    QCOMPARE(spy.count(), 0);

    m_dialog->onAddToLibraryClicked(); // explicit user action triggers the write
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("gutenberg:1342"));
}

void AudiobookFlowDialogTest::resetState_rewiresToNextFromClose_afterGeneration()
{
    // Drive to completion — onGenerationComplete() rewires Next to accept().
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_selectedFormat = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep = 4;
    m_dialog->onNextOrGenerateClicked();
    QTRY_COMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));

    // Reopen for a second book — resetState() must rewire Next back to navigation.
    m_dialog->resetState();
    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Next →"));
    QCOMPARE(m_dialog->m_currentStep, 0);

    // Verify the step actually advances on click rather than accepting the dialog.
    m_dialog->m_selectedLanguage = QStringLiteral("en");
    m_dialog->m_hasLanguageStep = true;
    m_dialog->updateStepUi();
    m_dialog->onNextOrGenerateClicked();
    QCOMPARE(m_dialog->m_currentStep, 1);
}

void AudiobookFlowDialogTest::onGenerationComplete_hidesAddToLibBtn_whenNotInLibrary()
{
    m_dialog->m_libraryItemId = 0; // book not in the user's library
    m_dialog->onGenerationComplete();

    QVERIFY(m_dialog->m_addToLibBtn->isHidden());
    // Open-file and result label are still shown regardless of library membership.
    QVERIFY(!m_dialog->m_openFileBtn->isHidden());
    QVERIFY(!m_dialog->m_resultLabel->text().isEmpty());
}

void AudiobookFlowDialogTest::startForBook_populatesLanguagesThroughWorkerChain()
{
    // Tear down the dialog from init() — we need a fresh one constructed
    // *after* the default-connection schema is open so its BookDetailsService
    // can resolve real DB rows through the worker chain.
    delete m_dialog;
    m_dialog = nullptr;

    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open()); // opens default connection
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    AudiobookFlowDialog dialog(m_libraryService, m_worker);
    dialog.startForBook(QStringLiteral("lccn:n78095332")); // 2 editions in sample data

    // If BookDetailsService is not wired to the worker, the chain produces
    // nothing and m_editions stays empty. With wiring, the synchronous
    // TestQueryWorker dispatch fully populates the language list.
    QCOMPARE(dialog.m_editions.size(), 2);
    QCOMPARE(dialog.m_languageList->count(), 2);
    QVERIFY(dialog.m_hasLanguageStep);
}

} // namespace bookhub::gui

using AudiobookFlowDialogTest = bookhub::gui::AudiobookFlowDialogTest;
QTEST_MAIN(AudiobookFlowDialogTest)
#include "test_audiobook_flow_dialog.moc"
