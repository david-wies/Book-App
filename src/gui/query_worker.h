#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "services/book_details_service.h"
#include "services/explore_service.h"
#include "services/library_service.h"
#include "services/search_service.h"
#include "services/tts_types.h"

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
    virtual void handleSearchRequest(quint64 requestId, const bookhub::gui::SearchParams &params,
                                     int offset, int limit);
    virtual void handleCountRequest(quint64 requestId, const bookhub::gui::SearchParams &params);
    virtual void handleLanguagesRequest(quint64 requestId);
    virtual void handleSourcesRequest(quint64 requestId);
    virtual void handleGenresRequest(quint64 requestId);

    // Library
    virtual void handleFetchItemsRequest(quint64 requestId, const QString &sortColumn);
    virtual void handleAddBookRequest(quint64 requestId, const QString &bookId, int editionId);
    virtual void handleRemoveBookRequest(quint64 requestId, int libraryItemId, QString bookId);
    virtual void handleRemoveBookByBookIdRequest(quint64 requestId, const QString &bookId);
    virtual void handleUpdateStatusRequest(quint64 requestId, int libraryItemId,
                                           const QString &status);

    // Explore
    virtual void handleTrendingRequest(quint64 requestId);
    virtual void handleNewArrivalsRequest(quint64 requestId);
    virtual void handleCategoriesRequest(quint64 requestId);
    virtual void handleBooksForGenreRequest(quint64 requestId, const QString &genre,
                                            int offset, int limit);

    // Book details
    virtual void handleBookDetailsRequest(quint64 requestId, const QString &bookId);
    virtual void handleFormatsForEditionRequest(quint64 requestId, int editionId);

    // Voice CRUD
    virtual void handleListVoicesRequest(quint64 requestId);
    virtual void handleInsertVoiceRequest(quint64 requestId, const QString &name,
                                          const QString &type, const QString &engine);
    virtual void handleUpdateVoiceRequest(quint64 requestId, int voiceId, const QString &engine);
    virtual void handleDeleteVoiceRequest(quint64 requestId, int voiceId);

    // Audiobook operations
    virtual void handleQueryAudiobookStatusRequest(quint64 requestId, const QString &bookId);
    virtual void handleSetAudiobookReadyRequest(quint64 requestId, const QString &bookId);

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
    void removeBookCompleted(quint64 requestId, QString bookId, bool success);
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

    // Voice results
    void listVoicesCompleted(quint64 requestId, QList<bookhub::gui::VoiceEntry> voices);
    void insertVoiceCompleted(quint64 requestId, bool success, int newId);
    void updateVoiceCompleted(quint64 requestId, bool success);
    void deleteVoiceCompleted(quint64 requestId, bool success);

    // Audiobook status results
    void audiobookStatusQueried(quint64 requestId, bool isReady);
    void audiobookReadySet(quint64 requestId, bool success);

private:
    static constexpr const char *kConnectionName = "gui_query_connection";
    static QLatin1String conn() noexcept { return QLatin1String{kConnectionName}; }
};

// ---------------------------------------------------------------------------
// Internal free functions for voice and audiobook-status queries.
// Implemented in query_worker.cpp; called by TestQueryWorker in tests.
// ---------------------------------------------------------------------------
namespace internal {
    QList<VoiceEntry> listVoices(const QString &connectionName);
    // type must be 'preset' or 'custom'; engine must be 'sherpa_onnx' or 'pocket_tts'.
    bool insertVoice(const QString &name, const QString &type, const QString &engine,
                     int *outNewId, const QString &connectionName);
    bool updateVoice(int voiceId, const QString &engine, const QString &connectionName);
    bool deleteVoice(int voiceId, const QString &connectionName);
    bool queryAudiobookStatus(const QString &bookId, bool &outIsReady,
                              const QString &connectionName);
    bool setAudiobookReady(const QString &bookId, const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
