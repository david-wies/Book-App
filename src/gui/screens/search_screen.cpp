#include "search_screen.h"
#include "../services/search_service.h"
#include "../services/library_service.h"
#include "../widgets/search_result_delegate.h"
#include "../style_tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListView>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QScrollArea>
#include <QComboBox>
#include <QFrame>
#include <QSignalBlocker>
#include <QToolButton>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SearchScreen::SearchScreen(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    m_service        = new SearchService(this);
    m_libraryService = new LibraryService(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(SpacingMD, SpacingMD, SpacingMD, SpacingMD);
    root->setSpacing(SpacingMD);

    // --- Search bar (full width) ---
    auto *searchBarContainer = new QWidget(this);
    searchBarContainer->setFixedHeight(48);
    buildSearchBar(searchBarContainer);
    root->addWidget(searchBarContainer);

    // --- Splitter: filter panel | results pane ---
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle { background-color: %1; }").arg(ColorBorder));

    auto *filterPanel = new QWidget(splitter);
    filterPanel->setMinimumWidth(180);
    buildFilterPanel(filterPanel);
    splitter->addWidget(filterPanel);

    auto *resultsPane = new QWidget(splitter);
    buildResultsPane(resultsPane);
    splitter->addWidget(resultsPane);

    // 220px for filter panel; results pane gets the rest
    splitter->setSizes({220, 9999});
    splitter->setCollapsible(0, true);
    splitter->setCollapsible(1, false);

    root->addWidget(splitter, 1);

    // Timers must exist before populating filter lists — setCheckState fires
    // itemChanged which triggers onFiltersChanged which calls timer->stop().
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &SearchScreen::runSearch);

    m_authorTimer = new QTimer(this);
    m_authorTimer->setSingleShot(true);
    connect(m_authorTimer, &QTimer::timeout, this, &SearchScreen::runSearch);

    // Populate filter controls from DB
    const QStringList genres    = m_service->fetchGenres();
    const QStringList languages = m_service->fetchDistinctLanguages();
    const QStringList sources   = m_service->fetchDistinctSources();

    for (int i = 0; i < genres.size(); ++i) {
        auto *item = new QListWidgetItem(genres[i], m_genreList);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        if (i >= kGenreCollapsed)
            item->setHidden(true);
    }
    m_showMoreGenres->setVisible(genres.size() > kGenreCollapsed);
    {
        const int rowH = m_genreList->sizeHintForRow(0);
        if (rowH > 0)
            m_genreList->setFixedHeight(kGenreCollapsed * rowH);
    }

    for (const QString &lang : languages) {
        auto *item = new QListWidgetItem(lang, m_langList);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }

    for (const QString &src : sources) {
        auto *item = new QListWidgetItem(src, m_srcList);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
}

// ---------------------------------------------------------------------------
// Widget builders
// ---------------------------------------------------------------------------

void SearchScreen::buildSearchBar(QWidget *container)
{
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(SpacingSM);

    m_searchBar = new QLineEdit(container);
    m_searchBar->setPlaceholderText(
        QStringLiteral("Search books by title, author, or keyword…"));
    m_searchBar->setAccessibleName(QStringLiteral("Search books"));
    m_searchBar->setFixedHeight(48);
    m_searchBar->setStyleSheet(QStringLiteral(R"(
        QLineEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
            padding: 0 %4px;
            font-size: %5pt;
            color: %6;
        }
        QLineEdit:focus {
            border-color: %7;
        }
    )").arg(ColorSurface)
       .arg(ColorBorder)
       .arg(RadiusMD)
       .arg(SpacingMD)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary)
       .arg(ColorAccent));

    // Clear button inside the search bar area
    auto *clearBtn = new QToolButton(container);
    clearBtn->setText(QStringLiteral("✕"));
    clearBtn->setFixedSize(32, 32);
    clearBtn->setCursor(Qt::ArrowCursor);
    clearBtn->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; background: transparent; color: %1; "
        "  font-size: %2pt; }")
        .arg(ColorTextMuted)
        .arg(FontSizeBody));
    clearBtn->setToolTip(QStringLiteral("Clear search"));

    connect(clearBtn, &QToolButton::clicked, this, [this] {
        m_searchBar->clear();
        runSearch();
    });
    connect(m_searchBar, &QLineEdit::textChanged,
            this, &SearchScreen::onSearchBarChanged);
    connect(m_searchBar, &QLineEdit::returnPressed,
            this, [this] {
                m_searchTimer->stop();
                runSearch();
            });

    layout->addWidget(m_searchBar, 1);
    layout->addWidget(clearBtn);
}

