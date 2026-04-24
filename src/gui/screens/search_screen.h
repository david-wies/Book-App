#pragma once

#include <QWidget>
#include <QList>

class QLineEdit;
class QListWidget;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QListView;
class QStackedWidget;
class QStandardItemModel;
class QTimer;
class QSplitter;

namespace bookhub::gui {

class SearchService;
class LibraryService;
class SearchResultDelegate;
struct SearchParams;
struct SearchResult;

// ---------------------------------------------------------------------------
// SearchScreen — the Search tab's content widget (spec Section 3).
//
// Filter panel (220 px) + results pane in a QSplitter. Debounced search from
// the main bar (500 ms) and from the author sub-filter (300 ms). Paginated
// 50 rows at a time with a "Load more" button.
// ---------------------------------------------------------------------------

class SearchScreen : public QWidget {
    Q_OBJECT
public:
    explicit SearchScreen(QWidget *parent = nullptr);

signals:
    void bookDetailsRequested(const QString &bookId);

private slots:
    void onSearchBarChanged();
    void onAuthorFilterChanged();
    void onFiltersChanged();
    void runSearch();
    void onLoadMore();
    void onAddToLibrary(const QString &bookId);
    void onClearFilters();
    void onSortChanged(int index);
    void onShowMoreGenres();

private:
    void buildSearchBar(QWidget *container);
    void buildFilterPanel(QWidget *panel);
    void buildResultsPane(QWidget *pane);

    SearchParams collectParams() const;
    bool isQueryActive() const;
    void populateModel(const QList<SearchResult> &results, bool append);
    void updateCountLabel(int count);
    void setResultsState(int state); // 0=initial, 1=populated, 2=empty

    SearchService         *m_service{};
    LibraryService        *m_libraryService{};
    SearchResultDelegate  *m_delegate{};
    QStandardItemModel    *m_model{};

    // Search bar
    QLineEdit  *m_searchBar{};
    QTimer     *m_searchTimer{};

    // Filter panel controls
    QLineEdit  *m_authorFilter{};
    QTimer     *m_authorTimer{};
    QListWidget *m_genreList{};
    QSpinBox   *m_yearFrom{};
    QSpinBox   *m_yearTo{};
    QListWidget *m_langList{};
    QListWidget *m_srcList{};
    QCheckBox  *m_ebookCheck{};
    QCheckBox  *m_audiobookCheck{};
    QPushButton *m_showMoreGenres{};
    bool        m_genresExpanded{false};

    // Results pane
    QListView   *m_resultsView{};
    QLabel      *m_countLabel{};
    QPushButton *m_loadMoreBtn{};
    QStackedWidget *m_resultsStack{}; // 0=initial, 1=results, 2=empty

    int         m_currentOffset{0};
    int         m_totalCount{0};
    QString     m_currentSortColumn{QStringLiteral("title")};

    static constexpr int kPageSize       = 50;
    static constexpr int kGenreCollapsed = 6;
};

} // namespace bookhub::gui
