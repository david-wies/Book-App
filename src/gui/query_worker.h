#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "services/book_details_service.h"
#include "services/explore_service.h"
#include "services/library_service.h"
#include "services/search_service.h"

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// QueryWorker — executes all GUI database queries on the query thread.
//
// Lifecycle: create on the GUI thread, move to a QThread via moveToThread(),
// then start the thread. onThreadStarted() opens "gui_query_connection" and
// onThreadFinished() closes it. All handle*Request slots run on the query
// thread; their emit*Completed signals are delivered back to the GUI thread
// via Qt::AutoConnection (queued cross-thread, direct same-thread in tests).
// ---------------------------------------------------------------------------

class QueryWorker : public QObject {
    Q_OBJECT
public:
    explicit QueryWorker(QObject *parent = nullptr);

public slots:
    // Thread lifecycle
    virtual void onThreadStarted();
    virtual void onThreadFinished();

    // Search
    virtual void handleSearchRequest(quint64 requestId, bookhub::gui::SearchParams params,
                                     int offset, int limit);
    virtual void handleCountRequest(quint64 requestId, bookhub::gui::SearchParams params);
    virtual void handleLanguagesRequest(quint64 requestId);
    virtual void handleSourcesRequest(quint64 requestId);
    virtual void handleGenresRequest(quint64 requestId);

    // Library
    virtual void handleFetchItemsRequest(quint64 requestId, QString sortColumn);
    virtual void handleAddBookRequest(quint64 requestId, QString bookId, int editionId);
    virtual void handleRemoveBookRequest(quint64 requestId, int libraryItemId);
    virtual void handleUpdateStatusRequest(quint64 requestId, int libraryItemId, QString status);

    // Explore
    virtual void handleTrendingRequest(quint64 requestId);
    virtual void handleNewArrivalsRequest(quint64 requestId);
    virtual void handleCategoriesRequest(quint64 requestId);
    virtual void handleBooksForGenreRequest(quint64 requestId, QString genre, int offset, int limit);

    // Book details
    virtual void handleBookDetailsRequest(quint64 requestId, QString bookId);
    virtual void handleFormatsForEditionRequest(quint64 requestId, int editionId);

signals:
    // Search results
    void searchCompleted(quint64 requestId, QList<bookhub::gui::SearchResult> results);
    void countCompleted(quint64 requestId, int count);
    void languagesCompleted(quint64 requestId, QStringList languages);
    void sourcesCompleted(quint64 requestId, QStringList sources);
    void genresCompleted(quint64 requestId, QStringList genres);

    // Library results
    void fetchItemsCompleted(quint64 requestId, QList<bookhub::gui::LibraryItem> items);
    void addBookCompleted(quint64 requestId, QString bookId, bool success, int newId);
    void removeBookCompleted(quint64 requestId, bool success);
    void updateStatusCompleted(quint64 requestId, bool success);

    // Explore results
    void trendingCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void newArrivalsCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void categoriesCompleted(quint64 requestId, QList<bookhub::gui::ExploreCategory> categories);
    void booksForGenreCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);

    // Book details results
    void bookDetailsCompleted(quint64 requestId, bookhub::gui::BookDetails details);
    void formatsForEditionCompleted(quint64 requestId,
                                    QList<bookhub::gui::BookFormatEntry> formats);

private:
    static constexpr const char *kConnectionName = "gui_query_connection";
    static QLatin1String conn() noexcept { return QLatin1String{kConnectionName}; }
};

} // namespace bookhub::gui
