#pragma once

#include <QWidget>
#include <QList>

class QListView;
class QComboBox;
class QButtonGroup;
class QStackedWidget;
class QStandardItemModel;
class QLabel;
class LibraryScreenTest; // test friend — defined in tests/gui/

namespace bookhub::gui {

class LibraryService;
class BookCardDelegate;
class EmptyStateWidget;
class QueryWorker;
struct LibraryItem;

// ---------------------------------------------------------------------------
// LibraryScreen — the Library tab's content widget (spec Section 2).
//
// Owns a LibraryService wired to a QueryWorker for async DB access.
// Toolbar: book count label + sort combo + list/grid toggle.
// Emits bookDetailsRequested when the user clicks "Details" on a card.
// ---------------------------------------------------------------------------

class LibraryScreen : public QWidget {
    Q_OBJECT
public:
    // Default constructor — creates and owns a LibraryService.
    explicit LibraryScreen(QueryWorker *worker, QWidget *parent = nullptr);

    // Injection constructor — borrows a pre-wired shared LibraryService.
    explicit LibraryScreen(LibraryService *service, QWidget *parent = nullptr);

    // Refresh the list from the database. Call after any write operation.
    void reload();

signals:
    void bookDetailsRequested(const QString &bookId);
    void exploreRequested();

private slots:
    void onSortChanged(int index);
    void onViewToggled(int id, bool checked);
    void onDetailsRequested(int libraryItemId, const QString &bookId);
    void onDownloadRequested(int libraryItemId, const QString &bookId);
    void onAudiobookRequested(int libraryItemId, const QString &bookId);
    void onRemoveRequested(int libraryItemId, const QString &bookId);
    void onFetchItemsCompleted(quint64 requestId, QList<bookhub::gui::LibraryItem> items);

private:
    void init(LibraryService *service);
    friend class ::LibraryScreenTest;

    void buildToolbar(QWidget *toolbar);
    void populateModel(const QList<LibraryItem> &items);
    void updateCountLabel(int count);

    LibraryService       *m_service{};  // owned when default ctor used; borrowed (non-owning) otherwise
    QListView            *m_listView{};
    QStandardItemModel   *m_model{};
    BookCardDelegate     *m_delegate{};
    EmptyStateWidget     *m_emptyState{};
    QStackedWidget       *m_stack{};   // 0=list, 1=empty state, 2=loading
    QLabel               *m_countLabel{};
    QComboBox            *m_sortCombo{};
    QButtonGroup         *m_viewGroup{};

    quint64 m_pendingFetchId{0};
};

} // namespace bookhub::gui
