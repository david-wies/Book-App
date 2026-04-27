#include "gui/services/explore_service.h"
#include "support/test_database_utils.h"

#include <QtTest>

using namespace bookhub::gui;

class ExploreServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void fetchCategories_returnsCounts();
    void fetchTrendingAndGenreBooks_returnExpectedWindows();
};

void ExploreServiceTest::fetchCategories_returnsCounts()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());
    QVERIFY(testDb.insertSampleData());

    const QList<ExploreCategory> categories =
        internal::fetchCategories(testDb.connection());
    QVERIFY(!categories.isEmpty());
    QCOMPARE(categories.first().genreName, QStringLiteral("Classic"));
    QCOMPARE(categories.first().bookCount, 3);
}

void ExploreServiceTest::fetchTrendingAndGenreBooks_returnExpectedWindows()
{
    bookhub::tests::TestDatabase testDb;
    QVERIFY(testDb.open());
    QVERIFY(testDb.createSchema());

    for (int i = 1; i <= 25; ++i) {
        QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
            "INSERT INTO books (book_id, title, author) VALUES ('book:%1', 'Book %1', 'Author %1')").arg(i)));
        QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
            "INSERT OR IGNORE INTO genres (id, genre_name) VALUES (%1, 'Genre%1')").arg(i)));
        QVERIFY(bookhub::tests::execSql(testDb.database(), QStringLiteral(
            "INSERT INTO book_genres (book_id, genre_id) VALUES ('book:%1', %2)").arg(i).arg(i)));
    }

    const QList<ExploreBook> trending = internal::fetchTrending(testDb.connection());
    const QList<ExploreBook> arrivals = internal::fetchNewArrivals(testDb.connection());
    QVERIFY(!trending.isEmpty());
    QVERIFY(!arrivals.isEmpty());
    // fetchTrending() selects ORDER BY rowid DESC LIMIT 20. Books 1..25 were
    // inserted in ascending order, so book:25 has the highest rowid and is first.
    QCOMPARE(trending.first().bookId, QStringLiteral("book:25"));
    // fetchNewArrivals() applies OFFSET 20 to the same rowid-DESC order, skipping
    // books 25..6. book:5 is the first of the remaining window (5, 4, 3, 2, 1).
    QCOMPARE(arrivals.first().bookId, QStringLiteral("book:5"));

    const QList<ExploreBook> genreBooks =
        internal::fetchBooksForGenre(QStringLiteral("Genre10"), 0, 40, testDb.connection());
    QCOMPARE(genreBooks.size(), 1);
    QCOMPARE(genreBooks.first().bookId, QStringLiteral("book:10"));
}

QTEST_GUILESS_MAIN(ExploreServiceTest)

#include "test_explore_service.moc"
