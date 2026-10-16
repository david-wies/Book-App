#include "query_worker.h"

#include "shared/database.h"
#include "services/book_details_service.h"
#include "services/explore_service.h"
#include "services/library_service.h"
#include "services/search_service.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

#include <utility>

namespace bookhub::gui {

QueryWorker::QueryWorker(QObject *parent)
    : QObject(parent)
{}

void QueryWorker::onThreadStarted()
{
    // Compute the path directly — reading it from the default connection would
    // cross thread ownership and cause Qt to return an invalid database object,
    // making databaseName() return "" and silently opening an in-memory SQLite.
    if (!bookhub::db::initializeDatabase(bookhub::db::databaseFilePath(), conn())) {
        qWarning() << "QueryWorker: failed to open gui_query_connection";
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

void QueryWorker::handleSearchRequest(quint64 requestId, const SearchParams &params,
                                       int offset, int limit)
{
    emit searchCompleted(requestId,
        internal::runSearch(params, offset, limit, conn()));
}

void QueryWorker::handleCountRequest(quint64 requestId, const SearchParams &params)
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

void QueryWorker::handleFetchItemsRequest(quint64 requestId, const QString &sortColumn)
{
    emit fetchItemsCompleted(requestId,
        internal::fetchItems(sortColumn, conn()));
}

void QueryWorker::handleAddBookRequest(quint64 requestId, const QString &bookId, int editionId)
{
    const int newId = internal::addBook(bookId, editionId, conn());
    emit addBookCompleted(requestId, bookId, newId > 0, newId);
}

void QueryWorker::handleRemoveBookRequest(quint64 requestId, int libraryItemId, QString bookId)
{
    const bool ok = internal::removeBook(libraryItemId, conn());
    emit removeBookCompleted(requestId, std::move(bookId), ok);
}

void QueryWorker::handleRemoveBookByBookIdRequest(quint64 requestId, const QString &bookId)
{
    const bool ok = internal::removeBookByBookId(bookId, conn());
    emit removeBookCompleted(requestId, bookId, ok);
}

void QueryWorker::handleUpdateStatusRequest(quint64 requestId, int libraryItemId,
                                             const QString &status)
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

void QueryWorker::handleBooksForGenreRequest(quint64 requestId, const QString &genre,
                                              int offset, int limit)
{
    emit booksForGenreCompleted(requestId,
        internal::fetchBooksForGenre(genre, offset, limit, conn()));
}

// ---------------------------------------------------------------------------
// Book details handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleBookDetailsRequest(quint64 requestId, const QString &bookId)
{
    emit bookDetailsCompleted(requestId,
        internal::fetchBookDetails(bookId, conn()));
}

void QueryWorker::handleFormatsForEditionRequest(quint64 requestId, int editionId)
{
    emit formatsForEditionCompleted(requestId,
        internal::fetchFormatsForEdition(editionId, conn()));
}

// ---------------------------------------------------------------------------
// Voice CRUD handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleListVoicesRequest(quint64 requestId)
{
    emit listVoicesCompleted(requestId, internal::listVoices(conn()));
}

void QueryWorker::handleInsertVoiceRequest(quint64 requestId, const QString &name,
                                           const QString &type, const QString &engine)
{
    int newId = -1;
    const bool ok = internal::insertVoice(name, type, engine, &newId, conn());
    emit insertVoiceCompleted(requestId, ok, newId);
}

void QueryWorker::handleUpdateVoiceRequest(quint64 requestId, int voiceId,
                                           const QString &engine)
{
    emit updateVoiceCompleted(requestId, internal::updateVoice(voiceId, engine, conn()));
}

void QueryWorker::handleDeleteVoiceRequest(quint64 requestId, int voiceId)
{
    emit deleteVoiceCompleted(requestId, internal::deleteVoice(voiceId, conn()));
}

// ---------------------------------------------------------------------------
// Audiobook status handlers
// ---------------------------------------------------------------------------

void QueryWorker::handleQueryAudiobookStatusRequest(quint64 requestId, const QString &bookId)
{
    bool isReady = false;
    if (!internal::queryAudiobookStatus(bookId, isReady, conn()))
        qWarning() << "handleQueryAudiobookStatusRequest failed for book_id:" << bookId;
    emit audiobookStatusQueried(requestId, isReady);
}

void QueryWorker::handleSetAudiobookReadyRequest(quint64 requestId, const QString &bookId)
{
    const bool success = internal::setAudiobookReady(bookId, conn());
    if (!success)
        qWarning() << "handleSetAudiobookReadyRequest: no matching library_items row for book_id:" << bookId;
    emit audiobookReadySet(requestId, success);
}

// ---------------------------------------------------------------------------
// Internal free functions — voice and audiobook-status SQL
// ---------------------------------------------------------------------------

namespace internal {

QList<VoiceEntry> listVoices(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    QList<VoiceEntry> voices;
    if (!query.exec(QStringLiteral(
            "SELECT id, name, (type = 'preset') FROM voices"
            " ORDER BY (type = 'preset') DESC, name"))) {
        qWarning() << "internal::listVoices failed:" << query.lastError().text();
        return voices;
    }
    while (query.next()) {
        VoiceEntry entry;
        entry.id       = query.value(0).toInt();
        entry.name     = query.value(1).toString();
        entry.isPreset = query.value(2).toInt() != 0;
        voices.append(entry);
    }
    return voices;
}

bool insertVoice(const QString &name, const QString &type, const QString &engine,
                 int *outNewId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral(
        "INSERT INTO voices (name, type, engine) VALUES (?, ?, ?)"));
    query.addBindValue(name);
    query.addBindValue(type);
    query.addBindValue(engine);
    if (!query.exec()) {
        qWarning() << "internal::insertVoice failed:" << query.lastError().text();
        return false;
    }
    if (outNewId)
        *outNewId = query.lastInsertId().toInt();
    return true;
}

bool updateVoice(int voiceId, const QString &engine, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral("UPDATE voices SET engine = ? WHERE id = ?"));
    query.addBindValue(engine);
    query.addBindValue(voiceId);
    if (!query.exec()) {
        qWarning() << "internal::updateVoice failed:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool deleteVoice(int voiceId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral("DELETE FROM voices WHERE id = ?"));
    query.addBindValue(voiceId);
    if (!query.exec()) {
        qWarning() << "internal::deleteVoice failed:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool queryAudiobookStatus(const QString &bookId, bool &outIsReady, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral(
        "SELECT status FROM library_items WHERE book_id = ? LIMIT 1"));
    query.addBindValue(bookId);
    if (!query.exec())
        return false;
    outIsReady = query.next()
        && (query.value(0).toString() == QLatin1String("audiobook_ready"));
    return true;
}

bool setAudiobookReady(const QString &bookId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral(
        "UPDATE library_items SET status = 'audiobook_ready' WHERE book_id = ?"));
    query.addBindValue(bookId);
    return query.exec() && query.numRowsAffected() > 0;
}

} // namespace internal

} // namespace bookhub::gui
