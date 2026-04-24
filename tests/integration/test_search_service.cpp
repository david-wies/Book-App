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
    void search_escapesLikeWildcardsInKeyword();
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

    // No keyword — returns all books in the database ordered by author.
    // Sample data has exactly 3 books; Alexandre Dumas sorts first alphabetically.
    SearchParams sortByAuthor;
    sortByAuthor.sortColumn = QStringLiteral("author");
    const QList<SearchResult> sorted = service.search(sortByAuthor);
    QCOMPARE(sorted.size(), 3);
    QCOMPARE(sorted.first().author, QStringLiteral("Alexandre Dumas"));
}

void SearchServiceTest::search_escapesLikeWildcardsInKeyword()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    // "100% Complete" contains a literal '%'. Without escaping, searching for
    // "100%" produces LIKE '%100%%', which also matches "1001 Adventures"
    // because the unescaped '%' acts as a wildcard after "100".
    // "A_B Catalogue" contains a literal '_'. Without escaping, searching for
    // "A_B" produces LIKE '%A_B%', which also matches "ACB Compendium" because
    // the unescaped '_' acts as a single-character wildcard.
    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO books (book_id, title, author) VALUES "
        "('book:percent', '100% Complete',   'A'), "
        "('book:wild',    '1001 Adventures', 'B'), "
        "('book:under',   'A_B Catalogue',   'C'), "
        "('book:any',     'ACB Compendium',  'D')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES "
        "('book:percent', 'English'), ('book:wild', 'English'), "
        "('book:under', 'English'), ('book:any', 'English')")));

    SearchService service;
    SearchParams params;

    params.keyword = QStringLiteral("100%");
    const QList<SearchResult> percentResults = service.search(params);
    QCOMPARE(percentResults.size(), 1);
    QCOMPARE(percentResults.first().bookId, QStringLiteral("book:percent"));

    params.keyword = QStringLiteral("A_B");
    const QList<SearchResult> underResults = service.search(params);
    QCOMPARE(underResults.size(), 1);
    QCOMPARE(underResults.first().bookId, QStringLiteral("book:under"));
}

QTEST_GUILESS_MAIN(SearchServiceTest)

#include "test_search_service.moc"
