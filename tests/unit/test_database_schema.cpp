#include "shared/database.h"
#include "support/test_database_utils.h"

#include <QStandardPaths>
#include <QtTest>

using namespace bookhub;

class DatabaseSchemaTest : public QObject
{
    Q_OBJECT

private slots:
    void databaseFilePath_resolvesToAppDataLocation();
    void createSchema_createsCoreTablesAndVersion();
    void verifySchemaVersion_freshDatabaseIsValid();
    void verifySchemaVersion_mismatchedVersionIsRejected();
    void constraints_andSampleData_behaveAsExpected();
    void migration_v4ToV5_stripsFormatSuffixAndMarcTitles();
    void migration_v5ToV6_stripsMimeParamsAndFixesDoubleColon();
    void migration_v6ToV7_renamesTextPlainToPlain();
};

void DatabaseSchemaTest::databaseFilePath_resolvesToAppDataLocation()
{
    const QString expected = QDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("bookhub.db"));
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
        "VALUES ('gutenberg:1342', 'gutenberg', '1342')")));

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "UPDATE books SET book_id = 'gutenberg:updated' WHERE book_id = 'gutenberg:1342'")));
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM book_identifiers WHERE book_id = 'gutenberg:updated' AND type = 'gutenberg' AND value = '1342'")),
        1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE book_id = 'gutenberg:updated'")),
        1);

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "DELETE FROM books WHERE book_id = 'gutenberg:updated'")));
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM book_identifiers WHERE book_id = 'gutenberg:updated'")),
        0);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE book_id = 'gutenberg:updated'")),
        0);
}

void DatabaseSchemaTest::migration_v4ToV5_stripsFormatSuffixAndMarcTitles()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("v4tov5")));
    QVERIFY(testDb.createSchema());

    // Downgrade to v4 to simulate a pre-migration database.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("PRAGMA user_version = 4")));

    // Insert a book with a MARC-contaminated title and a names-authority book_id.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO books (book_id, title) VALUES ('lccn:n11111111', 'Foo $b Bar')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO book_identifiers (book_id, type, value) "
                       "VALUES ('lccn:n11111111', 'gutenberg', '9999')")));

    // Insert an edition and two epub formats with _N suffixes.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO editions (id, book_id, language) "
                       "VALUES (100, 'lccn:n11111111', 'English')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO formats (id, edition_id, format_type) "
                       "VALUES (100, 100, 'epub_1'), (101, 100, 'epub_2')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO sources (format_id, source_name, download_link) "
                       "VALUES (100, 'G', 'http://a'), (101, 'G', 'http://b')")));

    // Run the migration.
    QVERIFY(db::verifySchemaVersion(testDb.connection()));

    // book_id must be remapped from the names-authority LCCN to gutenberg:9999.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM books WHERE book_id = 'gutenberg:9999'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM books WHERE book_id = 'lccn:n11111111'")), 0);

    // Title must have the MARC marker replaced.
    QCOMPARE(testDb.scalarString(QStringLiteral(
        "SELECT title FROM books WHERE book_id = 'gutenberg:9999'")),
        QStringLiteral("Foo: Bar"));

    // Only one bare 'epub' format row must remain; no _1 or _2 rows.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE format_type = 'epub'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE format_type LIKE 'epub_%'")), 0);

    // Schema version must be at the current target.
    QCOMPARE(testDb.scalarInt(QStringLiteral("PRAGMA user_version")), db::kSchemaVersion);
}

void DatabaseSchemaTest::migration_v5ToV6_stripsMimeParamsAndFixesDoubleColon()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("v5tov6")));
    QVERIFY(testDb.createSchema());

    // Downgrade to v5 to simulate a pre-migration database.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("PRAGMA user_version = 5")));

    // Insert a book whose title has double-colon artefact from old MARC regex.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO books (book_id, title) "
                       "VALUES ('gb:1', 'His Last Bow : : Some Later Reminiscences')")));

    // Insert an edition with a parameterised MIME format type.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO editions (id, book_id, language) "
                       "VALUES (1, 'gb:1', 'English')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO formats (id, edition_id, format_type) "
                       "VALUES (1, 1, 'plain; charset=us_ascii'), (2, 1, 'plain; charset=utf_8')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO sources (format_id, source_name, download_link) "
                       "VALUES (1, 'G', 'http://a'), (2, 'G', 'http://b')")));

    // Run the migration chain.
    QVERIFY(db::verifySchemaVersion(testDb.connection()));

    // Double-colon title must be cleaned.
    QCOMPARE(testDb.scalarString(QStringLiteral(
        "SELECT title FROM books WHERE book_id = 'gb:1'")),
        QStringLiteral("His Last Bow: Some Later Reminiscences"));

    // Only one 'plain' format row must remain; no parameterised rows.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE format_type = 'plain'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE format_type LIKE '%;%'")), 0);

    // Schema must be at the current target.
    QCOMPARE(testDb.scalarInt(QStringLiteral("PRAGMA user_version")), db::kSchemaVersion);
}

void DatabaseSchemaTest::migration_v6ToV7_renamesTextPlainToPlain()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("v6tov7")));
    QVERIFY(testDb.createSchema());

    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("PRAGMA user_version = 6")));

    // Book A: has both 'text_plain' and 'plain' — collision case.
    // text_plain row must be deleted; the existing 'plain' row must survive.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO books (book_id, title) VALUES ('gb:1', 'A'), ('gb:2', 'B')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO editions (id, book_id, language) "
                       "VALUES (1, 'gb:1', 'English'), (2, 'gb:2', 'English')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO formats (id, edition_id, format_type) "
                       "VALUES (1, 1, 'text_plain'), (2, 1, 'plain'), (3, 2, 'text_plain')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("INSERT INTO sources (format_id, source_name, download_link) "
                       "VALUES (1, 'G', 'http://a'), (2, 'G', 'http://b'), (3, 'G', 'http://c')")));

    QVERIFY(db::verifySchemaVersion(testDb.connection()));

    // Collision case: edition 1 must have exactly one 'plain' row.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE edition_id = 1 AND format_type = 'plain'")), 1);
    // Simple rename case: edition 2 must have 'plain', no 'text_plain'.
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE edition_id = 2 AND format_type = 'plain'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM formats WHERE format_type = 'text_plain'")), 0);

    QCOMPARE(testDb.scalarInt(QStringLiteral("PRAGMA user_version")), db::kSchemaVersion);
}

QTEST_GUILESS_MAIN(DatabaseSchemaTest)

#include "test_database_schema.moc"
