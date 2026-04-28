#include "query_worker.h"

#include "services/book_details_service.h"
#include "services/explore_service.h"
#include "services/library_service.h"
#include "services/search_service.h"

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
                                                 conn());
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning() << "QueryWorker: failed to open gui_query_connection:"
                   << db.lastError().text();
    }
}

void QueryWorker::onThreadFinished()
{
    QSqlDatabase::database(conn()).close();
    QSqlDatabase::removeDatabase(conn());
}

// ---------------------------------------------------------------------------
// Search handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleSearchRequest(quint64 requestId, SearchParams params,
                                       int offset, int limit)
{
    emit searchCompleted(requestId,
        internal::runSearch(params, offset, limit, conn()));
}

void QueryWorker::handleCountRequest(quint64 requestId, SearchParams params)
{
    emit countCompleted(requestId,
        internal::runCount(params, conn()));
}

void QueryWorker::handleLanguagesRequest(quint64 requestId)
{
    emit languagesCompleted(requestId,
        internal::fetchLanguages(conn()));
}

void QueryWorker::handleSourcesRequest(quint64 requestId)
{
    emit sourcesCompleted(requestId,
        internal::fetchSources(conn()));
}

void QueryWorker::handleGenresRequest(quint64 requestId)
{
    emit genresCompleted(requestId,
        internal::fetchGenres(conn()));
}

// ---------------------------------------------------------------------------
// Library handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleFetchItemsRequest(quint64 requestId, QString sortColumn)
{
    emit fetchItemsCompleted(requestId,
        internal::fetchItems(sortColumn, conn()));
}

void QueryWorker::handleAddBookRequest(quint64 requestId, QString bookId, int editionId)
{
    const int newId = internal::addBook(bookId, editionId, conn());
    emit addBookCompleted(requestId, bookId, newId > 0, newId);
}

void QueryWorker::handleRemoveBookRequest(quint64 requestId, int libraryItemId, QString bookId)
{
    const bool ok = internal::removeBook(libraryItemId, conn());
    emit removeBookCompleted(requestId, bookId, ok);
}

void QueryWorker::handleRemoveBookByBookIdRequest(quint64 requestId, QString bookId)
{
    const bool ok = internal::removeBookByBookId(bookId, conn());
    emit removeBookCompleted(requestId, bookId, ok);
}

void QueryWorker::handleUpdateStatusRequest(quint64 requestId, int libraryItemId,
                                             QString status)
{
    emit updateStatusCompleted(requestId,
        internal::updateStatus(libraryItemId, status, conn()));
}

// ---------------------------------------------------------------------------
// Explore handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleTrendingRequest(quint64 requestId)
{
    emit trendingCompleted(requestId,
        internal::fetchTrending(conn()));
}

void QueryWorker::handleNewArrivalsRequest(quint64 requestId)
{
    emit newArrivalsCompleted(requestId,
        internal::fetchNewArrivals(conn()));
}

void QueryWorker::handleCategoriesRequest(quint64 requestId)
{
    emit categoriesCompleted(requestId,
        internal::fetchCategories(conn()));
}

void QueryWorker::handleBooksForGenreRequest(quint64 requestId, QString genre,
                                              int offset, int limit)
{
    emit booksForGenreCompleted(requestId,
        internal::fetchBooksForGenre(genre, offset, limit, conn()));
}

// ---------------------------------------------------------------------------
// Book details handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleBookDetailsRequest(quint64 requestId, QString bookId)
{
    emit bookDetailsCompleted(requestId,
        internal::fetchBookDetails(bookId, conn()));
}

void QueryWorker::handleFormatsForEditionRequest(quint64 requestId, int editionId)
{
    emit formatsForEditionCompleted(requestId,
        internal::fetchFormatsForEdition(editionId, conn()));
}

} // namespace bookhub::gui
