#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub;

class DatabaseSchemaTest : public QObject
{
    Q_OBJECT

private slots:
    void databaseFilePath_resolvesNextToExecutable();
    void createSchema_createsCoreTablesAndVersion();
    void verifySchemaVersion_freshDatabaseIsValid();
    void verifySchemaVersion_mismatchedVersionIsRejected();
    void constraints_andSampleData_behaveAsExpected();
};

void DatabaseSchemaTest::databaseFilePath_resolvesNextToExecutable()
{
    const QString expected =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("bookhub.db"));
    QCOMPARE(db::databaseFilePath(), expected);
}

void DatabaseSchemaTest::createSchema_createsCoreTablesAndVersion()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QCOMPARE(testDb.scalarInt(QStringLiteral("PRAGMA user_version")), db::kSchemaVersion);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
        "('books','book_identifiers','editions','genres','book_genres','formats','sources','library_items')")),
        8);
}

void DatabaseSchemaTest::verifySchemaVersion_freshDatabaseIsValid()
{
    // A database that has been opened but never had createSchema() called has
    // user_version=0 and no tables. verifySchemaVersion treats this as a fresh,
    // valid database — the caller is expected to call createSchema() immediately
    // after receiving true from this check.
    bookhub::tests::TestDatabase freshDb;
    QVERIFY(freshDb.open());
    QVERIFY(db::verifySchemaVersion(freshDb.connection()));
}

void DatabaseSchemaTest::verifySchemaVersion_mismatchedVersionIsRejected()
{
    bookhub::tests::TestDatabase mismatchDb;
    QVERIFY(mismatchDb.open(QStringLiteral("mismatch")));
    QVERIFY(mismatchDb.createSchema());
    QVERIFY(bookhub::tests::execSql(mismatchDb.database(), QStringLiteral("PRAGMA user_version = 99")));
    QVERIFY(!db::verifySchemaVersion(mismatchDb.connection()));
}

void DatabaseSchemaTest::constraints_andSampleData_behaveAsExpected()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());
    QVERIFY(testDb.insertSampleData());

    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM books")), 3);
    QCOMPARE(testDb.scalarInt(QStringLiteral("SELECT COUNT(*) FROM library_items")), 2);

    QVERIFY(!bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO book_identifiers (book_id, type, value) "
        "VALUES ('lccn:n78095332', 'gutenberg', '1342')")));

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "UPDATE books SET book_id = 'lccn:updated' WHERE book_id = 'lccn:n78095332'")));
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM book_identifiers WHERE book_id = 'lccn:updated' AND type = 'gutenberg' AND value = '1342'")),
        1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE book_id = 'lccn:updated'")),
        1);

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "DELETE FROM books WHERE book_id = 'lccn:updated'")));
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM book_identifiers WHERE book_id = 'lccn:updated'")),
        0);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE book_id = 'lccn:updated'")),
        0);
}

QTEST_GUILESS_MAIN(DatabaseSchemaTest)

#include "test_database_schema.moc"
