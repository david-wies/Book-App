#include "gui/services/library_service.h"
#include "support/test_database_utils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace bookhub::gui;

class LibraryServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void fetchItems_returnsRowsInRequestedSortOrder();
    void writeOperations_emitSignalsAndMutateRows();
};

void LibraryServiceTest::fetchItems_returnsRowsInRequestedSortOrder()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    LibraryService service;
    const QList<LibraryItem> byTitle = service.fetchItems(QStringLiteral("title"));
    QVERIFY(byTitle.size() >= 2);
    QCOMPARE(byTitle.first().title, QStringLiteral("Pride and Prejudice"));

    const QList<LibraryItem> byStatus = service.fetchItems(QStringLiteral("status"));
    QVERIFY(byStatus.size() >= 2);
    QCOMPARE(byStatus.first().status, QStringLiteral("downloaded"));
}

void LibraryServiceTest::writeOperations_emitSignalsAndMutateRows()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    LibraryService service;
    QSignalSpy spy(&service, &LibraryService::libraryChanged);

    const int addedId = service.addBook(QStringLiteral("gutenberg:1184"), 4);
    QVERIFY(addedId > 0);
    QCOMPARE(spy.count(), 1);

    QVERIFY(service.updateStatus(addedId, QStringLiteral("downloaded")));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(testDb.scalarString(QStringLiteral(
        "SELECT status FROM library_items WHERE id = %1").arg(addedId)),
        QStringLiteral("downloaded"));

    QVERIFY(service.removeBook(addedId));
    QCOMPARE(spy.count(), 3);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE id = %1").arg(addedId)),
        0);
}

QTEST_GUILESS_MAIN(LibraryServiceTest)

#include "test_library_service.moc"