void SearchScreen::buildFilterPanel(QWidget *panel)
{
    panel->setStyleSheet(QStringLiteral(
        "background-color: %1; border-right: 1px solid %2;")
        .arg(ColorSurface, ColorBorder));

    auto *scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    scroll->setWidget(content);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(SpacingMD, SpacingMD, SpacingMD, SpacingMD);
    layout->setSpacing(SpacingMD);

    const QString sectionStyle = QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;")
        .arg(FontSizeMeta).arg(ColorTextMuted);

    const QString inputStyle = QStringLiteral(R"(
        QLineEdit {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
            padding: 2px 6px;
            font-size: %4pt;
            color: %5;
        }
    )").arg(ColorBackground, ColorBorder)
       .arg(RadiusSM)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary);

    const QString listStyle = QStringLiteral(R"(
        QListWidget {
            background-color: transparent;
            border: none;
            font-size: %1pt;
            color: %2;
        }
        QListWidget::item { padding: 2px 0; }
        QListWidget::item:selected { background: transparent; color: %2; }
    )").arg(FontSizeBody).arg(ColorTextPrimary);

    // --- Author ---
    auto *authorLabel = new QLabel(QStringLiteral("AUTHOR"), content);
    authorLabel->setStyleSheet(sectionStyle);
    layout->addWidget(authorLabel);

    m_authorFilter = new QLineEdit(content);
    m_authorFilter->setPlaceholderText(QStringLiteral("Filter by author…"));
    m_authorFilter->setStyleSheet(inputStyle);
    connect(m_authorFilter, &QLineEdit::textChanged,
            this, &SearchScreen::onAuthorFilterChanged);
    layout->addWidget(m_authorFilter);

    // --- Genre ---
    auto *genreLabel = new QLabel(QStringLiteral("CATEGORY / GENRE"), content);
    genreLabel->setStyleSheet(sectionStyle);
    layout->addWidget(genreLabel);

    m_genreList = new QListWidget(content);
    m_genreList->setStyleSheet(listStyle);
    m_genreList->setSelectionMode(QAbstractItemView::NoSelection);
    m_genreList->setFocusPolicy(Qt::NoFocus);
    connect(m_genreList, &QListWidget::itemChanged,
            this, &SearchScreen::onFiltersChanged);
    layout->addWidget(m_genreList);

    m_showMoreGenres = new QPushButton(QStringLiteral("show more…"), content);
    m_showMoreGenres->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; "
        "  color: %1; font-size: %2pt; text-align: left; padding: 0; }")
        .arg(ColorAccent).arg(FontSizeBody));
    m_showMoreGenres->setCursor(Qt::PointingHandCursor);
    connect(m_showMoreGenres, &QPushButton::clicked,
            this, &SearchScreen::onShowMoreGenres);
    layout->addWidget(m_showMoreGenres);

    // --- Year ---
    auto *yearLabel = new QLabel(QStringLiteral("YEAR"), content);
    yearLabel->setStyleSheet(sectionStyle);
    layout->addWidget(yearLabel);

    auto *yearRow = new QHBoxLayout();
    yearRow->setSpacing(SpacingSM);

    m_yearFrom = new QSpinBox(content);
    m_yearFrom->setRange(1400, 2100);
    m_yearFrom->setValue(1400);
    m_yearFrom->setSpecialValueText(QStringLiteral(" "));
    m_yearFrom->setToolTip(QStringLiteral("Year from"));

    m_yearTo = new QSpinBox(content);
    m_yearTo->setRange(1400, 2100);
    m_yearTo->setValue(2100);
    m_yearTo->setSpecialValueText(QStringLiteral(" "));
    m_yearTo->setToolTip(QStringLiteral("Year to"));

    // Clamp so from ≤ to
    connect(m_yearFrom, &QSpinBox::valueChanged, this, [this](int v) {
        if (v > m_yearTo->value())
            m_yearTo->setValue(v);
        onFiltersChanged();
    });
    connect(m_yearTo, &QSpinBox::valueChanged, this, [this](int v) {
        if (v < m_yearFrom->value())
            m_yearFrom->setValue(v);
        onFiltersChanged();
    });

    yearRow->addWidget(m_yearFrom);
    yearRow->addWidget(new QLabel(QStringLiteral("–"), content));
    yearRow->addWidget(m_yearTo);
    layout->addLayout(yearRow);

    // --- Language ---
    auto *langLabel = new QLabel(QStringLiteral("LANGUAGE"), content);
    langLabel->setStyleSheet(sectionStyle);
    layout->addWidget(langLabel);

    m_langList = new QListWidget(content);
    m_langList->setStyleSheet(listStyle);
    m_langList->setSelectionMode(QAbstractItemView::NoSelection);
    m_langList->setFocusPolicy(Qt::NoFocus);
    m_langList->setMaximumHeight(120);
    connect(m_langList, &QListWidget::itemChanged,
            this, &SearchScreen::onFiltersChanged);
    layout->addWidget(m_langList);

    // --- Source ---
    auto *srcLabel = new QLabel(QStringLiteral("SOURCE"), content);
    srcLabel->setStyleSheet(sectionStyle);
    layout->addWidget(srcLabel);

    m_srcList = new QListWidget(content);
    m_srcList->setStyleSheet(listStyle);
    m_srcList->setSelectionMode(QAbstractItemView::NoSelection);
    m_srcList->setFocusPolicy(Qt::NoFocus);
    m_srcList->setMaximumHeight(80);
    connect(m_srcList, &QListWidget::itemChanged,
            this, &SearchScreen::onFiltersChanged);
    layout->addWidget(m_srcList);

    // --- Availability ---
    auto *availLabel = new QLabel(QStringLiteral("AVAILABILITY"), content);
    availLabel->setStyleSheet(sectionStyle);
    layout->addWidget(availLabel);

    m_ebookCheck = new QCheckBox(QStringLiteral("Ebook available"), content);
    m_ebookCheck->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    connect(m_ebookCheck, &QCheckBox::toggled, this, &SearchScreen::onFiltersChanged);
    layout->addWidget(m_ebookCheck);

    m_audiobookCheck = new QCheckBox(QStringLiteral("Audiobook ready"), content);
    m_audiobookCheck->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    connect(m_audiobookCheck, &QCheckBox::toggled, this, &SearchScreen::onFiltersChanged);
    layout->addWidget(m_audiobookCheck);

    layout->addStretch();

    // --- Clear button ---
    auto *clearBtn = new QPushButton(QStringLiteral("Clear filters"), content);
    clearBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            border: 1px solid %1;
            border-radius: %2px;
            padding: 4px 8px;
            font-size: %3pt;
            color: %4;
        }
        QPushButton:hover { background-color: %1; }
    )").arg(ColorBorder)
       .arg(RadiusSM)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary));
    connect(clearBtn, &QPushButton::clicked, this, &SearchScreen::onClearFilters);
    layout->addWidget(clearBtn);
}

