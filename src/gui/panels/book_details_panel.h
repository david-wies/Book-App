#pragma once

#include "../services/book_details_service.h"

#include <QWidget>
#include <QList>

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class BookDetailsPanelTest; // test friend — defined in tests/gui/

namespace bookhub::gui {

class LibraryService;
class QueryWorker;

// ---------------------------------------------------------------------------
// BookDetailsPanel — right pane of the content-area QSplitter (spec §5).
//
// Lifecycle:
//   • Created once in MainWindow; sits as the right child of m_contentSplitter.
//   • Hidden initially (splitter gives all space to the screen stack).
//   • Call loadBook(bookId) to start loading; the panel switches to a loading
//     state, then to full content once queries complete.
//   • Connect dismissed() to MainWindow::onBookDetailsDismissed() so clicking
//     "← Back" collapses the panel.
//
// Library mutations (add/remove) go through the shared LibraryService so that
// LibraryScreen automatically refreshes via libraryChanged().
// ---------------------------------------------------------------------------

class BookDetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit BookDetailsPanel(LibraryService *libraryService,
                              QueryWorker    *worker,
                              QWidget        *parent = nullptr);

    void loadBook(const QString &bookId);

signals:
    void dismissed();
    void downloadRequested(const QString &bookId);
    void audiobookRequested(const QString &bookId);

private slots:
    void onDetailsCompleted(quint64 requestId, bookhub::gui::BookDetails details);
    void onFormatsCompleted(quint64 requestId,
                            QList<bookhub::gui::BookFormatEntry> formats);
    void onLanguageChanged(int index);
    void onAddToLibraryClicked();
    void onRemoveFromLibraryClicked();
    void onAddBookCompleted(quint64 requestId, QString bookId, bool success, int newId);
    void onRemoveBookCompleted(quint64 requestId, QString bookId, bool success);
    void onToggleSummary();

private:
    void buildUi();
    void populateDetails(const BookDetails &details);
    void populateFormats(const QList<BookFormatEntry> &formats);
    void setLibraryButtonState(bool inLibrary);
    void clearFormatsSection();

    friend class ::BookDetailsPanelTest;

    // Services (not owned — both outlive this panel)
    BookDetailsService *m_detailsService{};
    LibraryService     *m_libraryService{};

    // Panel state
    QString    m_currentBookId;
    bool       m_inLibrary{false};
    int        m_libraryItemId{0};
    QString    m_fullSummary;
    bool       m_summaryExpanded{false};
    QList<int> m_editionIds;   // parallel to m_langCombo items

    // Pending request IDs for stale-response suppression
    quint64 m_pendingDetailsId{0};
    quint64 m_pendingFormatsId{0};
    quint64 m_pendingAddId{0};
    quint64 m_pendingRemoveId{0};

    // Outer stack: 0 = loading, 1 = content
    QStackedWidget *m_outerStack{};

    // Metadata widgets (inside scroll area)
    QLabel      *m_titleLabel{};
    QLabel      *m_authorLabel{};
    QLabel      *m_metaLabel{};
    QLabel      *m_langLabel{};
    QComboBox   *m_langCombo{};
    QLabel      *m_singleLangLabel{};
    QLabel      *m_summaryLabel{};
    QPushButton *m_showMoreBtn{};
    QWidget     *m_formatsWidget{};
    QVBoxLayout *m_formatsLayout{};

    // Action bar (pinned below scroll area)
    QPushButton *m_libraryBtn{};
    QPushButton *m_downloadBtn{};
    QPushButton *m_audiobookBtn{};

    static constexpr int kSummaryCollapseLen = 320;
};

} // namespace bookhub::gui
