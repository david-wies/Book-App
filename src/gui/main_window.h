#pragma once

#include <QMainWindow>

class QSplitter;
class QStackedWidget;
class QButtonGroup;
class QLabel;
class MainWindowTest; // test friend — defined in tests/gui/
class QThread;

namespace bookhub::gui {

class BookDetailsPanel;
class ExploreScreen;
class LibraryScreen;
class LibraryService;
class QueryWorker;
class SearchScreen;

// ---------------------------------------------------------------------------
// MainWindow — the top-level QMainWindow for BookHub.
//
// Layout (spec Section 1):
//   - Dark nav bar (48 px): three exclusive QPushButton tabs
//   - QStackedWidget: LibraryScreen (0), SearchScreen (1), ExploreScreen (2)
//   - QStatusBar (24 px): collector status text + coloured dot
//
// Receives CollectorWorker status signals via Qt::QueuedConnection so the
// GUI thread is never blocked by cross-thread calls.
// ---------------------------------------------------------------------------

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    // Connected to CollectorWorker signals (queued connection from main.cpp).
    void onCollectorStatusChanged(const QString &message);
    void onCollectorErrorOccurred(const QString &errorMessage);

private slots:
    void onNavTabChanged(int id, bool checked);
    void onLibraryExploreRequested();
    void onBookDetailsRequested(const QString &bookId);
    void onBookDetailsDismissed();

private:
    friend class ::MainWindowTest;

    void buildNavBar();
    void buildStatusBar();
    void setStatusDot(const QColor &color);

    QThread        *m_queryThread{};
    QueryWorker    *m_queryWorker{};
    LibraryService *m_libraryService{};

    QWidget           *m_navBar{};
    QSplitter         *m_contentSplitter{};
    QStackedWidget    *m_stack{};
    QButtonGroup      *m_navGroup{};
    LibraryScreen     *m_libraryScreen{};
    SearchScreen      *m_searchScreen{};
    ExploreScreen     *m_exploreScreen{};
    BookDetailsPanel  *m_bookDetailsPanel{};

    // Status bar widgets
    QLabel *m_statusLabel{};
    QLabel *m_statusDot{};
};

} // namespace bookhub::gui
