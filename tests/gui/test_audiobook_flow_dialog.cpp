#include <QtTest>
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>

#include "gui/dialogs/audiobook_flow_dialog.h"
#include "gui/services/library_service.h"
#include "gui/widgets/voice_selector_widget.h"
#include "support/test_query_worker.h"

namespace bookhub::gui {

class AudiobookFlowDialogTest : public QObject {
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
    void populateFormatList_selectsFirstWhenNoEpub();

    // Voice list
    void voiceSelector_displaysPresetAndCustomSeparately();

    // Generation flow
    void startGeneration_succeedsWithAllSelections();
    void startGeneration_failsWithMissingSelections();
    void onGenerationComplete_updatesCloseButton();
    void onAddToLibraryClicked_emitsAudiobookReadyRequest();

private:
    TestQueryWorker     *m_worker{};
    LibraryService      *m_libraryService{};
    AudiobookFlowDialog *m_dialog{};
};

void AudiobookFlowDialogTest::init()
{
    m_worker = new TestQueryWorker();
    m_libraryService = new LibraryService();
    m_libraryService->connectToWorker(m_worker);
    m_dialog = new AudiobookFlowDialog(m_libraryService, m_worker);
}

void AudiobookFlowDialogTest::cleanup()
{
    delete m_dialog;    m_dialog        = nullptr;
    delete m_libraryService; m_libraryService = nullptr;
    delete m_worker;    m_worker         = nullptr;
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
    BookFormatEntry epub; epub.formatType = QStringLiteral("epub");
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
    BookFormatEntry epub; epub.formatType = QStringLiteral("epub");
    formats.append(epub);
    m_dialog->onFormatsCompleted(8, formats); // wrong ID
    QVERIFY(m_dialog->m_formats.isEmpty());
}

void AudiobookFlowDialogTest::populateFormatList_autoSelectsEpub()
{
    // txt is first so the test verifies epub is preferred, not just "selects first"
    QList<BookFormatEntry> formats;
    BookFormatEntry txt;  txt.formatType  = QStringLiteral("txt");
    BookFormatEntry epub; epub.formatType = QStringLiteral("epub");
    formats.append(txt);
    formats.append(epub);

    m_dialog->m_pendingFormatsId = 4;
    m_dialog->onFormatsCompleted(4, formats);

    QCOMPARE(m_dialog->m_selectedFormat, QStringLiteral("epub"));
    QCOMPARE(m_dialog->m_formatList->count(), 2);
    QVERIFY(m_dialog->m_formatList->item(1)->text().contains(QStringLiteral("recommended")));
}

void AudiobookFlowDialogTest::populateFormatList_selectsFirstWhenNoEpub()
{
    QList<BookFormatEntry> formats;
    BookFormatEntry txt; txt.formatType = QStringLiteral("txt");
    BookFormatEntry pdf; pdf.formatType = QStringLiteral("pdf");
    formats.append(txt);
    formats.append(pdf);

    m_dialog->m_pendingFormatsId = 5;
    m_dialog->onFormatsCompleted(5, formats);

    QCOMPARE(m_dialog->m_selectedFormat, QStringLiteral("txt"));
    QCOMPARE(m_dialog->m_formatList->count(), 2);
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

void AudiobookFlowDialogTest::startGeneration_succeedsWithAllSelections()
{
    m_dialog->m_selectedLanguage  = QStringLiteral("en");
    m_dialog->m_selectedFormat    = QStringLiteral("epub");
    m_dialog->m_selectedVoiceId   = 1;
    m_dialog->m_selectedVoiceName = QStringLiteral("Classic Storyteller");
    m_dialog->m_currentStep       = 4;

    m_dialog->onNextOrGenerateClicked();

    QCOMPARE(m_dialog->m_progressBar->value(), 100);
    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));
}

void AudiobookFlowDialogTest::startGeneration_failsWithMissingSelections()
{
    // Leave language, format, and voice unset so startGeneration() rejects.
    m_dialog->m_currentStep = 4;

    // Dismiss the warning dialog that showErrorState() will block on.
    QTimer::singleShot(0, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box) box->reject();
    });

    m_dialog->onNextOrGenerateClicked();

    // Generation was blocked — progress bar stays at 0 and result is empty.
    QCOMPARE(m_dialog->m_progressBar->value(), 0);
    QVERIFY(m_dialog->m_resultLabel->text().isEmpty());
}

void AudiobookFlowDialogTest::onGenerationComplete_updatesCloseButton()
{
    m_dialog->onGenerationComplete();

    QCOMPARE(m_dialog->m_nextBtn->text(), QStringLiteral("Close"));
    QVERIFY(!m_dialog->m_resultLabel->text().isEmpty());
}

void AudiobookFlowDialogTest::onAddToLibraryClicked_emitsAudiobookReadyRequest()
{
    m_dialog->m_bookId        = QStringLiteral("gutenberg:1342");
    m_dialog->m_libraryItemId = 1; // non-zero triggers the status update path

    QSignalSpy spy(m_libraryService, &LibraryService::setAudiobookReadyRequested);
    m_dialog->onGenerationComplete(); // shows "Add to library" button — no status write
    QCOMPARE(spy.count(), 0);

    m_dialog->onAddToLibraryClicked(); // explicit user action triggers the write
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("gutenberg:1342"));
}

} // namespace bookhub::gui

using AudiobookFlowDialogTest = bookhub::gui::AudiobookFlowDialogTest;
QTEST_MAIN(AudiobookFlowDialogTest)
#include "test_audiobook_flow_dialog.moc"
