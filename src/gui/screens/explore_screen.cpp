#include "explore_screen.h"
#include "../services/explore_service.h"
#include "../query_worker.h"
#include "../style_tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QSizePolicy>

namespace bookhub::gui {

// ===========================================================================
// BookMiniCard — 120 × 180 fixed-size clickable card.
// ===========================================================================

class BookMiniCard : public QFrame {
    Q_OBJECT
public:
    BookMiniCard(const QString &bookId,
                 const QString &title,
                 const QString &author,
                 QWidget *parent = nullptr)
        : QFrame(parent)
        , m_bookId(bookId)
        , m_title(title)
        , m_author(author)
    {
        setFixedSize(120, 180);
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);

        setStyleSheet(QStringLiteral(R"(
            BookMiniCard {
                background-color: %1;
                border: 1px solid %2;
                border-radius: %3px;
            }
            BookMiniCard:hover {
                border-color: %4;
            }
        )").arg(ColorSurface)
           .arg(ColorBorder)
           .arg(RadiusMD)
           .arg(ColorAccent));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);

        m_coverLabel = new QLabel(this);
        m_coverLabel->setFixedSize(108, 128);
        m_coverLabel->setAlignment(Qt::AlignCenter);
        m_coverLabel->setStyleSheet(QStringLiteral(
            "background-color: %1; border-radius: %2px;")
            .arg(ColorBorder)
            .arg(RadiusMD));
        layout->addWidget(m_coverLabel);

        m_titleLabel = new QLabel(elidedTwoLines(title, 108, 10), this);
        m_titleLabel->setWordWrap(true);
        QFont titleFont = m_titleLabel->font();
        titleFont.setPointSize(10);
        titleFont.setBold(true);
        m_titleLabel->setFont(titleFont);
        m_titleLabel->setFixedHeight(QFontMetrics(titleFont).lineSpacing() * 2);
        m_titleLabel->setStyleSheet(QStringLiteral("color: %1; border: none; background: transparent;")
            .arg(ColorTextPrimary));
        layout->addWidget(m_titleLabel);

        m_authorLabel = new QLabel(this);
        QFont authorFont = m_authorLabel->font();
        authorFont.setPointSize(9);
        m_authorLabel->setFont(authorFont);
        m_authorLabel->setStyleSheet(QStringLiteral("color: %1; border: none; background: transparent;")
            .arg(ColorTextMuted));
        m_authorLabel->setText(
            QFontMetrics(authorFont).elidedText(author, Qt::ElideRight, 108));
        m_authorLabel->setFixedHeight(QFontMetrics(authorFont).lineSpacing());
        layout->addWidget(m_authorLabel);

        layout->addStretch();

        setToolTip(QStringLiteral("%1\n%2").arg(title, author));
        setAccessibleName(QStringLiteral("%1 by %2").arg(title, author));
    }

signals:
    void bookClicked(const QString &bookId);

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit bookClicked(m_bookId);
        QFrame::mousePressEvent(event);
    }

private:
    static QString elidedTwoLines(const QString &text, int widthPx, int pointSize)
    {
        QFont f;
        f.setPointSize(pointSize);
        f.setBold(true);
        QFontMetrics fm(f);

        if (fm.horizontalAdvance(text) <= widthPx)
            return text;

        QStringList words = text.split(QLatin1Char(' '));
        QString line1, line2;
        for (const QString &w : words) {
            const QString candidate = line1.isEmpty() ? w : line1 + QLatin1Char(' ') + w;
            if (fm.horizontalAdvance(candidate) <= widthPx) {
                line1 = candidate;
            } else {
                if (line2.isEmpty())
                    line2 = w;
                else
                    line2 += QLatin1Char(' ') + w;
            }
        }

        if (line2.isEmpty())
            return line1;

        return line1 + QLatin1Char('\n') + fm.elidedText(line2, Qt::ElideRight, widthPx);
    }

    QString  m_bookId;
    QString  m_title;
    QString  m_author;
    QLabel  *m_coverLabel{};
    QLabel  *m_titleLabel{};
    QLabel  *m_authorLabel{};
};

// ===========================================================================
// CategoryCard
// ===========================================================================

static constexpr const char* kCategoryPastels[] = {
    "#EFF6FF", "#F0FDF4", "#FFF7ED", "#FDF4FF", "#FEF9C3", "#F0F9FF"
};
static constexpr const char* kCategoryPastelsHover[] = {
    "#DBEAFE", "#DCFCE7", "#FFEDD5", "#F3E8FF", "#FEF08A", "#E0F2FE"
};
static_assert(std::size(kCategoryPastels)     == 6, "palette must have 6 entries");
static_assert(std::size(kCategoryPastelsHover) == 6, "hover palette must have 6 entries");

