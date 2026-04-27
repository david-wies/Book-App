#pragma once

#include <QWidget>
#include <QList>

class QStackedWidget;
class QLabel;
class QPushButton;
class QScrollArea;
class QWidget;
class QGridLayout;
class QHBoxLayout;
class ExploreScreenTest; // test friend — defined in tests/gui/
class QVBoxLayout;

namespace bookhub::gui {

class ExploreService;
class QueryWorker;
struct ExploreBook;
struct ExploreCategory;

// ---------------------------------------------------------------------------
// ExploreScreen — the Explore tab's content widget (spec Section 4).
//
// Pass a QueryWorker* to enable async loading. Without a worker the carousels
// and category grid display placeholders (used in fallback/standalone mode).
// ---------------------------------------------------------------------------

class ExploreScreen : public QWidget {
    Q_OBJECT
public:
    explicit ExploreScreen(QueryWorker *worker, QWidget *parent = nullptr);

signals:
    void bookDetailsRequested(const QString &bookId);

private slots:
    void onCategoryClicked(const QString &genreName);
    void onLoadMore();

    // Async result slots
    void onTrendingCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void onNewArrivalsCompleted(quint64 requestId, QList<bookhub::gui::ExploreBook> books);
    void onCategoriesCompleted(quint64 requestId,
                               QList<bookhub::gui::ExploreCategory> categories);
    void onBooksForGenreCompleted(quint64 requestId,
                                  QList<bookhub::gui::ExploreBook> books);

private:
    // Page builders
    QWidget *buildTopLevelPage();
    QWidget *buildGenreBooksPage();

    // Sub-section builders
    QWidget *buildCarousel(const QList<ExploreBook> &books);
    QWidget *buildCategoryGrid(const QList<ExploreCategory> &categories);

    // Genre drill-down helpers
    void loadTopLevelData();
    void showGenreBooks(const QString &genreName);
    void appendGenreBooks(int offset);

    friend class ::ExploreScreenTest;

    ExploreService *m_service{};

    // Internal navigation stack
    QStackedWidget *m_exploreStack{};

    // Top-level page layout — needed to replace placeholder carousels/grid
    QVBoxLayout *m_topLevelLayout{};
    QWidget     *m_trendingCarousel{};
    QWidget     *m_categoryGrid{};
    QWidget     *m_newArrivalsCarousel{};

    // Layout positions of the replaceable widgets
    static constexpr int kTrendingCarouselIndex   = 1; // after "TRENDING" label
    static constexpr int kCategoryGridIndex       = 3; // after "BROWSE CATEGORIES" label
    static constexpr int kNewArrivalsCarouselIndex = 5; // after "NEW ARRIVALS" label

    // Genre drill-down page widgets
    QLabel      *m_breadcrumb{};
    QWidget     *m_genreGridContainer{};
    QGridLayout *m_genreGridLayout{};
    QPushButton *m_loadMoreBtn{};

    // State for paginated genre browsing
    QString m_currentGenre;
    int     m_genreOffset{0};

    // Pending request IDs for stale-response suppression
    quint64 m_pendingTrendingId{0};
    quint64 m_pendingNewArrivalsId{0};
    quint64 m_pendingCategoriesId{0};
    quint64 m_pendingGenreId{0};

    static constexpr int kGenrePageSize = 40;
    static constexpr int kGridColumns   = 4;
};

} // namespace bookhub::gui
