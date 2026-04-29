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
    QCOMPARE(dialog.m_stepLabel->text(), QStringLiteral("Step 2 of 3: Format"));
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

} // namespace bookhub::gui

using DownloadFlowDialogTest = bookhub::gui::DownloadFlowDialogTest;
QTEST_MAIN(DownloadFlowDialogTest)
#include "test_download_flow_dialog.moc"
