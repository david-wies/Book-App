#include "explore_service.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace bookhub::gui {

ExploreService::ExploreService(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Private helper — executes a simple SELECT and returns rows as ExploreBook
// records. The caller provides the full SQL string (no user input is ever
// interpolated into SQL text here; genre filtering uses bound parameters).
// ---------------------------------------------------------------------------

static QList<ExploreBook> execBookQuery(const QString &sql,
                                        const QVariantList &bindings = {})
{
    QSqlQuery query(QSqlDatabase::database());
    if (!query.prepare(sql)) {
        qWarning() << "ExploreService: prepare failed:" << query.lastError().text();
        return {};
    }
    for (const QVariant &v : bindings)
        query.addBindValue(v);

    if (!query.exec()) {
        qWarning() << "ExploreService: exec failed:" << query.lastError().text();
        return {};
    }

    QList<ExploreBook> result;
    while (query.next()) {
        ExploreBook b;
        b.bookId = query.value(0).toString();
        b.title  = query.value(1).toString();
        b.author = query.value(2).toString();
        result.append(b);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QList<ExploreBook> ExploreService::fetchTrending() const
{
    // rowid DESC gives us the most-recently ingested books, which serves as a
    // reasonable "trending" proxy for MVP until a proper popularity signal
    // (download count, view count) is available.
    return execBookQuery(QStringLiteral(
        "SELECT book_id, title, author FROM books ORDER BY rowid DESC LIMIT 20"));
}

QList<ExploreBook> ExploreService::fetchNewArrivals() const
{
    // New Arrivals is the next window after Trending; OFFSET 20 avoids
    // duplicate cards between the two horizontal carousels.
    return execBookQuery(QStringLiteral(
        "SELECT book_id, title, author FROM books ORDER BY rowid DESC LIMIT 20 OFFSET 20"));
}

QList<ExploreCategory> ExploreService::fetchCategories() const
{
    QSqlQuery query(QSqlDatabase::database());
    if (!query.exec(QStringLiteral(R"(
            SELECT g.genre_name, COUNT(DISTINCT bg.book_id) AS cnt
            FROM genres g
            JOIN book_genres bg ON g.id = bg.genre_id
            GROUP BY g.genre_name
            ORDER BY cnt DESC
        )"))) {
        qWarning() << "ExploreService::fetchCategories failed:"
                   << query.lastError().text();
        return {};
    }

    QList<ExploreCategory> result;
    while (query.next()) {
        ExploreCategory c;
        c.genreName = query.value(0).toString();
        c.bookCount = query.value(1).toInt();
        result.append(c);
    }
    return result;
}

QList<ExploreBook> ExploreService::fetchBooksForGenre(const QString &genre,
                                                       int offset,
                                                       int limit) const
{
    // NOTE: genre is bound as a parameter — never interpolated into SQL text.
    return execBookQuery(QStringLiteral(R"(
        SELECT b.book_id, b.title, b.author
        FROM books b
        JOIN book_genres bg ON b.book_id = bg.book_id
        JOIN genres g ON bg.genre_id = g.id
        WHERE g.genre_name = ?
        ORDER BY b.title
        LIMIT ? OFFSET ?
    )"), {genre, limit, offset});
}

} // namespace bookhub::gui
