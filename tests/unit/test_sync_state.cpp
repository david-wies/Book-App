#include "shared/database.h"
#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub;

class SyncStateTest : public QObject
{
    Q_OBJECT

private slots:
    void createSchema_includesSyncStateTable();
    void getSyncState_returnsNulloptWhenNoRow();
    void beginFetch_initialisesInProgressDownloadingRow();
    void beginFetch_preservesPriorLastModified();
    void recordDownloadProgress_updatesBytesAndEtag();
    void recordDownloadProgress_doesNotClearEtagWhenEmpty();
    void recordDownloadComplete_transitionsToParsing();
    void recordBatchCommit_advancesCursorAndCounter();
    void completeFetch_clearsResumeStateAndSetsCompleted();
    void completeFetch_preservesLastModifiedWhenEmpty();
    void failFetch_preservesResumeState();
    void countBooksForAdapter_matchesByPrefix();
    void migration_v7ToV8_createsSyncStateTable();
    void migration_v7ToV8_preservesExistingData();
};

void SyncStateTest::createSchema_includesSyncStateTable()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name='sync_state'")), 1);

    // Verify CHECK constraint on status: invalid values must be rejected.
    QVERIFY(!bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO sync_state (adapter_id, status) VALUES ('x', 'bogus')")));
    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO sync_state (adapter_id, status) VALUES ('x', 'completed')")));
}

void SyncStateTest::getSyncState_returnsNulloptWhenNoRow()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(!state.has_value());
}

void SyncStateTest::beginFetch_initialisesInProgressDownloadingRow()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"),
                           QStringLiteral("https://example.com/a.tar.bz2"),
                           QStringLiteral("/tmp/a.tar.bz2"),
                           testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->status, QStringLiteral("in_progress"));
    QCOMPARE(state->phase, QStringLiteral("downloading"));
    QCOMPARE(state->downloadUrl, QStringLiteral("https://example.com/a.tar.bz2"));
    QCOMPARE(state->archivePath, QStringLiteral("/tmp/a.tar.bz2"));
    QCOMPARE(state->booksProcessed, qint64{0});
    QCOMPARE(state->bytesDownloaded, qint64{0});
    QCOMPARE(state->bytesTotal, qint64{0});
    QVERIFY(state->lastParsedEntry.isEmpty());
    QVERIFY(state->downloadEtag.isEmpty());
    QVERIFY(state->errorMessage.isEmpty());
    QVERIFY(!state->startedAt.isEmpty());
}

void SyncStateTest::beginFetch_preservesPriorLastModified()
{
    // Reason this exists: a previously-completed fetch sets last_modified.
    // A subsequent beginFetch() opens a new attempt but must keep that value
    // around so the conditional-GET path on the next *completed* attempt can
    // still send If-Modified-Since correctly if the new attempt fails midway.
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO sync_state (adapter_id, status, last_modified) "
        "VALUES ('gutenberg', 'completed', 'Wed, 01 Jan 2025 00:00:00 GMT')")));

    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"),
                           QStringLiteral("https://example.com/a.tar.bz2"),
                           QStringLiteral("/tmp/a.tar.bz2"),
                           testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->status, QStringLiteral("in_progress"));
    QCOMPARE(state->lastModified, QStringLiteral("Wed, 01 Jan 2025 00:00:00 GMT"));
}

void SyncStateTest::recordDownloadProgress_updatesBytesAndEtag()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"), QStringLiteral("u"),
                           QStringLiteral("p"), testDb.connection()));

    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"),
                                       12345, 67890,
                                       QStringLiteral("Thu, 02 Jan 2025 00:00:00 GMT"),
                                       testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->bytesDownloaded, qint64{12345});
    QCOMPARE(state->bytesTotal, qint64{67890});
    QCOMPARE(state->downloadEtag, QStringLiteral("Thu, 02 Jan 2025 00:00:00 GMT"));
}

void SyncStateTest::recordDownloadProgress_doesNotClearEtagWhenEmpty()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"), QStringLiteral("u"),
                           QStringLiteral("p"), testDb.connection()));
    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"), 100, 200,
                                       QStringLiteral("etag-v1"), testDb.connection()));

    // Subsequent progress update without a fresh etag must not wipe the stored one.
    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"), 500, 1000,
                                       QString(), testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->bytesDownloaded, qint64{500});
    QCOMPARE(state->downloadEtag, QStringLiteral("etag-v1"));
}

void SyncStateTest::recordDownloadComplete_transitionsToParsing()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"), QStringLiteral("u"),
                           QStringLiteral("p"), testDb.connection()));
    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"), 800, 1000,
                                       QStringLiteral("etag"), testDb.connection()));

    QVERIFY(db::recordDownloadComplete(QStringLiteral("gutenberg"), 1000,
                                       testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->status, QStringLiteral("in_progress"));
    QCOMPARE(state->phase, QStringLiteral("parsing"));
    QCOMPARE(state->bytesTotal, qint64{1000});
    QCOMPARE(state->bytesDownloaded, qint64{1000});
    QCOMPARE(state->booksProcessed, qint64{0});
    QVERIFY(state->lastParsedEntry.isEmpty());
}

void SyncStateTest::recordBatchCommit_advancesCursorAndCounter()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"), QStringLiteral("u"),
                           QStringLiteral("p"), testDb.connection()));
    QVERIFY(db::recordDownloadComplete(QStringLiteral("gutenberg"), 1, testDb.connection()));

    QVERIFY(db::recordBatchCommit(QStringLiteral("gutenberg"),
                                  QStringLiteral("cache/epub/100/pg100.rdf"),
                                  500, testDb.connection()));
    QVERIFY(db::recordBatchCommit(QStringLiteral("gutenberg"),
                                  QStringLiteral("cache/epub/200/pg200.rdf"),
                                  300, testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->lastParsedEntry, QStringLiteral("cache/epub/200/pg200.rdf"));
    QCOMPARE(state->booksProcessed, qint64{800});
}

