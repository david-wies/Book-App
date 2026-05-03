#include "library_screen.h"
#include "../services/library_service.h"
#include "../query_worker.h"
#include "../widgets/book_card_delegate.h"
#include "../widgets/empty_state_widget.h"
#include "../style_tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QComboBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QLabel>
#include <QProgressBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QMenu>
#include <QAction>

namespace bookhub::gui {

// Sort combo index → SQL column mapping (must stay in sync with QComboBox items below)
static const char* kSortColumns[] = {
    "added_date", // 0: Date Added (default)
    "title",      // 1: Title A→Z
    "author",     // 2: Author A→Z
    "status",     // 3: Status
};

LibraryScreen::LibraryScreen(QueryWorker *worker, QWidget *parent)
    : QWidget(parent)
{
    auto *service = new LibraryService(this);
    service->connectToWorker(worker);
    init(service);
}

LibraryScreen::LibraryScreen(LibraryService *service, QWidget *parent)
    : QWidget(parent)
{
    init(service);
}

void LibraryScreen::init(LibraryService *service)
{
    m_service = service;

    setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar
    auto *toolbar = new QWidget(this);
    toolbar->setFixedHeight(LibraryToolbarH);
    toolbar->setStyleSheet(QStringLiteral(
        "background-color: %1; border-bottom: 1px solid %2;"
    ).arg(ColorSurface, ColorBorder));
    buildToolbar(toolbar);
    root->addWidget(toolbar);

    // Stacked widget: list view (0) or empty state (1)
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // List view
    auto *listContainer = new QWidget(m_stack);
    auto *listLayout    = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);

    m_model    = new QStandardItemModel(this);
    m_delegate = new BookCardDelegate(this);

    m_listView = new QListView(listContainer);
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));
    m_listView->setMouseTracking(true); // needed so editorEvent receives hover events

    listLayout->addWidget(m_listView);
    m_stack->addWidget(listContainer); // index 0

    // Empty state
    m_emptyState = new EmptyStateWidget(
        QIcon::fromTheme(QStringLiteral("folder-open"),
                         QIcon(QStringLiteral(":/book_reader_icon.jpg"))),
        QStringLiteral("Your library is empty."),
        QStringLiteral("Start by exploring books from the Explore tab."),
        QStringLiteral("Explore Books"),
        m_stack
    );
    connect(m_emptyState, &EmptyStateWidget::ctaClicked,
            this, &LibraryScreen::exploreRequested);
    m_stack->addWidget(m_emptyState); // index 1

    // Loading state (index 2) — shown during the initial synchronous DB query
    auto *loadingWidget = new QWidget(m_stack);
    auto *loadingLayout = new QVBoxLayout(loadingWidget);
    loadingLayout->setAlignment(Qt::AlignCenter);
    auto *loadingLabel = new QLabel(QStringLiteral("Loading library…"), loadingWidget);
    loadingLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
                                    .arg(ColorTextMuted).arg(FontSizeBody));
    loadingLabel->setAlignment(Qt::AlignCenter);
    auto *progressBar = new QProgressBar(loadingWidget);
    progressBar->setRange(0, 0); // indeterminate
    progressBar->setFixedWidth(240);
    loadingLayout->addWidget(loadingLabel);
    loadingLayout->addWidget(progressBar, 0, Qt::AlignCenter);
    m_stack->addWidget(loadingWidget); // index 2
    m_stack->setCurrentIndex(2);      // show loading until first reload() completes

    // Wire up service
    connect(m_service, &LibraryService::libraryChanged,
            this, &LibraryScreen::reload);
    connect(m_service, &LibraryService::fetchItemsCompleted,
            this, &LibraryScreen::onFetchItemsCompleted);

    // Wire up delegate signals
    connect(m_delegate, &BookCardDelegate::detailsRequested,
            this, &LibraryScreen::onDetailsRequested);
    connect(m_delegate, &BookCardDelegate::downloadRequested,
            this, &LibraryScreen::onDownloadRequested);
    connect(m_delegate, &BookCardDelegate::audiobookRequested,
            this, &LibraryScreen::onAudiobookRequested);
    connect(m_delegate, &BookCardDelegate::removeRequested,
            this, &LibraryScreen::onRemoveRequested);

    // Double-click on a row also opens details
    connect(m_listView, &QListView::doubleClicked,
            this, [this](const QModelIndex &idx) {
                const QString bookId = idx.data(LibraryRole::BookId).toString();
                if (!bookId.isEmpty())
                    emit bookDetailsRequested(bookId);
            });

    // Restore saved view mode — must happen after m_listView is constructed
    QSettings settings;
    const int savedViewMode = settings.value(QStringLiteral("Library/viewMode"), 0).toInt();
    if (savedViewMode == 1) {
        // Trigger the grid button — QButtonGroup::idToggled fires onViewToggled
        if (auto *gridBtn = qobject_cast<QPushButton *>(m_viewGroup->button(1)))
            gridBtn->setChecked(true);
    }

    reload();
}

