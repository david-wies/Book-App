#pragma once

#include <QObject>
#include <QString>
#include <QList>

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

// ---------------------------------------------------------------------------
// ExploreService — synchronous SQL queries on the GUI thread's default
// database connection. All methods called from the GUI thread only.
//
// The Explore screen needs three data sets:
//   - Trending:     20 most-recently ingested books (ORDER BY rowid DESC)
//   - New Arrivals: next 20 after the trending window (OFFSET 20)
//   - Categories:   genres with per-genre book counts, ranked by count
//   - Genre books:  books for a specific genre, paginated 40 per page
//
// rowid ordering is used as a cheap discovery-order proxy for MVP; a proper
// "last_seen_at" timestamp column should be added before 1.0 (TODO).
// ---------------------------------------------------------------------------

class ExploreService : public QObject {
    Q_OBJECT
public:
    explicit ExploreService(QObject *parent = nullptr);

    // NOTE: all methods are called only from the GUI thread.
    QList<ExploreBook>    fetchTrending()    const;
    QList<ExploreBook>    fetchNewArrivals() const;
    QList<ExploreCategory> fetchCategories() const;
    QList<ExploreBook>    fetchBooksForGenre(const QString &genre,
                                             int offset = 0,
                                             int limit  = 40) const;
};

} // namespace bookhub::gui
