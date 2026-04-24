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
};

static DiscoveredBook makeFallbackBook()
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

QTEST_GUILESS_MAIN(BookDiscoveryServiceTest)

#include "test_book_discovery_service.moc"
