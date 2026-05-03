#pragma once

#include <QWidget>
#include <QList>
#include <QHash>

#include "../services/search_service.h"

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
class SearchScreenTest; // test friend — defined in tests/gui/

namespace bookhub::gui {

class LibraryService;
class SearchResultDelegate;
class QueryWorker;

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
    // Default constructor — creates self-owned services, requires a worker.
    explicit SearchScreen(QueryWorker *worker, QWidget *parent = nullptr);

    // Injection constructor — borrows a shared LibraryService.
    explicit SearchScreen(LibraryService *service, QueryWorker *worker,
                          QWidget *parent = nullptr);

signals:
    void bookDetailsRequested(const QString &bookId);

private slots:
    void onSearchBarChanged();
    void onAuthorFilterChanged();
    void onFiltersChanged();
    void runSearch();
    void onLoadMore();
    void onAddToLibrary(const QString &bookId);
    void onRemoveFromLibrary(const QString &bookId);
    void onClearFilters();
    void onSortChanged(int index);
    void onShowMoreGenres();

    // Async result slots
    void onCountCompleted(quint64 requestId, int count);
    void onSearchCompleted(quint64 requestId, const QList<bookhub::gui::SearchResult> &results);
    void onLoadMoreCompleted(quint64 requestId, const QList<bookhub::gui::SearchResult> &results);
    void onAddBookCompleted(quint64 requestId, const QString &bookId, bool success, int newId);
    void onRemoveBookCompleted(quint64 requestId, const QString &bookId, bool success);
    void onLanguagesCompleted(quint64 requestId, const QStringList &languages);
    void onSourcesCompleted(quint64 requestId, const QStringList &sources);
    void onGenresCompleted(quint64 requestId, const QStringList &genres);

private:
    void init(QueryWorker *worker);
    void buildSearchBar(QWidget *container);
    void buildFilterPanel(QWidget *panel);
    void buildResultsPane(QWidget *pane);

    SearchParams collectParams() const;
    bool isQueryActive() const;
    void populateModel(const QList<SearchResult> &results, bool append);
    void updateCountLabel(int count);
    void setResultsState(int state); // 0=initial, 1=populated, 2=empty

    friend class ::SearchScreenTest;

    SearchService         *m_service{};
    LibraryService        *m_libraryService{};  // owned when default ctor used; borrowed (non-owning) otherwise
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

    // Pending request IDs for stale-response suppression
    quint64 m_pendingCountId{0};
    quint64 m_pendingSearchId{0};
    quint64 m_pendingLoadMoreId{0};
    // Cached params for the count→search hand-off
    SearchParams m_pendingSearchParams;

    static constexpr int kPageSize       = 50;
    static constexpr int kGenreCollapsed = 6;
};

} // namespace bookhub::gui