void SearchScreen::buildResultsPane(QWidget *pane)
{
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(SpacingMD, 0, 0, 0);
    layout->setSpacing(SpacingSM);

    // Toolbar: count label + sort combo + view toggles (placeholder)
    auto *toolbar = new QWidget(pane);
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(0, 0, 0, 0);
    tbLayout->setSpacing(SpacingSM);

    m_countLabel = new QLabel(QStringLiteral(""), toolbar);
    m_countLabel->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeMeta).arg(ColorTextMuted));
    tbLayout->addWidget(m_countLabel, 1);

    auto *sortLabel = new QLabel(QStringLiteral("Sort:"), toolbar);
    sortLabel->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeMeta).arg(ColorTextMuted));
    tbLayout->addWidget(sortLabel);

    auto *sortCombo = new QComboBox(toolbar);
    sortCombo->addItem(QStringLiteral("Title A→Z"));
    sortCombo->addItem(QStringLiteral("Author A→Z"));
    sortCombo->setAccessibleName(QStringLiteral("Sort results by"));
    connect(sortCombo, &QComboBox::currentIndexChanged,
            this, &SearchScreen::onSortChanged);
    tbLayout->addWidget(sortCombo);

    layout->addWidget(toolbar);

    // Separator
    auto *sep = new QFrame(pane);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    layout->addWidget(sep);

    // Stacked widget: 0=initial hint, 1=results list, 2=empty state
    m_resultsStack = new QStackedWidget(pane);
    layout->addWidget(m_resultsStack, 1);

    // Index 0: initial state
    auto *initialWidget = new QWidget(m_resultsStack);
    auto *initLayout    = new QVBoxLayout(initialWidget);
    initLayout->setAlignment(Qt::AlignCenter);
    auto *initLabel = new QLabel(
        QStringLiteral("Search across thousands of public-domain books."),
        initialWidget);
    initLabel->setAlignment(Qt::AlignCenter);
    initLabel->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeBody).arg(ColorTextMuted));
    initLayout->addWidget(initLabel);
    m_resultsStack->addWidget(initialWidget); // index 0

    // Index 1: results list + load more button
    auto *resultsWidget = new QWidget(m_resultsStack);
    auto *rLayout       = new QVBoxLayout(resultsWidget);
    rLayout->setContentsMargins(0, 0, 0, 0);
    rLayout->setSpacing(SpacingSM);

    m_model    = new QStandardItemModel(this);
    m_delegate = new SearchResultDelegate(this);

    m_resultsView = new QListView(resultsWidget);
    m_resultsView->setModel(m_model);
    m_resultsView->setItemDelegate(m_delegate);
    m_resultsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsView->setFrameShape(QFrame::NoFrame);
    m_resultsView->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(ColorBackground));
    m_resultsView->setMouseTracking(true);

    connect(m_delegate, &SearchResultDelegate::addToLibraryRequested,
            this, &SearchScreen::onAddToLibrary);
    connect(m_delegate, &SearchResultDelegate::detailsRequested,
            this, &SearchScreen::bookDetailsRequested);

    connect(m_resultsView, &QListView::doubleClicked,
            this, [this](const QModelIndex &idx) {
                const QString bookId = idx.data(SearchRole::BookId).toString();
                if (!bookId.isEmpty())
                    emit bookDetailsRequested(bookId);
            });

    m_loadMoreBtn = new QPushButton(QStringLiteral("Load more results"), resultsWidget);
    m_loadMoreBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            border: 1px solid %1;
            border-radius: %2px;
            padding: 6px 16px;
            font-size: %3pt;
            color: %4;
        }
        QPushButton:hover { background-color: %1; }
    )").arg(ColorBorder)
       .arg(RadiusSM)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary));
    m_loadMoreBtn->setVisible(false);
    connect(m_loadMoreBtn, &QPushButton::clicked, this, &SearchScreen::onLoadMore);

    rLayout->addWidget(m_resultsView, 1);
    rLayout->addWidget(m_loadMoreBtn, 0, Qt::AlignHCenter);
    m_resultsStack->addWidget(resultsWidget); // index 1

    // Index 2: empty state
    auto *emptyWidget = new QWidget(m_resultsStack);
    auto *eLayout     = new QVBoxLayout(emptyWidget);
    eLayout->setAlignment(Qt::AlignCenter);
    auto *emptyLabel = new QLabel(
        QStringLiteral("No books match your search."),
        emptyWidget);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeBody).arg(ColorTextMuted));
    auto *emptyHint = new QLabel(
        QStringLiteral("Try different keywords or clear the filters."),
        emptyWidget);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setStyleSheet(QStringLiteral("font-size: %1pt; color: %2;")
        .arg(FontSizeMeta).arg(ColorTextMuted));
    eLayout->addWidget(emptyLabel);
    eLayout->addWidget(emptyHint);
    m_resultsStack->addWidget(emptyWidget); // index 2

    m_resultsStack->setCurrentIndex(0);
}

