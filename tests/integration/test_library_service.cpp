#include "gui/services/library_service.h"
#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub::gui;

class LibraryServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void fetchItems_returnsRowsInRequestedSortOrder();
    void writeOperations_mutateRows();
};

void LibraryServiceTest::fetchItems_returnsRowsInRequestedSortOrder()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    const QList<LibraryItem> byTitle =
        internal::fetchItems(QStringLiteral("title"), testDb.connection());
    QVERIFY(byTitle.size() >= 2);
    QCOMPARE(byTitle.first().title, QStringLiteral("Pride and Prejudice"));

    const QList<LibraryItem> byStatus =
        internal::fetchItems(QStringLiteral("status"), testDb.connection());
    QVERIFY(byStatus.size() >= 2);
    QCOMPARE(byStatus.first().status, QStringLiteral("downloaded"));
}

void LibraryServiceTest::writeOperations_mutateRows()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    const int editionId = testDb.scalarInt(QStringLiteral(
        "SELECT id FROM editions WHERE book_id = 'gutenberg:1184' AND language = 'English'"));
    QVERIFY(editionId > 0);

    const int addedId =
        internal::addBook(QStringLiteral("gutenberg:1184"), editionId, testDb.connection());
    QVERIFY(addedId > 0);

    QVERIFY(internal::updateStatus(addedId, QStringLiteral("downloaded"), testDb.connection()));
    QCOMPARE(testDb.scalarString(QStringLiteral(
        "SELECT status FROM library_items WHERE id = %1").arg(addedId)),
        QStringLiteral("downloaded"));

    QVERIFY(internal::removeBook(addedId, testDb.connection()));
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE id = %1").arg(addedId)),
        0);
}

QTEST_GUILESS_MAIN(LibraryServiceTest)

#include "test_library_service.moc"
