#include "main_window.h"
#include "dialogs/audiobook_flow_dialog.h"
#include "dialogs/download_flow_dialog.h"
#include "panels/book_details_panel.h"
#include "query_worker.h"
#include "screens/explore_screen.h"
#include "screens/library_screen.h"
#include "screens/search_screen.h"
#include "services/library_service.h"
#include "style_tokens.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

namespace bookhub::gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("BookHub"));
  setMinimumSize(WindowMinWidth, WindowMinHeight);
  resize(WindowDefWidth, WindowDefHeight);

  // -----------------------------------------------------------------------
  // Query thread — must be set up before any screen that calls
  // connectToWorker(). The worker is parented to nothing so moveToThread() can
  // transfer ownership.
  // -----------------------------------------------------------------------
  m_queryThread = new QThread(this);
  m_queryWorker = new QueryWorker(); // no parent — will be moved to thread
  m_queryWorker->moveToThread(m_queryThread);

  connect(m_queryThread, &QThread::started, m_queryWorker,
          &QueryWorker::onThreadStarted);
  connect(m_queryThread, &QThread::finished, m_queryWorker,
          &QueryWorker::onThreadFinished);
  connect(m_queryThread, &QThread::finished, m_queryWorker,
          &QObject::deleteLater);

  m_queryThread->start();

  // Shared LibraryService — owned by MainWindow, passed to both LibraryScreen
  // and SearchScreen so they see the same libraryChanged notifications.
  m_libraryService = new LibraryService(this);
  m_libraryService->connectToWorker(m_queryWorker);

  // -----------------------------------------------------------------------
  // Root layout
  // -----------------------------------------------------------------------
  auto *central = new QWidget(this);
  setCentralWidget(central);
  auto *root = new QVBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Nav bar
  m_navBar = new QWidget(central);
  m_navBar->setFixedHeight(NavBarHeight);
  m_navBar->setStyleSheet(
      QStringLiteral("background-color: %1;").arg(ColorNavBg));
  buildNavBar();
  root->addWidget(m_navBar);

  // Content splitter — left: screen stack, right: book details panel (hidden initially)
  m_contentSplitter = new QSplitter(Qt::Horizontal, central);
  m_contentSplitter->setHandleWidth(1);
  m_contentSplitter->setChildrenCollapsible(false);
  m_contentSplitter->setStyleSheet(
      QStringLiteral("QSplitter::handle { background-color: %1; }").arg(ColorBorder));
  root->addWidget(m_contentSplitter, 1);

  // Screen stack (left pane)
  m_stack = new QStackedWidget(m_contentSplitter);
  m_contentSplitter->addWidget(m_stack);

  // Screen 0: Library
  m_libraryScreen = new LibraryScreen(m_libraryService, m_stack);
  connect(m_libraryScreen, &LibraryScreen::exploreRequested, this,
          &MainWindow::onLibraryExploreRequested);
  connect(m_libraryScreen, &LibraryScreen::bookDetailsRequested,
          this, &MainWindow::onBookDetailsRequested);
  m_stack->addWidget(m_libraryScreen); // index 0

  // Screen 1: Search — borrows the shared LibraryService
  m_searchScreen = new SearchScreen(m_libraryService, m_queryWorker, m_stack);
  connect(m_searchScreen, &SearchScreen::bookDetailsRequested,
          this, &MainWindow::onBookDetailsRequested);
  m_stack->addWidget(m_searchScreen); // index 1

  // Screen 2: Explore
  m_exploreScreen = new ExploreScreen(m_queryWorker, m_stack);
  connect(m_exploreScreen, &ExploreScreen::bookDetailsRequested,
          this, &MainWindow::onBookDetailsRequested);
  m_stack->addWidget(m_exploreScreen); // index 2

  m_stack->setCurrentIndex(0);

  // Book details panel (right pane — hidden until a book is clicked)
  m_bookDetailsPanel =
      new BookDetailsPanel(m_libraryService, m_queryWorker, m_contentSplitter);
  m_contentSplitter->addWidget(m_bookDetailsPanel);
  m_bookDetailsPanel->hide();

  connect(m_bookDetailsPanel, &BookDetailsPanel::dismissed,
          this, &MainWindow::onBookDetailsDismissed);
  connect(m_bookDetailsPanel, &BookDetailsPanel::downloadRequested,
          this, &MainWindow::onDownloadRequested);
  connect(m_bookDetailsPanel, &BookDetailsPanel::audiobookRequested,
          this, &MainWindow::onAudiobookRequested);

  m_downloadDialog = new DownloadFlowDialog(m_libraryService, m_queryWorker, this);
  m_audiobookDialog = new AudiobookFlowDialog(m_libraryService, m_queryWorker, nullptr, this);

  // Status bar
  buildStatusBar();
}

MainWindow::~MainWindow() {
  m_queryThread->quit();
  if (!m_queryThread->wait(3000)) {
    qWarning()
        << "QueryWorker: thread did not stop within 3 s — forcing termination";
    m_queryThread->terminate();
    m_queryThread->wait();
  }
}

