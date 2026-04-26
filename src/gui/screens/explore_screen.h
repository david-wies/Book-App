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

namespace bookhub::gui {

class ExploreService;
struct ExploreBook;
struct ExploreCategory;

// ---------------------------------------------------------------------------
// ExploreScreen — the Explore tab's content widget (spec Section 4).
//
// Internal stack:
//   index 0 — TopLevelPage: vertical scroll area with Trending carousel,
//              Browse Categories grid, and New Arrivals carousel.
//   index 1 — GenreBooksPage: back button + breadcrumb + paginated book grid
//              for a single genre with a "Load more" button.
//
// Navigation:
//   CategoryCard click  → showGenreBooks(genreName)   (index 0 → 1)
//   "← Back" click      → m_exploreStack → index 0
//   "Load more" click   → appends next page of genre books
//
// BookMiniCard::bookClicked is emitted but not connected to anything until
// Task 9 (BookDetailsPanel) is implemented.
// ---------------------------------------------------------------------------

class ExploreScreen : public QWidget {
    Q_OBJECT
public:
    explicit ExploreScreen(QWidget *parent = nullptr);

signals:
    void bookDetailsRequested(const QString &bookId);

private slots:
    void onCategoryClicked(const QString &genreName);
    void onLoadMore();

private:
    // Page builders
    QWidget *buildTopLevelPage();
    QWidget *buildGenreBooksPage();

    // Sub-section builders used by buildTopLevelPage
    QWidget *buildCarousel(const QList<ExploreBook> &books);
    QWidget *buildCategoryGrid(const QList<ExploreCategory> &categories);

    // Genre drill-down helpers
    void showGenreBooks(const QString &genreName);
    void appendGenreBooks(int offset);

    friend class ::ExploreScreenTest;

    ExploreService *m_service{};

    // Internal navigation stack
    QStackedWidget *m_exploreStack{};

    // Genre drill-down page widgets
    QLabel      *m_breadcrumb{};
    QWidget     *m_genreGridContainer{};
    QGridLayout *m_genreGridLayout{};
    QPushButton *m_loadMoreBtn{};

    // State for paginated genre browsing
    QString m_currentGenre;
    int     m_genreOffset{0};

    static constexpr int kGenrePageSize = 40;
    static constexpr int kGridColumns   = 4;
};

} // namespace bookhub::gui
