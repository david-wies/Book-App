#include "query_worker.h"

#include "services/search_service.h"
#include "services/library_service.h"
#include "services/explore_service.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

namespace bookhub::gui {

QueryWorker::QueryWorker(QObject *parent)
    : QObject(parent)
{}

void QueryWorker::onThreadStarted()
{
    // Open a dedicated SQL connection on the query thread.
    // The path is taken from the GUI thread's default connection so both
    // threads point at the same database file.
    const QString dbPath =
        QSqlDatabase::database(QSqlDatabase::defaultConnection).databaseName();

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                 QLatin1String(kConnectionName));
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "QueryWorker: failed to open gui_query_connection:"
                   << db.lastError().text();
    }
}

void QueryWorker::onThreadFinished()
{
    QSqlDatabase::database(QLatin1String(kConnectionName)).close();
    QSqlDatabase::removeDatabase(QLatin1String(kConnectionName));
}

// ---------------------------------------------------------------------------
// Search handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleSearchRequest(quint64 requestId, SearchParams params,
                                       int offset, int limit)
{
    emit searchCompleted(requestId,
        internal::runSearch(params, offset, limit, QLatin1String(kConnectionName)));
}

void QueryWorker::handleCountRequest(quint64 requestId, SearchParams params)
{
    emit countCompleted(requestId,
        internal::runCount(params, QLatin1String(kConnectionName)));
}

void QueryWorker::handleLanguagesRequest(quint64 requestId)
{
    emit languagesCompleted(requestId,
        internal::fetchLanguages(QLatin1String(kConnectionName)));
}

void QueryWorker::handleSourcesRequest(quint64 requestId)
{
    emit sourcesCompleted(requestId,
        internal::fetchSources(QLatin1String(kConnectionName)));
}

void QueryWorker::handleGenresRequest(quint64 requestId)
{
    emit genresCompleted(requestId,
        internal::fetchGenres(QLatin1String(kConnectionName)));
}

// ---------------------------------------------------------------------------
// Library handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleFetchItemsRequest(quint64 requestId, QString sortColumn)
{
    emit fetchItemsCompleted(requestId,
        internal::fetchItems(sortColumn, QLatin1String(kConnectionName)));
}

void QueryWorker::handleAddBookRequest(quint64 requestId, QString bookId, int editionId)
{
    const int newId = internal::addBook(bookId, editionId, QLatin1String(kConnectionName));
    emit addBookCompleted(requestId, bookId, newId > 0, newId);
}

void QueryWorker::handleRemoveBookRequest(quint64 requestId, int libraryItemId)
{
    emit removeBookCompleted(requestId,
        internal::removeBook(libraryItemId, QLatin1String(kConnectionName)));
}

void QueryWorker::handleUpdateStatusRequest(quint64 requestId, int libraryItemId,
                                             QString status)
{
    emit updateStatusCompleted(requestId,
        internal::updateStatus(libraryItemId, status, QLatin1String(kConnectionName)));
}

// ---------------------------------------------------------------------------
// Explore handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleTrendingRequest(quint64 requestId)
{
    emit trendingCompleted(requestId,
        internal::fetchTrending(QLatin1String(kConnectionName)));
}

void QueryWorker::handleNewArrivalsRequest(quint64 requestId)
{
    emit newArrivalsCompleted(requestId,
        internal::fetchNewArrivals(QLatin1String(kConnectionName)));
}

void QueryWorker::handleCategoriesRequest(quint64 requestId)
{
    emit categoriesCompleted(requestId,
        internal::fetchCategories(QLatin1String(kConnectionName)));
}

void QueryWorker::handleBooksForGenreRequest(quint64 requestId, QString genre,
                                              int offset, int limit)
{
    emit booksForGenreCompleted(requestId,
        internal::fetchBooksForGenre(genre, offset, limit, QLatin1String(kConnectionName)));
}

} // namespace bookhub::gui