class CategoryCard : public QFrame {
    Q_OBJECT
public:
    CategoryCard(const QString &genreName, int bookCount, int index,
                 QWidget *parent = nullptr)
        : QFrame(parent)
        , m_genreName(genreName)
    {
        setMinimumSize(220, 80);
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);

        const int paletteIdx = index % 6;
        m_normalBg = QLatin1String(kCategoryPastels[paletteIdx]);
        m_hoverBg  = QLatin1String(kCategoryPastelsHover[paletteIdx]);

        applyStyle(m_normalBg);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(SpacingMD, SpacingSM, SpacingMD, SpacingSM);
        layout->setSpacing(4);

        auto *nameLabel = new QLabel(genreName, this);
        QFont nameFont = nameLabel->font();
        nameFont.setPointSize(13);
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        nameLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(ColorTextPrimary));
        layout->addWidget(nameLabel);

        const QString countText = QStringLiteral("%L1 book%2")
            .arg(bookCount)
            .arg(bookCount == 1 ? QString{} : QStringLiteral("s"));
        auto *countLabel = new QLabel(countText, this);
        QFont countFont = countLabel->font();
        countFont.setPointSize(11);
        countLabel->setFont(countFont);
        countLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(ColorTextMuted));
        layout->addWidget(countLabel);

        setToolTip(QStringLiteral("Browse %1").arg(genreName));
        setAccessibleName(QStringLiteral("%1, %2").arg(genreName, countText));
    }

signals:
    void clicked(const QString &genreName);

protected:
    void enterEvent(QEnterEvent *event) override
    {
        applyStyle(m_hoverBg);
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        applyStyle(m_normalBg);
        QFrame::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit clicked(m_genreName);
        QFrame::mousePressEvent(event);
    }

private:
    void applyStyle(const QString &bg)
    {
        setStyleSheet(QStringLiteral(
            "CategoryCard { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
            .arg(bg)
            .arg(ColorBorder)
            .arg(RadiusMD));
    }

    QString m_genreName;
    QString m_normalBg;
    QString m_hoverBg;
};

#include "explore_screen.moc"

// ===========================================================================
// ExploreScreen
// ===========================================================================

ExploreScreen::ExploreScreen(QueryWorker *worker, QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    m_service = new ExploreService(this);
    m_service->connectToWorker(worker);

    connect(m_service, &ExploreService::trendingCompleted,
            this, &ExploreScreen::onTrendingCompleted);
    connect(m_service, &ExploreService::newArrivalsCompleted,
            this, &ExploreScreen::onNewArrivalsCompleted);
    connect(m_service, &ExploreService::categoriesCompleted,
            this, &ExploreScreen::onCategoriesCompleted);
    connect(m_service, &ExploreService::booksForGenreCompleted,
            this, &ExploreScreen::onBooksForGenreCompleted);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_exploreStack = new QStackedWidget(this);
    m_exploreStack->addWidget(buildTopLevelPage()); // index 0
    m_exploreStack->addWidget(buildGenreBooksPage()); // index 1
    m_exploreStack->setCurrentIndex(0);

    root->addWidget(m_exploreStack, 1);

    loadTopLevelData();
}

// ---------------------------------------------------------------------------
// Top-level page — scroll area with placeholder carousels/grid
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildTopLevelPage()
{
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    auto *page   = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(SpacingLG, SpacingLG, SpacingLG, SpacingLG);
    layout->setSpacing(SpacingMD);
    m_topLevelLayout = layout; // store for later widget replacement

    const QString headerStyle = QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2; letter-spacing: 1px;")
        .arg(FontSizeMeta)
        .arg(ColorTextMuted);

    // index 0: TRENDING label
    auto *trendingLabel = new QLabel(QStringLiteral("TRENDING"), page);
    trendingLabel->setStyleSheet(headerStyle);
    layout->addWidget(trendingLabel); // index 0

    // index 1: trending carousel placeholder
    m_trendingCarousel = buildCarousel({});
    layout->addWidget(m_trendingCarousel); // index 1

    // index 2: BROWSE CATEGORIES label
    auto *categoriesLabel = new QLabel(QStringLiteral("BROWSE CATEGORIES"), page);
    categoriesLabel->setStyleSheet(headerStyle);
    layout->addWidget(categoriesLabel); // index 2

    // index 3: category grid placeholder
    m_categoryGrid = buildCategoryGrid({});
    layout->addWidget(m_categoryGrid); // index 3

    // index 4: NEW ARRIVALS label
    auto *arrivalsLabel = new QLabel(QStringLiteral("NEW ARRIVALS"), page);
    arrivalsLabel->setStyleSheet(headerStyle);
    layout->addWidget(arrivalsLabel); // index 4

    // index 5: new arrivals carousel placeholder
    m_newArrivalsCarousel = buildCarousel({});
    layout->addWidget(m_newArrivalsCarousel); // index 5

    layout->addStretch(); // index 6

    scrollArea->setWidget(page);
    return scrollArea;
}

