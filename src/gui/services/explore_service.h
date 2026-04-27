#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <atomic>

namespace bookhub::gui {

struct ExploreBook {
    QString bookId;
    QString title;
    QString author;
};

struct ExploreCategory {
    QString genreName;
    int     bookCount{0};
};

class QueryWorker;

// ---------------------------------------------------------------------------
// ExploreService — async facade for Explore screen data queries.
//
// Call connectToWorker() once after construction. Use request*() methods;
// each returns a requestId for stale-response suppression.
// ---------------------------------------------------------------------------

class ExploreService : public QObject {
    Q_OBJECT
public:
    explicit ExploreService(QObject *parent = nullptr);

    void connectToWorker(QueryWorker *worker);
    bool isBusy() const;

    quint64 requestTrending();
    quint64 requestNewArrivals();
    quint64 requestCategories();
    quint64 requestBooksForGenre(const QString &genre, int offset = 0, int limit = 40);

signals:
    // Result signals — delivered on the GUI thread
    void trendingCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void newArrivalsCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void categoriesCompleted(quint64 requestId, QList<bookhub::gui::ExploreCategory> categories);
    void booksForGenreCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);

    // Internal request signals — routed to QueryWorker
    void trendingRequested(quint64 requestId);
    void newArrivalsRequested(quint64 requestId);
    void categoriesRequested(quint64 requestId);
    void booksForGenreRequested(quint64 requestId, QString genre, int offset, int limit);

private:
    std::atomic<quint64> m_nextRequestId{1};
    int                  m_pendingCount{0};
};

// ---------------------------------------------------------------------------
// Internal free functions — called by QueryWorker and integration tests.
// ---------------------------------------------------------------------------
namespace internal {
    QList<ExploreBook>     fetchTrending(const QString &connectionName);
    QList<ExploreBook>     fetchNewArrivals(const QString &connectionName);
    QList<ExploreCategory> fetchCategories(const QString &connectionName);
    QList<ExploreBook>     fetchBooksForGenre(const QString &genre, int offset, int limit,
                                               const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
