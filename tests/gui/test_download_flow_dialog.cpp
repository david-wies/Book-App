#include <QtTest>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

#include "gui/dialogs/download_flow_dialog.h"
#include "gui/services/library_service.h"
#include "support/test_query_worker.h"

namespace bookhub::gui {

class DownloadFlowDialogTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void singleLanguage_startsAtFormatStep();
    void nextButton_requiresSelectionsPerStep();
    void formatSelection_populatesSources();

    void backNavigation_decrementsStep();
    void backNavigation_singleLanguage_clampedAtFormatStep();
    void staleDetailsRequest_isIgnored();
    void staleFormatsRequest_isIgnored();
    void emptyEditions_showsErrorState();
    void singleEdition_formatRequestedExactlyOnce();

private:
    TestQueryWorker *m_worker{};
    LibraryService  *m_libraryService{};
};

void DownloadFlowDialogTest::init()
{
    m_worker = new TestQueryWorker();
    m_libraryService = new LibraryService();
    m_libraryService->connectToWorker(m_worker);
}

void DownloadFlowDialogTest::cleanup()
{
    delete m_libraryService;
    m_libraryService = nullptr;
    delete m_worker;
    m_worker = nullptr;
}

void DownloadFlowDialogTest::singleLanguage_startsAtFormatStep()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 7;

    BookDetails details;
    details.title = QStringLiteral("Pride and Prejudice");
    details.editions.append({11, QStringLiteral("en")});
    dialog.onDetailsCompleted(7, details);

    QCOMPARE(dialog.m_currentStep, 1);
    QCOMPARE(dialog.m_stepLabel->text(), QStringLiteral("Step 1 of 2: Format"));
}

void DownloadFlowDialogTest::nextButton_requiresSelectionsPerStep()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 9;

    BookDetails details;
    details.title = QStringLiteral("Multilingual");
    details.editions.append({21, QStringLiteral("en")});
    details.editions.append({22, QStringLiteral("fr")});
    dialog.onDetailsCompleted(9, details);

    QCOMPARE(dialog.m_currentStep, 0);
    QVERIFY(dialog.m_nextBtn->isEnabled());

    dialog.onNextOrDownloadClicked();
    QCOMPARE(dialog.m_currentStep, 1);
    QVERIFY(!dialog.m_nextBtn->isEnabled());

    QList<BookFormatEntry> formats;
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    formats.append(epub);
    dialog.m_pendingFormatsId = 12;
    dialog.onFormatsCompleted(12, formats);

    QVERIFY(dialog.m_nextBtn->isEnabled());
}

void DownloadFlowDialogTest::formatSelection_populatesSources()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_currentStep = 1;

    QList<BookFormatEntry> formats;

    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    epub.sources.append({QStringLiteral("Gutenberg"), QStringLiteral("https://gutenberg.org/epub")});
    epub.sources.append({QStringLiteral("Mirror"), QStringLiteral("https://mirror.org/epub")});

    BookFormatEntry pdf;
    pdf.formatType = QStringLiteral("pdf");
    pdf.sources.append({QStringLiteral("Gutenberg"), QStringLiteral("https://gutenberg.org/pdf")});

    formats.append(epub);
    formats.append(pdf);
    dialog.m_pendingFormatsId = 4;
    dialog.onFormatsCompleted(4, formats);

    dialog.m_formatList->setCurrentRow(1);
    dialog.onFormatSelectionChanged();

    QCOMPARE(dialog.m_sourceList->count(), 1);
    QVERIFY(dialog.m_sourceList->item(0)->text().contains(QStringLiteral("Gutenberg")));
}

void DownloadFlowDialogTest::backNavigation_decrementsStep()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 1;

    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    details.editions.append({2, QStringLiteral("fr")});
    dialog.onDetailsCompleted(1, details);
    QCOMPARE(dialog.m_currentStep, 0); // language step

    // back at minimum step is a no-op
    dialog.onBackClicked();
    QCOMPARE(dialog.m_currentStep, 0);
    QVERIFY(dialog.m_backBtn->isHidden());

    // advance to format step
    dialog.onNextOrDownloadClicked();
    QCOMPARE(dialog.m_currentStep, 1);
    QVERIFY(!dialog.m_backBtn->isHidden());

    // back returns to language step and hides the back button
    dialog.onBackClicked();
    QCOMPARE(dialog.m_currentStep, 0);
    QVERIFY(dialog.m_backBtn->isHidden());
}

void DownloadFlowDialogTest::backNavigation_singleLanguage_clampedAtFormatStep()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 2;

    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    dialog.onDetailsCompleted(2, details);
    QCOMPARE(dialog.m_currentStep, 1); // starts at format step; no language step

    // back at format step (minimum for single-language) is a no-op
    dialog.onBackClicked();
    QCOMPARE(dialog.m_currentStep, 1);

    // inject a format with a source so we can reach the source step
    QList<BookFormatEntry> formats;
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    epub.sources.append({QStringLiteral("Gutenberg"), QStringLiteral("https://example.com/book.epub")});
    formats.append(epub);
    dialog.m_pendingFormatsId = 99;
    dialog.onFormatsCompleted(99, formats);

    dialog.onNextOrDownloadClicked();
    QCOMPARE(dialog.m_currentStep, 2);

    // back returns to format step
    dialog.onBackClicked();
    QCOMPARE(dialog.m_currentStep, 1);

    // back at format step (minimum) is a no-op again
    dialog.onBackClicked();
    QCOMPARE(dialog.m_currentStep, 1);
}

void DownloadFlowDialogTest::staleDetailsRequest_isIgnored()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 5;

    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    dialog.onDetailsCompleted(4, details); // wrong request ID

    QVERIFY(dialog.m_editions.isEmpty());
    QCOMPARE(dialog.m_currentStep, 0);
}

void DownloadFlowDialogTest::staleFormatsRequest_isIgnored()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingFormatsId = 7;

    QList<BookFormatEntry> formats;
    BookFormatEntry epub;
    epub.formatType = QStringLiteral("epub");
    formats.append(epub);
    dialog.onFormatsCompleted(6, formats); // wrong request ID

    QVERIFY(dialog.m_formats.isEmpty());
}

void DownloadFlowDialogTest::emptyEditions_showsErrorState()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 3;

    BookDetails details;
    details.title = QStringLiteral("Empty Book");
    // no editions — should trigger error state
    dialog.onDetailsCompleted(3, details);

    QVERIFY(dialog.m_stepsContainer->isHidden());
    QVERIFY(!dialog.m_progressContainer->isHidden());
    QCOMPARE(dialog.m_stepLabel->text(), QStringLiteral("Error"));
    QVERIFY(!dialog.m_resultLabel->text().isEmpty());
}

void DownloadFlowDialogTest::singleEdition_formatRequestedExactlyOnce()
{
    DownloadFlowDialog dialog(m_libraryService, m_worker);
    dialog.m_pendingDetailsId = 1;

    const quint64 idBefore = dialog.m_detailsService->peekNextId();

    BookDetails details;
    details.editions.append({1, QStringLiteral("en")});
    dialog.onDetailsCompleted(1, details);

    // Regression guard: exactly one format request must fire for a single-edition book.
    // Two would mean populateLanguageList's implicit trigger fired AND onDetailsCompleted
    // issued a second explicit request — the double-request bug.
    QCOMPARE(dialog.m_detailsService->peekNextId() - idBefore, quint64{1});
}

} // namespace bookhub::gui

using DownloadFlowDialogTest = bookhub::gui::DownloadFlowDialogTest;
QTEST_MAIN(DownloadFlowDialogTest)
#include "test_download_flow_dialog.moc"
