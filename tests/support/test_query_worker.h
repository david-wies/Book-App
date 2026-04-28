#pragma once

#include "gui/query_worker.h"
#include "gui/services/book_details_service.h"
#include "gui/services/explore_service.h"
#include "gui/services/library_service.h"
#include "gui/services/search_service.h"

#include <QSqlDatabase>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// TestQueryWorker — synchronous stand-in for QueryWorker used in GUI tests.
//
// Never moved to a QThread; stays on the GUI thread so Qt::AutoConnection
// resolves to Qt::DirectConnection, making all async round-trips synchronous.
// Each handle*Request slot calls the same internal::* free function that the
// real QueryWorker uses, but targets QSqlDatabase::defaultConnection instead
// of "gui_query_connection". onThreadStarted/onThreadFinished are no-ops.
// ---------------------------------------------------------------------------

class TestQueryWorker : public QueryWorker {
public:
    explicit TestQueryWorker(QObject *parent = nullptr) : QueryWorker(parent) {}

public slots:
    void onThreadStarted() override {}
    void onThreadFinished() override {}

    void handleSearchRequest(quint64 requestId, bookhub::gui::SearchParams params,
                             int offset, int limit) override
    {
        emit searchCompleted(requestId,
            internal::runSearch(params, offset, limit, QSqlDatabase::defaultConnection));
    }

    void handleCountRequest(quint64 requestId, bookhub::gui::SearchParams params) override
    {
        emit countCompleted(requestId,
            internal::runCount(params, QSqlDatabase::defaultConnection));
    }

    void handleLanguagesRequest(quint64 requestId) override
    {
        emit languagesCompleted(requestId,
            internal::fetchLanguages(QSqlDatabase::defaultConnection));
    }

    void handleSourcesRequest(quint64 requestId) override
    {
        emit sourcesCompleted(requestId,
            internal::fetchSources(QSqlDatabase::defaultConnection));
    }

    void handleGenresRequest(quint64 requestId) override
    {
        emit genresCompleted(requestId,
            internal::fetchGenres(QSqlDatabase::defaultConnection));
    }

    void handleFetchItemsRequest(quint64 requestId, QString sortColumn) override
    {
        emit fetchItemsCompleted(requestId,
            internal::fetchItems(sortColumn, QSqlDatabase::defaultConnection));
    }

    void handleAddBookRequest(quint64 requestId, QString bookId, int editionId) override
    {
        const int newId = internal::addBook(bookId, editionId, QSqlDatabase::defaultConnection);
        emit addBookCompleted(requestId, bookId, newId > 0, newId);
    }

    void handleRemoveBookRequest(quint64 requestId, int libraryItemId, QString bookId) override
    {
        emit removeBookCompleted(requestId, bookId,
            internal::removeBook(libraryItemId, QSqlDatabase::defaultConnection));
    }

    void handleRemoveBookByBookIdRequest(quint64 requestId, QString bookId) override
    {
        emit removeBookCompleted(requestId, bookId,
            internal::removeBookByBookId(bookId, QSqlDatabase::defaultConnection));
    }

    void handleUpdateStatusRequest(quint64 requestId, int libraryItemId,
                                   QString status) override
    {
        emit updateStatusCompleted(requestId,
            internal::updateStatus(libraryItemId, status, QSqlDatabase::defaultConnection));
    }

    void handleTrendingRequest(quint64 requestId) override
    {
        emit trendingCompleted(requestId,
            internal::fetchTrending(QSqlDatabase::defaultConnection));
    }

    void handleNewArrivalsRequest(quint64 requestId) override
    {
        emit newArrivalsCompleted(requestId,
            internal::fetchNewArrivals(QSqlDatabase::defaultConnection));
    }

    void handleCategoriesRequest(quint64 requestId) override
    {
        emit categoriesCompleted(requestId,
            internal::fetchCategories(QSqlDatabase::defaultConnection));
    }

    void handleBooksForGenreRequest(quint64 requestId, QString genre,
                                    int offset, int limit) override
    {
        emit booksForGenreCompleted(requestId,
            internal::fetchBooksForGenre(genre, offset, limit,
                                         QSqlDatabase::defaultConnection));
    }

    void handleBookDetailsRequest(quint64 requestId, QString bookId) override
    {
        emit bookDetailsCompleted(requestId,
            internal::fetchBookDetails(bookId, QSqlDatabase::defaultConnection));
    }

    void handleFormatsForEditionRequest(quint64 requestId, int editionId) override
    {
        emit formatsForEditionCompleted(requestId,
            internal::fetchFormatsForEdition(editionId,
                                             QSqlDatabase::defaultConnection));
    }
};

} // namespace bookhub::gui