// ---------------------------------------------------------------------------
// Filter helpers
// ---------------------------------------------------------------------------

SearchParams SearchScreen::collectParams() const
{
    SearchParams p;
    p.keyword    = m_searchBar->text().trimmed();
    p.author     = m_authorFilter->text().trimmed();
    p.sortColumn = m_currentSortColumn;

    for (int i = 0; i < m_genreList->count(); ++i) {
        const auto *item = m_genreList->item(i);
        if (item->checkState() == Qt::Checked)
            p.genres.append(item->text());
    }

    // Year: treat default boundary values as "no filter"
    const int yFrom = m_yearFrom->value();
    const int yTo   = m_yearTo->value();
    p.yearFrom = (yFrom > 1400) ? yFrom : 0;
    p.yearTo   = (yTo < 2100)   ? yTo   : 0;

    for (int i = 0; i < m_langList->count(); ++i) {
        const auto *item = m_langList->item(i);
        if (item->checkState() == Qt::Checked)
            p.languages.append(item->text());
    }

    for (int i = 0; i < m_srcList->count(); ++i) {
        const auto *item = m_srcList->item(i);
        if (item->checkState() == Qt::Checked)
            p.sources.append(item->text());
    }

    p.ebookOnly     = m_ebookCheck->isChecked();
    p.audiobookOnly = m_audiobookCheck->isChecked();

    return p;
}

