#include "gui/services/search_service.h"
#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub::gui;

class SearchServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void search_appliesAndSemanticsAcrossFilters();
    void search_supportsAudiobookFilterAndSorting();
};

void SearchServiceTest::search_appliesAndSemanticsAcrossFilters()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    SearchService service;
    SearchParams params;
    params.keyword = QStringLiteral("Pride");
    params.languages = {QStringLiteral("English")};
    params.sources = {QStringLiteral("Gutenberg")};
    params.genres = {QStringLiteral("Romance")};

    const QList<SearchResult> results = service.search(params);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().bookId, QStringLiteral("lccn:n78095332"));
}

void SearchServiceTest::search_supportsAudiobookFilterAndSorting()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());
    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "UPDATE library_items SET status = 'audiobook_ready' WHERE book_id = 'lccn:n79025140'")));

    SearchService service;
    SearchParams audiobook;
    audiobook.audiobookOnly = true;
    const QList<SearchResult> audioResults = service.search(audiobook);
    QCOMPARE(audioResults.size(), 1);
    QCOMPARE(audioResults.first().bookId, QStringLiteral("lccn:n79025140"));

    SearchParams sortByAuthor;
    sortByAuthor.keyword = QStringLiteral(" ");
    sortByAuthor.sortColumn = QStringLiteral("author");
    const QList<SearchResult> sorted = service.search(sortByAuthor);
    QVERIFY(sorted.size() >= 3);
    QCOMPARE(sorted.first().author, QStringLiteral("Alexandre Dumas"));
}

QTEST_GUILESS_MAIN(SearchServiceTest)

#include "test_search_service.moc"