void LibraryScreen::buildToolbar(QWidget *toolbar)
{
    auto *layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(SpacingMD, 0, SpacingMD, 0);
    layout->setSpacing(SpacingMD);

    m_countLabel = new QLabel(QStringLiteral("Library"), toolbar);
    m_countLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;"
    ).arg(FontSizeSubtitle).arg(ColorTextPrimary));
    layout->addWidget(m_countLabel);
    layout->addStretch();

    auto *sortLabel = new QLabel(QStringLiteral("Sort:"), toolbar);
    sortLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
                             .arg(ColorTextMuted).arg(FontSizeBody));
    layout->addWidget(sortLabel);

    m_sortCombo = new QComboBox(toolbar);
    m_sortCombo->addItem(QStringLiteral("Date Added"));
    m_sortCombo->addItem(QStringLiteral("Title A→Z"));
    m_sortCombo->addItem(QStringLiteral("Author A→Z"));
    m_sortCombo->addItem(QStringLiteral("Status"));
    m_sortCombo->setAccessibleName(QStringLiteral("Sort library by"));
    connect(m_sortCombo, &QComboBox::currentIndexChanged,
            this, &LibraryScreen::onSortChanged);
    layout->addWidget(m_sortCombo);

    // List / Grid toggle buttons
    m_viewGroup = new QButtonGroup(toolbar);
    m_viewGroup->setExclusive(true);

    auto makeViewBtn = [&](const QString &label, int id) -> QPushButton * {
        auto *btn = new QPushButton(label, toolbar);
        btn->setCheckable(true);
        btn->setFixedSize(32, 28);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { border: 1px solid %1; border-radius: %2px; "
            "  background: transparent; font-size: %3pt; }"
            "QPushButton:checked { background-color: %4; color: white; border-color: %4; }"
        ).arg(ColorBorder)
         .arg(RadiusSM)
         .arg(FontSizeMeta)
         .arg(ColorAccent));
        m_viewGroup->addButton(btn, id);
        return btn;
    };

    auto *listBtn = makeViewBtn(QStringLiteral("☰"), 0);
    auto *gridBtn = makeViewBtn(QStringLiteral("⊞"), 1);
    listBtn->setChecked(true);
    listBtn->setToolTip(QStringLiteral("List view"));
    gridBtn->setToolTip(QStringLiteral("Grid view"));

    layout->addWidget(listBtn);
    layout->addWidget(gridBtn);

    connect(m_viewGroup, &QButtonGroup::idToggled,
            this, &LibraryScreen::onViewToggled);
}

void LibraryScreen::reload()
{
    const int sortIdx = m_sortCombo ? m_sortCombo->currentIndex() : 0;
    const QString col = QString::fromLatin1(kSortColumns[sortIdx]);
    m_stack->setCurrentIndex(2); // show loading while query is in flight
    m_pendingFetchId = m_service->requestFetchItems(col);
}