void MainWindow::buildNavBar() {
  auto *layout = new QHBoxLayout(m_navBar);
  layout->setContentsMargins(SpacingMD, 0, SpacingMD, 0);
  layout->setSpacing(0);

  m_navGroup = new QButtonGroup(m_navBar);
  m_navGroup->setExclusive(true);

  // Shared stylesheet for the three nav tabs.
  // The active tab is indicated with a 3 px bottom border in the accent colour.
  // Using a ::checked pseudo-element avoids a repaint on the unchecked sibling.
  const QString tabStyle = QStringLiteral(R"(
        QPushButton {
            color: %1;
            background: transparent;
            border: none;
            border-bottom: 3px solid transparent;
            padding: 0 %2px;
            min-width: 90px;
            height: %3px;
            font-size: %4pt;
        }
        QPushButton:checked {
            border-bottom-color: %5;
            color: white;
        }
        QPushButton:hover:!checked {
            color: #CBD5E1;
        }
    )")
                               .arg(ColorNavText)
                               .arg(SpacingLG)
                               .arg(NavBarHeight)
                               .arg(FontSizeBody)
                               .arg(ColorNavActive);

  auto makeTab = [&](const QString &label, int id) -> QPushButton * {
    auto *btn = new QPushButton(label, m_navBar);
    btn->setCheckable(true);
    btn->setStyleSheet(tabStyle);
    btn->setAccessibleName(label);
    m_navGroup->addButton(btn, id);
    layout->addWidget(btn);
    return btn;
  };

  auto *libBtn = makeTab(QStringLiteral("Library"), 0);
  makeTab(QStringLiteral("Search"), 1);
  makeTab(QStringLiteral("Explore"), 2);
  libBtn->setChecked(true);

  layout->addStretch();

  connect(m_navGroup, &QButtonGroup::idToggled, this,
          &MainWindow::onNavTabChanged);
}

void MainWindow::buildStatusBar() {
  auto *bar = new QStatusBar(this);
  bar->setFixedHeight(StatusBarHeight);
  bar->setStyleSheet(
      QStringLiteral(
          "QStatusBar { background-color: %1; border-top: 1px solid %2; "
          "  font-size: %3pt; color: %4; }")
          .arg(ColorSurface, ColorBorder)
          .arg(FontSizeMeta)
          .arg(ColorTextMuted));
  setStatusBar(bar);

  m_statusLabel = new QLabel(QStringLiteral("Collector: idle"), bar);
  bar->addWidget(m_statusLabel, 1);

  m_statusDot = new QLabel(QStringLiteral("●"), bar);
  m_statusDot->setStyleSheet(
      QStringLiteral("color: %1; font-size: 10pt;").arg(ColorSuccess));
  m_statusDot->setToolTip(QStringLiteral("Collector idle"));
  bar->addPermanentWidget(m_statusDot);
}

void MainWindow::setStatusDot(const QColor &color) {
  m_statusDot->setStyleSheet(
      QStringLiteral("color: %1; font-size: 10pt;").arg(color.name()));
}

// ---------------------------------------------------------------------------
// Public slots — connected to CollectorWorker via Qt::QueuedConnection
// ---------------------------------------------------------------------------

void MainWindow::onCollectorStatusChanged(const QString &message) {
  // NOTE: CollectorWorker does not currently emit status signals; this slot
  // is wired up in anticipation of those being added (Task tracking TBD).
  m_statusLabel->setText(message);
  setStatusDot(QColor(ColorWarning)); // amber while active
}

void MainWindow::onCollectorErrorOccurred(const QString &errorMessage) {
  m_statusLabel->setText(
      QStringLiteral("Collector error: %1").arg(errorMessage));
  setStatusDot(QColor(ColorError));
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void MainWindow::onNavTabChanged(int id, bool checked) {
  if (checked)
    m_stack->setCurrentIndex(id);
}

void MainWindow::onLibraryExploreRequested() {
  // Switch to the Explore tab (index 2) when the user clicks "Explore Books"
  // from the empty library state. setChecked fires idToggled → onNavTabChanged
  // which calls setCurrentIndex(2), so no explicit setCurrentIndex is needed.
  if (auto *btn = qobject_cast<QPushButton *>(m_navGroup->button(2))) {
    btn->setChecked(true);
  }
}

void MainWindow::onBookDetailsRequested(const QString &bookId) {
  m_bookDetailsPanel->loadBook(bookId);
  if (!m_bookDetailsPanel->isVisible()) {
    m_bookDetailsPanel->show();
    const int total = m_contentSplitter->width();
    m_contentSplitter->setSizes({total / 2, total / 2});
  }
}

void MainWindow::onBookDetailsDismissed() {
  m_bookDetailsPanel->hide();
}

void MainWindow::onDownloadRequested(const QString &bookId) {
  m_downloadDialog->startForBook(bookId);
}

void MainWindow::onAudiobookRequested(const QString &bookId) {
  m_audiobookDialog->startForBook(bookId);
  m_audiobookDialog->exec();
}

} // namespace bookhub::gui