void ExploreScreen::loadTopLevelData()
{
    m_pendingTrendingId    = m_service->requestTrending();
    m_pendingCategoriesId  = m_service->requestCategories();
    m_pendingNewArrivalsId = m_service->requestNewArrivals();
}

// ---------------------------------------------------------------------------
// Async result slots — replace placeholders with populated widgets
// ---------------------------------------------------------------------------

void ExploreScreen::onTrendingCompleted(quint64 requestId,
                                         QList<ExploreBook> books)
{
    if (requestId < m_pendingTrendingId)
        return;

    m_topLevelLayout->removeWidget(m_trendingCarousel);
    delete m_trendingCarousel;
    m_trendingCarousel = buildCarousel(books);
    m_topLevelLayout->insertWidget(kTrendingCarouselIndex, m_trendingCarousel);
}

void ExploreScreen::onNewArrivalsCompleted(quint64 requestId,
                                            QList<ExploreBook> books)
{
    if (requestId < m_pendingNewArrivalsId)
        return;

    m_topLevelLayout->removeWidget(m_newArrivalsCarousel);
    delete m_newArrivalsCarousel;
    m_newArrivalsCarousel = buildCarousel(books);
    m_topLevelLayout->insertWidget(kNewArrivalsCarouselIndex, m_newArrivalsCarousel);
}

void ExploreScreen::onCategoriesCompleted(quint64 requestId,
                                           QList<ExploreCategory> categories)
{
    if (requestId < m_pendingCategoriesId)
        return;

    m_topLevelLayout->removeWidget(m_categoryGrid);
    delete m_categoryGrid;
    m_categoryGrid = buildCategoryGrid(categories);
    m_topLevelLayout->insertWidget(kCategoryGridIndex, m_categoryGrid);
}

void ExploreScreen::onBooksForGenreCompleted(quint64 requestId,
                                              QList<ExploreBook> books)
{
    if (requestId < m_pendingGenreId)
        return;

    const int offset = m_genreOffset;

    if (books.isEmpty() && offset == 0) {
        auto *empty = new QLabel(
            QStringLiteral("No books found for this category."),
            m_genreGridContainer);
        empty->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
            .arg(ColorTextMuted).arg(FontSizeBody));
        m_genreGridLayout->addWidget(empty, 0, 0, 1, kGridColumns);
        m_loadMoreBtn->setVisible(false);
        return;
    }

    for (int i = 0; i < books.size(); ++i) {
        const ExploreBook &b = books[i];
        auto *card = new BookMiniCard(b.bookId, b.title, b.author, m_genreGridContainer);
        connect(card, &BookMiniCard::bookClicked,
                this, &ExploreScreen::bookDetailsRequested);

        const int absoluteIdx = offset + i;
        m_genreGridLayout->addWidget(card, absoluteIdx / kGridColumns,
                                            absoluteIdx % kGridColumns);
    }

    m_genreOffset = offset + books.size();
    m_loadMoreBtn->setVisible(books.size() == kGenrePageSize);
    m_loadMoreBtn->setEnabled(true);
}

// ---------------------------------------------------------------------------
// Horizontal carousel of BookMiniCards
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildCarousel(const QList<ExploreBook> &books)
{
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidgetResizable(false);
    scrollArea->setFixedHeight(196);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *strip  = new QWidget(scrollArea);
    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(0, 4, 0, 8);
    layout->setSpacing(SpacingMD);

    if (books.isEmpty()) {
        auto *placeholder = new QLabel(
            QStringLiteral("No books available yet."), strip);
        placeholder->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
            .arg(ColorTextMuted).arg(FontSizeBody));
        layout->addWidget(placeholder);
    } else {
        for (const ExploreBook &b : books) {
            auto *card = new BookMiniCard(b.bookId, b.title, b.author, strip);
            connect(card, &BookMiniCard::bookClicked,
                    this, &ExploreScreen::bookDetailsRequested);
            layout->addWidget(card);
        }
    }

    layout->addStretch();
    strip->setLayout(layout);
    strip->adjustSize();
    scrollArea->setWidget(strip);
    return scrollArea;
}