void LibraryScreen::onFetchItemsCompleted(quint64 requestId, const QList<LibraryItem> &items)
{
    if (requestId < m_pendingFetchId)
        return; // stale response — a newer reload() is already in flight

    populateModel(items);
    updateCountLabel(static_cast<int>(items.size()));
    m_stack->setCurrentIndex(items.isEmpty() ? 1 : 0);
}

void LibraryScreen::populateModel(const QList<LibraryItem> &items)
{
    m_model->clear();
    for (const LibraryItem &item : items) {
        auto *row = new QStandardItem();
        row->setData(item.bookId,     LibraryRole::BookId);
        row->setData(item.title,      LibraryRole::Title);
        row->setData(item.author,     LibraryRole::Author);
        row->setData(item.language,   LibraryRole::Language);
        row->setData(item.sourceName, LibraryRole::SourceName);
        row->setData(item.status,     LibraryRole::Status);
        row->setData(item.id,         LibraryRole::LibraryItemId);
        row->setData(item.editionId,  LibraryRole::EditionId);

        // Accessibility announcement for screen readers
        row->setAccessibleText(
            QStringLiteral("%1 by %2. Status: %3. Language: %4.")
                .arg(item.title, item.author, item.status, item.language)
        );

        row->setEditable(false);
        m_model->appendRow(row);
    }
}

void LibraryScreen::updateCountLabel(int count)
{
    if (!m_countLabel)
        return;
    m_countLabel->setText(
        count == 0 ? QStringLiteral("Library")
                   : QStringLiteral("Library (%1 book%2)")
                         .arg(count)
                         .arg(count == 1 ? QString{} : QStringLiteral("s"))
    );
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void LibraryScreen::onSortChanged(int /*index*/)
{
    reload();
}

void LibraryScreen::onViewToggled(int id, bool checked)
{
    if (!checked)
        return;

    QSettings settings;
    settings.setValue(QStringLiteral("Library/viewMode"), id);

    const ViewMode mode = (id == 1) ? ViewMode::Grid : ViewMode::List;
    m_delegate->setViewMode(mode);

    if (mode == ViewMode::Grid) {
        m_listView->setViewMode(QListView::IconMode);
        m_listView->setResizeMode(QListView::Adjust);
        m_listView->setGridSize({BookCardGridW + SpacingMD, BookCardGridH + SpacingMD});
        m_listView->setSpacing(SpacingMD);
        m_listView->setWrapping(true);
    } else {
        m_listView->setViewMode(QListView::ListMode);
        m_listView->setGridSize({});
        m_listView->setSpacing(0);
        m_listView->setWrapping(false);
    }

    m_listView->update();
}

void LibraryScreen::onDetailsRequested(int /*libraryItemId*/, const QString &bookId)
{
    // TODO: open BookDetailsPanel (Task 9) — for now just emit the signal
    emit bookDetailsRequested(bookId);
}

void LibraryScreen::onDownloadRequested(int libraryItemId, const QString &bookId)
{
    // TODO: open DownloadFlowDialog (Task 11)
    Q_UNUSED(libraryItemId)
    Q_UNUSED(bookId)
}

void LibraryScreen::onAudiobookRequested(int libraryItemId, const QString &bookId)
{
    // TODO: open AudiobookFlowDialog (Task 12)
    Q_UNUSED(libraryItemId)
    Q_UNUSED(bookId)
}

void LibraryScreen::onRemoveRequested(int libraryItemId, const QString &bookId)
{
    // Resolve the display title from the model so the dialog names the book.
    QString title = bookId;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r);
        if (item->data(LibraryRole::LibraryItemId).toInt() == libraryItemId) {
            title = item->data(LibraryRole::Title).toString();
            break;
        }
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Remove from library"),
        QStringLiteral("Remove \"%1\" from your library?").arg(title),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (answer == QMessageBox::Yes)
        m_service->requestRemoveBook(libraryItemId, bookId);
}

} // namespace bookhub::gui
