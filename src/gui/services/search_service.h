#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <atomic>

namespace bookhub::gui {

struct SearchParams {
    QString     keyword;
    QString     author;
    QStringList genres;
    int         yearFrom{0};
    int         yearTo{0};
    QStringList languages;
    QStringList sources;
    bool        ebookOnly{false};
    bool        audiobookOnly{false};
    QString     sortColumn{QStringLiteral("title")}; // "title" or "author"
};

struct SearchResult {
    QString     bookId;
    QString     title;
    QString     author;
    int         publishYear{0};
    QStringList languages;
    QStringList sources;
    QStringList formats;
    bool        inLibrary{false};
};

class QueryWorker;

// ---------------------------------------------------------------------------
// SearchService — async facade that routes queries through QueryWorker.
//
// Call connectToWorker() once after construction. Then use the request*()
// methods — each returns a requestId the caller can use to discard stale
// responses. Result signals are delivered on the GUI thread.
// ---------------------------------------------------------------------------

class SearchService : public QObject {
    Q_OBJECT
public:
    explicit SearchService(QObject *parent = nullptr);

    void connectToWorker(QueryWorker *worker);
    bool isBusy() const;

    // Returns the ID that will be assigned to the next request without consuming it.
    // Call before requestSearch() to set m_pendingSearchId / m_pendingLoadMoreId
    // prior to the signal emission so synchronous (direct) connections work correctly.
    quint64 peekNextId() const;

    quint64 requestSearch(const SearchParams &params, int offset = 0, int limit = 50);
    quint64 requestCount(const SearchParams &params);
    quint64 requestLanguages();
    quint64 requestSources();
    quint64 requestGenres();

signals:
    // Result signals — delivered on the GUI thread
    void searchCompleted(quint64 requestId, QList<bookhub::gui::SearchResult> results);
    void countCompleted(quint64 requestId, int count);
    void languagesCompleted(quint64 requestId, QStringList languages);
    void sourcesCompleted(quint64 requestId, QStringList sources);
    void genresCompleted(quint64 requestId, QStringList genres);

    // Internal request signals — routed to QueryWorker
    void searchRequested(quint64 requestId, bookhub::gui::SearchParams params,
                         int offset, int limit);
    void countRequested(quint64 requestId, bookhub::gui::SearchParams params);
    void languagesRequested(quint64 requestId);
    void sourcesRequested(quint64 requestId);
    void genresRequested(quint64 requestId);

private:
    std::atomic<quint64> m_nextRequestId{1};
    quint64              m_pendingCount{0};
};

// ---------------------------------------------------------------------------
// Internal free functions — called by QueryWorker slots and integration tests.
// Each takes an explicit connectionName so they work on any SQL connection.
// ---------------------------------------------------------------------------
namespace internal {
    QList<SearchResult> runSearch(const SearchParams &params, int offset, int limit,
                                  const QString &connectionName);
    int runCount(const SearchParams &params, const QString &connectionName);
    QStringList fetchLanguages(const QString &connectionName);
    QStringList fetchSources(const QString &connectionName);
    QStringList fetchGenres(const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