bool SearchScreen::isQueryActive() const
{
    const SearchParams p = collectParams();
    return !p.keyword.isEmpty()
        || !p.author.isEmpty()
        || !p.genres.isEmpty()
        || p.yearFrom > 0
        || p.yearTo > 0
        || !p.languages.isEmpty()
        || !p.sources.isEmpty()
        || p.ebookOnly
        || p.audiobookOnly;
}

void SearchScreen::populateModel(const QList<SearchResult> &results, bool append)
{
    if (!append)
        m_model->clear();

    for (const SearchResult &r : results) {
        auto *item = new QStandardItem();
        item->setData(r.bookId,      SearchRole::BookId);
        item->setData(r.title,       SearchRole::Title);
        item->setData(r.author,      SearchRole::Author);
        item->setData(r.publishYear, SearchRole::PublishYear);
        item->setData(r.languages,   SearchRole::Languages);
        item->setData(r.sources,     SearchRole::Sources);
        item->setData(r.formats,     SearchRole::Formats);
        item->setData(r.inLibrary,   SearchRole::InLibrary);

        // Accessibility announcement
        item->setAccessibleText(
            QStringLiteral("%1 by %2, %3. Formats: %4. Source: %5.")
                .arg(r.title, r.author)
                .arg(r.publishYear)
                .arg(r.formats.join(QLatin1String(", ")),
                     r.sources.join(QLatin1String(", ")))
        );
        item->setEditable(false);
        m_model->appendRow(item);
    }
}

void SearchScreen::updateCountLabel(int count)
{
    if (count == 0) {
        m_countLabel->setText(QStringLiteral("No results"));
    } else {
        m_countLabel->setText(
            QStringLiteral("%1 book%2").arg(count).arg(count == 1 ? QString{} : QStringLiteral("s")));
    }
}

