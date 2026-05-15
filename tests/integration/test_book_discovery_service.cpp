#include <QObject>
#include <QString>
#include <QList>

#include "collector/book_discovery_service.h"
#include "collector/source_adapter.h"
#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub::collector;

class BookDiscoveryServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void insertBookIntoDatabase_insertsAndDoesNotDuplicate();
    void strongerIdentifier_promotesPrimaryKeyAndPreservesDependents();
    void repeatedDiscovery_sameSource_doesNotExplodeRowCounts();
    void sharedLCCN_differentSources_mergesIntoOneBook();
    void firstWriterMetadata_preservedWhenLaterSourceDiffers();
};

namespace {
DiscoveredBook makeFallbackBook()
{
    DiscoveredBook book;
    book.sourceId = QStringLiteral("1342");
    book.resolvedId = QStringLiteral("gutenberg:1342");
    book.title = QStringLiteral("Pride and Prejudice");
    book.authors = {QStringLiteral("Jane Austen")};
    book.languages = {QStringLiteral("English")};
    book.identifiers = {BookIdentifier{QStringLiteral("gutenberg"), QStringLiteral("1342")}};
    book.formats.insert(QStringLiteral("epub_1"), QStringLiteral("https://example.test/1342.epub"));
    return book;
}
} // namespace

void BookDiscoveryServiceTest::insertBookIntoDatabase_insertsAndDoesNotDuplicate()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("discovery_insert")));
    QVERIFY(testDb.createSchema());

    BookDiscoveryService service(testDb.connection());
    const DiscoveredBook book = makeFallbackBook();

    service.insertBookIntoDatabase(book, QStringLiteral("Gutenberg"));
    service.insertBookIntoDatabase(book, QStringLiteral("Gutenberg"));

    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM books")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM book_identifiers")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM editions")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM formats")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM sources")), 1);
}

void BookDiscoveryServiceTest::strongerIdentifier_promotesPrimaryKeyAndPreservesDependents()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("discovery_promote")));
    QVERIFY(testDb.createSchema());

    BookDiscoveryService service(testDb.connection());
    service.insertBookIntoDatabase(makeFallbackBook(), QStringLiteral("Gutenberg"));

    const int editionId = testDb.scalarInt(QStringLiteral(
        "SELECT id FROM editions WHERE book_id = 'gutenberg:1342'"));
    QVERIFY(editionId > 0);
    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO library_items (book_id, edition_id, status) "
        "VALUES ('gutenberg:1342', %1, 'saved')").arg(editionId)));

    DiscoveredBook stronger = makeFallbackBook();
    stronger.resolvedId = QStringLiteral("lccn:n78095332");
    stronger.identifiers = {
        BookIdentifier{QStringLiteral("lccn"), QStringLiteral("n78095332")},
        BookIdentifier{QStringLiteral("gutenberg"), QStringLiteral("1342")}
    };

    service.insertBookIntoDatabase(stronger, QStringLiteral("Gutenberg"));

    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM books WHERE book_id = 'lccn:n78095332'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM books WHERE book_id = 'gutenberg:1342'")), 0);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE book_id = 'lccn:n78095332'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM book_identifiers WHERE book_id = 'lccn:n78095332' AND type = 'gutenberg' AND value = '1342'")),
        1);
}

void BookDiscoveryServiceTest::repeatedDiscovery_sameSource_doesNotExplodeRowCounts()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("discovery_repeated")));
    QVERIFY(testDb.createSchema());

    BookDiscoveryService service(testDb.connection());
    DiscoveredBook book = makeFallbackBook();

    for (int i = 0; i < 5; ++i) {
        service.insertBookIntoDatabase(book, QStringLiteral("Gutenberg"));
    }

    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM books")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM editions")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM sources")), 1);
}

void BookDiscoveryServiceTest::sharedLCCN_differentSources_mergesIntoOneBook()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("discovery_shared_lccn")));
    QVERIFY(testDb.createSchema());

    BookDiscoveryService service(testDb.connection());

    DiscoveredBook gutenbergBook;
    gutenbergBook.sourceId = QStringLiteral("1342");
    gutenbergBook.resolvedId = QStringLiteral("lccn:n78095332");
    gutenbergBook.title = QStringLiteral("Pride and Prejudice");
    gutenbergBook.authors = {QStringLiteral("Jane Austen")};
    gutenbergBook.identifiers = {
        BookIdentifier{QStringLiteral("lccn"), QStringLiteral("n78095332")},
        BookIdentifier{QStringLiteral("gutenberg"), QStringLiteral("1342")}
    };
    gutenbergBook.formats.insert(QStringLiteral("epub_1"),
                                  QStringLiteral("https://example.test/1342.epub"));
    service.insertBookIntoDatabase(gutenbergBook, QStringLiteral("Gutenberg"));

    DiscoveredBook archiveBook;
    archiveBook.sourceId = QStringLiteral("archive-12345");
    archiveBook.resolvedId = QStringLiteral("lccn:n78095332");
    archiveBook.title = QStringLiteral("Pride and Prejudice");
    archiveBook.authors = {QStringLiteral("Jane Austen")};
    archiveBook.identifiers = {BookIdentifier{QStringLiteral("lccn"), QStringLiteral("n78095332")}};
    archiveBook.formats.insert(QStringLiteral("txt"),
                                QStringLiteral("https://archive.org/download/12345/12345.txt"));
    service.insertBookIntoDatabase(archiveBook, QStringLiteral("Archive.org"));

    // Deduplication must yield exactly one book, one edition, and two formats — one per source.
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM books")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM editions")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM formats")), 2);
    // Both source names must be present under the merged book.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(DISTINCT s.source_name) "
        "FROM sources s "
        "JOIN formats f ON f.id = s.format_id "
        "JOIN editions e ON e.id = f.edition_id "
        "WHERE e.book_id = 'lccn:n78095332'")), 2);
}

void BookDiscoveryServiceTest::firstWriterMetadata_preservedWhenLaterSourceDiffers()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("discovery_first_writer")));
    QVERIFY(testDb.createSchema());

    BookDiscoveryService service(testDb.connection());

    DiscoveredBook firstBook;
    firstBook.sourceId = QStringLiteral("1342");
    firstBook.resolvedId = QStringLiteral("gutenberg:1342");
    firstBook.title = QStringLiteral("Pride and Prejudice by Jane Austen");
    firstBook.authors = {QStringLiteral("Jane Austen")};
    firstBook.identifiers = {BookIdentifier{QStringLiteral("gutenberg"), QStringLiteral("1342")}};
    service.insertBookIntoDatabase(firstBook, QStringLiteral("Gutenberg"));

    const QString originalTitle = testDb.scalarString(
        QStringLiteral("SELECT title FROM books WHERE book_id = 'gutenberg:1342'"));

    DiscoveredBook secondBook;
    secondBook.sourceId = QStringLiteral("different-source");
    secondBook.resolvedId = QStringLiteral("gutenberg:1342");
    secondBook.title = QStringLiteral("Different Title That Should Not Overwrite");
    secondBook.authors = {QStringLiteral("Different Author")};
    secondBook.identifiers = {BookIdentifier{QStringLiteral("gutenberg"), QStringLiteral("1342")}};
    service.insertBookIntoDatabase(secondBook, QStringLiteral("OtherSource"));

    const QString finalTitle = testDb.scalarString(
        QStringLiteral("SELECT title FROM books WHERE book_id = 'gutenberg:1342'"));
    QCOMPARE(finalTitle, originalTitle);
}

QTEST_GUILESS_MAIN(BookDiscoveryServiceTest)

#include "test_book_discovery_service.moc"
