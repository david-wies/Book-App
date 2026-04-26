#pragma once

#include <QMainWindow>

class QStackedWidget;
class QButtonGroup;
class QLabel;
class MainWindowTest; // test friend — defined in tests/gui/

namespace bookhub::gui {

class LibraryScreen;
class LibraryService;
class SearchScreen;
class ExploreScreen;

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

public slots:
    // Connected to CollectorWorker signals (queued connection from main.cpp).
    void onCollectorStatusChanged(const QString &message);
    void onCollectorErrorOccurred(const QString &errorMessage);

private slots:
    void onNavTabChanged(int id, bool checked);
    void onLibraryExploreRequested();

private:
    friend class ::MainWindowTest;

    void buildNavBar();
    void buildStatusBar();
    void setStatusDot(const QColor &color);

    QWidget        *m_navBar{};
    QStackedWidget *m_stack{};
    QButtonGroup   *m_navGroup{};
    LibraryService *m_libraryService{};
    LibraryScreen  *m_libraryScreen{};
    SearchScreen   *m_searchScreen{};
    ExploreScreen  *m_exploreScreen{};

    // Status bar widgets
    QLabel *m_statusLabel{};
    QLabel *m_statusDot{};
};

} // namespace bookhub::gui