void SearchScreen::setResultsState(int state)
{
    m_resultsStack->setCurrentIndex(state);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SearchScreen::onSearchBarChanged()
{
    // 500 ms debounce — restarting the timer on each keystroke
    m_searchTimer->start(500);
}

void SearchScreen::onAuthorFilterChanged()
{
    // 300 ms debounce for the author sub-filter
    m_authorTimer->start(300);
}

void SearchScreen::onFiltersChanged()
{
    // Other filters trigger immediate search (no debounce needed for checkbox/spinbox)
    m_searchTimer->stop();
    m_authorTimer->stop();
    runSearch();
}

void SearchScreen::runSearch()
{
    if (!isQueryActive()) {
        m_model->clear();
        m_currentOffset = 0;
        m_totalCount    = 0;
        m_loadMoreBtn->setVisible(false);
        m_countLabel->setText(QStringLiteral(""));
        setResultsState(0);
        return;
    }

    const SearchParams params = collectParams();

    // Count query runs only when filters change (not on "Load more")
    m_totalCount = m_service->count(params);
    updateCountLabel(m_totalCount);

    m_currentOffset = 0;
    const QList<SearchResult> results = m_service->search(params, 0, kPageSize);

    populateModel(results, false);

    if (results.isEmpty()) {
        setResultsState(2); // empty state
        m_loadMoreBtn->setVisible(false);
    } else {
        setResultsState(1); // results
        m_currentOffset = results.size();
        m_loadMoreBtn->setVisible(m_currentOffset < m_totalCount);
    }
}

void SearchScreen::onLoadMore()
{
    const SearchParams params = collectParams();
    const QList<SearchResult> results =
        m_service->search(params, m_currentOffset, kPageSize);

    if (results.isEmpty())
        return;

    populateModel(results, true);
    m_currentOffset += results.size();
    m_loadMoreBtn->setVisible(m_currentOffset < m_totalCount);
}

void SearchScreen::onAddToLibrary(const QString &bookId)
{
    const int newId = m_libraryService->addBook(bookId, 0);
    if (newId < 0)
        return; // addBook already logs the error

    // Flip the inLibrary flag on the affected model row so the delegate
    // repaints the button without a full re-query.
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r);
        if (item->data(SearchRole::BookId).toString() == bookId) {
            item->setData(true, SearchRole::InLibrary);
            break;
        }
    }
}

void SearchScreen::onClearFilters()
{
    // Block signals while resetting to avoid triggering multiple re-searches.
    const QSignalBlocker bSearchBar(m_searchBar);
    const QSignalBlocker bAuthorFilter(m_authorFilter);
    const QSignalBlocker bGenreList(m_genreList);
    const QSignalBlocker bLangList(m_langList);
    const QSignalBlocker bSrcList(m_srcList);
    const QSignalBlocker bEbookCheck(m_ebookCheck);
    const QSignalBlocker bAudiobookCheck(m_audiobookCheck);

    m_searchBar->clear();
    m_authorFilter->clear();

    for (int i = 0; i < m_genreList->count(); ++i)
        m_genreList->item(i)->setCheckState(Qt::Unchecked);
    for (int i = 0; i < m_langList->count(); ++i)
        m_langList->item(i)->setCheckState(Qt::Unchecked);
    for (int i = 0; i < m_srcList->count(); ++i)
        m_srcList->item(i)->setCheckState(Qt::Unchecked);

    m_yearFrom->setValue(1400);
    m_yearTo->setValue(2100);

    m_ebookCheck->setChecked(false);
    m_audiobookCheck->setChecked(false);

    m_searchTimer->stop();
    m_authorTimer->stop();

    // Return to initial state (no query active)
    m_model->clear();
    m_currentOffset = 0;
    m_totalCount    = 0;
    m_loadMoreBtn->setVisible(false);
    m_countLabel->setText(QStringLiteral(""));
    setResultsState(0);
}

void SearchScreen::onSortChanged(int index)
{
    m_currentSortColumn = (index == 1)
        ? QStringLiteral("author")
        : QStringLiteral("title");

    runSearch();
}

void SearchScreen::onShowMoreGenres()
{
    m_genresExpanded = !m_genresExpanded;
    for (int i = kGenreCollapsed; i < m_genreList->count(); ++i)
        m_genreList->item(i)->setHidden(!m_genresExpanded);

    // Resize the list to fit the newly visible items
    const int rows = m_genresExpanded ? m_genreList->count() : kGenreCollapsed;
    const int rowH = m_genreList->sizeHintForRow(0);
    if (rowH > 0)
        m_genreList->setFixedHeight(rows * rowH);

    m_showMoreGenres->setText(
        m_genresExpanded ? QStringLiteral("show less") : QStringLiteral("show more…"));
}

} // namespace bookhub::gui