void SyncStateTest::completeFetch_clearsResumeStateAndSetsCompleted()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"),
                           QStringLiteral("https://x"),
                           QStringLiteral("/tmp/a"), testDb.connection()));
    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"), 1000, 1000,
                                       QStringLiteral("etag"), testDb.connection()));
    QVERIFY(db::recordDownloadComplete(QStringLiteral("gutenberg"), 1000,
                                       testDb.connection()));
    QVERIFY(db::recordBatchCommit(QStringLiteral("gutenberg"),
                                  QStringLiteral("entry-X"), 50, testDb.connection()));

    QVERIFY(db::completeFetch(QStringLiteral("gutenberg"),
                              QStringLiteral("Fri, 03 Jan 2025 00:00:00 GMT"),
                              testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->status, QStringLiteral("completed"));
    QVERIFY(state->phase.isEmpty());
    QCOMPARE(state->lastModified, QStringLiteral("Fri, 03 Jan 2025 00:00:00 GMT"));
    QVERIFY(!state->completedAt.isEmpty());
    QVERIFY(state->downloadUrl.isEmpty());
    QVERIFY(state->downloadEtag.isEmpty());
    QVERIFY(state->archivePath.isEmpty());
    QCOMPARE(state->bytesDownloaded, qint64{0});
    QCOMPARE(state->bytesTotal, qint64{0});
    QVERIFY(state->lastParsedEntry.isEmpty());
    QVERIFY(state->errorMessage.isEmpty());
}

void SyncStateTest::completeFetch_preservesLastModifiedWhenEmpty()
{
    // 304-Not-Modified path: completeFetch is called with empty lastModified,
    // because the response had no body and no header refresh.  The previously
    // stored value must survive.
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO sync_state (adapter_id, status, last_modified) "
        "VALUES ('gutenberg', 'completed', 'KEEPME')")));

    QVERIFY(db::completeFetch(QStringLiteral("gutenberg"), QString(), testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->lastModified, QStringLiteral("KEEPME"));
    QCOMPARE(state->status, QStringLiteral("completed"));
}

void SyncStateTest::failFetch_preservesResumeState()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(db::beginFetch(QStringLiteral("gutenberg"),
                           QStringLiteral("url"),
                           QStringLiteral("/tmp/a"), testDb.connection()));
    QVERIFY(db::recordDownloadProgress(QStringLiteral("gutenberg"), 1234, 5678,
                                       QStringLiteral("etag"), testDb.connection()));

    QVERIFY(db::failFetch(QStringLiteral("gutenberg"),
                          QStringLiteral("Connection reset"), testDb.connection()));

    const auto state = db::getSyncState(QStringLiteral("gutenberg"), testDb.connection());
    QVERIFY(state.has_value());
    QCOMPARE(state->status, QStringLiteral("failed"));
    QCOMPARE(state->errorMessage, QStringLiteral("Connection reset"));
    // Resume cursors are preserved so a future attempt can pick up.
    QCOMPARE(state->bytesDownloaded, qint64{1234});
    QCOMPARE(state->downloadEtag, QStringLiteral("etag"));
    QCOMPARE(state->archivePath, QStringLiteral("/tmp/a"));
}

void SyncStateTest::countBooksForAdapter_matchesByPrefix()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO books (book_id, title) VALUES "
        "('gutenberg:1', 'A'), ('gutenberg:2', 'B'), "
        "('lccn:42', 'C'), ('benyehuda:7', 'D')")));

    QCOMPARE(db::countBooksForAdapter(QStringLiteral("gutenberg"), testDb.connection()),
             qint64{2});
    QCOMPARE(db::countBooksForAdapter(QStringLiteral("benyehuda"), testDb.connection()),
             qint64{1});
    QCOMPARE(db::countBooksForAdapter(QStringLiteral("nonexistent"), testDb.connection()),
             qint64{0});
}

void SyncStateTest::migration_v7ToV8_createsSyncStateTable()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("v7tov8_create")));
    QVERIFY(testDb.createSchema());

    // Downgrade to v7 and drop the sync_state table to simulate a pre-v8 DB.
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("DROP TABLE sync_state")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("PRAGMA user_version = 7")));

    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name='sync_state'")), 0);

    QVERIFY(db::verifySchemaVersion(testDb.connection()));

    QCOMPARE(testDb.scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name='sync_state'")), 1);
    QCOMPARE(testDb.scalarInt(QStringLiteral("PRAGMA user_version")), db::kSchemaVersion);
}

void SyncStateTest::migration_v7ToV8_preservesExistingData()
{
    // The v7→v8 migration is purely additive — books and other tables must be untouched.
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open(QStringLiteral("v7tov8_preserve")));
    QVERIFY(testDb.createSchema());

    QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
        "INSERT INTO books (book_id, title) VALUES ('gutenberg:42', 'Title')")));

    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("DROP TABLE sync_state")));
    QVERIFY(bookhub::tests::execSql(testDb.database(),
        QStringLiteral("PRAGMA user_version = 7")));

    QVERIFY(db::verifySchemaVersion(testDb.connection()));

    QCOMPARE(testDb.scalarString(QStringLiteral(
        "SELECT title FROM books WHERE book_id = 'gutenberg:42'")),
        QStringLiteral("Title"));
}

QTEST_GUILESS_MAIN(SyncStateTest)

#include "test_sync_state.moc"
