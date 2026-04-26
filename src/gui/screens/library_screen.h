#pragma once

#include <QWidget>
#include <QList>

// Forward declarations to keep compile times low
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
struct LibraryItem;

// ---------------------------------------------------------------------------
// LibraryScreen — the Library tab's content widget (spec Section 2).
//
// Owns a LibraryService and a QListView driven by BookCardDelegate.
// Toolbar: book count label + sort combo + list/grid toggle.
// Emits bookDetailsRequested when the user clicks "Details" on a card so
// MainWindow can show the BookDetailsPanel (not yet implemented in Task 6).
// ---------------------------------------------------------------------------

class LibraryScreen : public QWidget {
    Q_OBJECT
public:
    explicit LibraryScreen(QWidget *parent = nullptr);

    // Refresh the list from the database. Call after any write operation.
    void reload();

signals:
    // Sent when the user clicks the "Details" button on a book card.
    void bookDetailsRequested(const QString &bookId);

    // Sent when the user clicks "Explore Books" from the empty state CTA.
    void exploreRequested();

private slots:
    void onSortChanged(int index);
    void onViewToggled(int id, bool checked);
    void onDetailsRequested(int libraryItemId, const QString &bookId);
    void onDownloadRequested(int libraryItemId, const QString &bookId);
    void onRemoveRequested(int libraryItemId, const QString &bookId);

private:
    friend class ::LibraryScreenTest;

    void buildToolbar(QWidget *toolbar);
    void populateModel(const QList<LibraryItem> &items);
    void updateCountLabel(int count);

    LibraryService       *m_service{};
    QListView            *m_listView{};
    QStandardItemModel   *m_model{};
    BookCardDelegate     *m_delegate{};
    EmptyStateWidget     *m_emptyState{};
    QStackedWidget       *m_stack{};   // index 0 = list, index 1 = empty state
    QLabel               *m_countLabel{};
    QComboBox            *m_sortCombo{};
    QButtonGroup         *m_viewGroup{};
};

} // namespace bookhub::gui
