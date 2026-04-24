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

    ExploreService service;
    const QList<ExploreCategory> categories = service.fetchCategories();
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

    ExploreService service;
    const QList<ExploreBook> trending = service.fetchTrending();
    const QList<ExploreBook> arrivals = service.fetchNewArrivals();
    QVERIFY(!trending.isEmpty());
    QVERIFY(!arrivals.isEmpty());
    QCOMPARE(trending.first().bookId, QStringLiteral("book:25"));
    QCOMPARE(arrivals.first().bookId, QStringLiteral("book:5"));

    const QList<ExploreBook> genreBooks = service.fetchBooksForGenre(QStringLiteral("Genre10"));
    QCOMPARE(genreBooks.size(), 1);
    QCOMPARE(genreBooks.first().bookId, QStringLiteral("book:10"));
}

QTEST_GUILESS_MAIN(ExploreServiceTest)

#include "test_explore_service.moc"
