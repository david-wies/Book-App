#include "explore_service.h"
#include "../query_worker.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace bookhub::gui {

ExploreService::ExploreService(QObject *parent)
    : QObject(parent)
{}

void ExploreService::connectToWorker(QueryWorker *worker)
{
    connect(this, &ExploreService::trendingRequested,
            worker, &QueryWorker::handleTrendingRequest);
    connect(this, &ExploreService::newArrivalsRequested,
            worker, &QueryWorker::handleNewArrivalsRequest);
    connect(this, &ExploreService::categoriesRequested,
            worker, &QueryWorker::handleCategoriesRequest);
    connect(this, &ExploreService::booksForGenreRequested,
            worker, &QueryWorker::handleBooksForGenreRequest);

    connect(worker, &QueryWorker::trendingCompleted, this,
            [this](quint64 id, QList<ExploreBook> books) {
                --m_pendingCount;
                emit trendingCompleted(id, books);
            });
    connect(worker, &QueryWorker::newArrivalsCompleted, this,
            [this](quint64 id, QList<ExploreBook> books) {
                --m_pendingCount;
                emit newArrivalsCompleted(id, books);
            });
    connect(worker, &QueryWorker::categoriesCompleted, this,
            [this](quint64 id, QList<ExploreCategory> cats) {
                --m_pendingCount;
                emit categoriesCompleted(id, cats);
            });
    connect(worker, &QueryWorker::booksForGenreCompleted, this,
            [this](quint64 id, QList<ExploreBook> books) {
                --m_pendingCount;
                emit booksForGenreCompleted(id, books);
            });
}

bool ExploreService::isBusy() const
{
    return m_pendingCount > 0;
}

quint64 ExploreService::requestTrending()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit trendingRequested(id);
    return id;
}

quint64 ExploreService::requestNewArrivals()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit newArrivalsRequested(id);
    return id;
}

quint64 ExploreService::requestCategories()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit categoriesRequested(id);
    return id;
}

quint64 ExploreService::requestBooksForGenre(const QString &genre, int offset, int limit)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit booksForGenreRequested(id, genre, offset, limit);
    return id;
}

// ---------------------------------------------------------------------------
// Internal free functions
// ---------------------------------------------------------------------------

namespace internal {

static QList<ExploreBook> execBookQuery(const QString &sql,
                                        const QVariantList &bindings,
                                        const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.prepare(sql)) {
        qWarning() << "internal::execBookQuery prepare failed:" << query.lastError().text();
        return {};
    }
    for (const QVariant &v : bindings)
        query.addBindValue(v);

    if (!query.exec()) {
        qWarning() << "internal::execBookQuery exec failed:" << query.lastError().text();
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

QList<ExploreBook> fetchTrending(const QString &connectionName)
{
    return execBookQuery(QStringLiteral(
        "SELECT book_id, title, author FROM books ORDER BY rowid DESC LIMIT 20"),
        {}, connectionName);
}

QList<ExploreBook> fetchNewArrivals(const QString &connectionName)
{
    return execBookQuery(QStringLiteral(
        "SELECT book_id, title, author FROM books ORDER BY rowid DESC LIMIT 20 OFFSET 20"),
        {}, connectionName);
}

QList<ExploreCategory> fetchCategories(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QStringLiteral(R"(
            SELECT g.genre_name, COUNT(DISTINCT bg.book_id) AS cnt
            FROM genres g
            JOIN book_genres bg ON g.id = bg.genre_id
            GROUP BY g.genre_name
            ORDER BY cnt DESC
        )"))) {
        qWarning() << "internal::fetchCategories failed:" << query.lastError().text();
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

QList<ExploreBook> fetchBooksForGenre(const QString &genre, int offset, int limit,
                                       const QString &connectionName)
{
    return execBookQuery(QStringLiteral(R"(
        SELECT b.book_id, b.title, b.author
        FROM books b
        JOIN book_genres bg ON b.book_id = bg.book_id
        JOIN genres g ON bg.genre_id = g.id
        WHERE g.genre_name = ?
        ORDER BY b.title
        LIMIT ? OFFSET ?
    )"), {genre, limit, offset}, connectionName);
}

} // namespace internal

} // namespace bookhub::gui