// ---------------------------------------------------------------------------
// Category grid — 3 columns of CategoryCards
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildCategoryGrid(const QList<ExploreCategory> &categories)
{
    auto *container = new QWidget(this);
    auto *grid      = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(SpacingMD);

    constexpr int kCols = 3;

    if (categories.isEmpty()) {
        auto *placeholder = new QLabel(
            QStringLiteral("No categories found."), container);
        placeholder->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
            .arg(ColorTextMuted).arg(FontSizeBody));
        grid->addWidget(placeholder, 0, 0);
    } else {
        for (int i = 0; i < categories.size(); ++i) {
            const ExploreCategory &cat = categories[i];
            auto *card = new CategoryCard(cat.genreName, cat.bookCount, i, container);
            connect(card, &CategoryCard::clicked,
                    this, &ExploreScreen::onCategoryClicked);
            grid->addWidget(card, i / kCols, i % kCols);
        }
    }

    for (int c = 0; c < kCols; ++c)
        grid->setColumnStretch(c, 1);

    return container;
}

// ---------------------------------------------------------------------------
// Genre drill-down page
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildGenreBooksPage()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(SpacingLG, SpacingMD, SpacingLG, SpacingLG);
    layout->setSpacing(SpacingMD);

    auto *headerRow    = new QWidget(page);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(SpacingSM);

    auto *backBtn = new QPushButton(QStringLiteral("← Back"), headerRow);
    backBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            border: 1px solid %1;
            border-radius: %2px;
            padding: 4px 12px;
            font-size: %3pt;
            color: %4;
        }
        QPushButton:hover {
            background-color: %1;
        }
    )").arg(ColorBorder)
       .arg(RadiusSM)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary));
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, [this] {
        m_exploreStack->setCurrentIndex(0);
    });
    headerLayout->addWidget(backBtn);

    m_breadcrumb = new QLabel(page);
    QFont bcFont = m_breadcrumb->font();
    bcFont.setPointSize(FontSizeSubtitle);
    bcFont.setBold(true);
    m_breadcrumb->setFont(bcFont);
    m_breadcrumb->setStyleSheet(QStringLiteral("color: %1;").arg(ColorTextPrimary));
    headerLayout->addWidget(m_breadcrumb, 1);

    layout->addWidget(headerRow);

    auto *sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    layout->addWidget(sep);

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    m_genreGridContainer = new QWidget(scrollArea);
    m_genreGridLayout    = new QGridLayout(m_genreGridContainer);
    m_genreGridLayout->setContentsMargins(0, 0, 0, 0);
    m_genreGridLayout->setSpacing(SpacingMD);

    for (int c = 0; c < kGridColumns; ++c)
        m_genreGridLayout->setColumnStretch(c, 1);

    scrollArea->setWidget(m_genreGridContainer);
    layout->addWidget(scrollArea, 1);

    m_loadMoreBtn = new QPushButton(QStringLiteral("Load more"), page);
    m_loadMoreBtn->setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background-color: transparent;
            border: 1px solid %1;
            border-radius: %2px;
            padding: 6px 20px;
            font-size: %3pt;
            color: %4;
        }
        QPushButton:hover {
            background-color: %1;
        }
    )").arg(ColorBorder)
       .arg(RadiusSM)
       .arg(FontSizeBody)
       .arg(ColorTextPrimary));
    m_loadMoreBtn->setCursor(Qt::PointingHandCursor);
    m_loadMoreBtn->setVisible(false);
    connect(m_loadMoreBtn, &QPushButton::clicked, this, &ExploreScreen::onLoadMore);
    layout->addWidget(m_loadMoreBtn, 0, Qt::AlignHCenter);

    return page;
}

// ---------------------------------------------------------------------------
// Genre drill-down helpers
// ---------------------------------------------------------------------------

void ExploreScreen::showGenreBooks(const QString &genreName)
{
    m_currentGenre = genreName;
    m_genreOffset  = 0;

    while (QLayoutItem *item = m_genreGridLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    m_breadcrumb->setText(genreName);
    m_loadMoreBtn->setVisible(false);

    appendGenreBooks(0);
    m_exploreStack->setCurrentIndex(1);
}

void ExploreScreen::appendGenreBooks(int offset)
{
    m_loadMoreBtn->setEnabled(false);
    m_genreOffset = offset; // updated in onBooksForGenreCompleted once results arrive
    m_pendingGenreId = m_service->requestBooksForGenre(
        m_currentGenre, offset, kGenrePageSize);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ExploreScreen::onCategoryClicked(const QString &genreName)
{
    showGenreBooks(genreName);
}

void ExploreScreen::onLoadMore()
{
    appendGenreBooks(m_genreOffset);
}

} // namespace bookhub::gui
