#include "explore_screen.h"
#include "../services/explore_service.h"
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
// BookMiniCard — 120 × 180 fixed-size clickable card showing a book's cover
// placeholder, title (2 lines max, elided), and author (1 line, elided).
//
// The cover placeholder is drawn in paintEvent as a rounded rectangle using
// ColorBorder as the fill so it follows the application theme without hard-
// coding a colour.  bookClicked is intentionally left unwired until Task 9.
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

        // Cover placeholder occupies the upper ~130 px; text is below.
        // We use a nested QVBoxLayout rather than painting text ourselves so
        // Qt handles font metrics and HiDPI scaling automatically.
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);

        // Cover area — styled QLabel acts as the grey placeholder rectangle
        m_coverLabel = new QLabel(this);
        m_coverLabel->setFixedSize(108, 128);
        m_coverLabel->setAlignment(Qt::AlignCenter);
        m_coverLabel->setStyleSheet(QStringLiteral(
            "background-color: %1; border-radius: %2px;")
            .arg(ColorBorder)
            .arg(RadiusMD));
        layout->addWidget(m_coverLabel);

        // Title label — bold 10pt, word-wrap capped to 2 lines via fixed height
        m_titleLabel = new QLabel(elidedTwoLines(title, 108, 10), this);
        m_titleLabel->setWordWrap(true);
        // Fix height to exactly 2 lines so cards align in the carousel
        QFont titleFont = m_titleLabel->font();
        titleFont.setPointSize(10);
        titleFont.setBold(true);
        m_titleLabel->setFont(titleFont);
        m_titleLabel->setFixedHeight(QFontMetrics(titleFont).lineSpacing() * 2);
        m_titleLabel->setStyleSheet(QStringLiteral("color: %1; border: none; background: transparent;")
            .arg(ColorTextPrimary));
        layout->addWidget(m_titleLabel);

        // Author label — muted 9pt, single line elided
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
    // Elide text to fit within `widthPx` pixels over at most 2 lines.
    // This is a best-effort approximation — Qt word-wrap handles final layout.
    static QString elidedTwoLines(const QString &text, int widthPx, int pointSize)
    {
        QFont f;
        f.setPointSize(pointSize);
        f.setBold(true);
        QFontMetrics fm(f);

        // If the whole text fits in one line, return as-is
        if (fm.horizontalAdvance(text) <= widthPx)
            return text;

        // Try to split at a word boundary that fills two lines
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

        // Elide line2 to fit
        const QString elided2 = fm.elidedText(line2, Qt::ElideRight, widthPx);
        return line1 + QLatin1Char('\n') + elided2;
    }

    QString  m_bookId;
    QString  m_title;
    QString  m_author;
    QLabel  *m_coverLabel{};
    QLabel  *m_titleLabel{};
    QLabel  *m_authorLabel{};
};

// ===========================================================================
// CategoryCard — 220 × 80 minimum-size clickable frame showing a genre name
// and its book count.  Background rotates through a small pastel palette so
// adjacent cards are visually distinct without requiring image assets.
// ===========================================================================

// Pastel palette indexed by (cardIndex % 6). Values chosen to be visible on
// both light and dark screen backgrounds while remaining unobtrusive.
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

        // Format count with thousands separator for readability
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

// MOC must be included for inner Q_OBJECT classes defined in a .cpp file.
#include "explore_screen.moc"

// ===========================================================================
// ExploreScreen
// ===========================================================================

ExploreScreen::ExploreScreen(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    m_service = new ExploreService(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Internal navigation stack: index 0 = top-level, index 1 = genre drill-down
    m_exploreStack = new QStackedWidget(this);
    m_exploreStack->addWidget(buildTopLevelPage()); // index 0
    m_exploreStack->addWidget(buildGenreBooksPage()); // index 1
    m_exploreStack->setCurrentIndex(0);

    root->addWidget(m_exploreStack, 1);
}

// ---------------------------------------------------------------------------
// Top-level page — scrollable, sections built top to bottom
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildTopLevelPage()
{
    // Outer scroll area wraps everything on the top-level page so the user can
    // scroll down to reach New Arrivals on small-height windows.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    auto *page   = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(SpacingLG, SpacingLG, SpacingLG, SpacingLG);
    layout->setSpacing(SpacingMD);

    // Section header style — uppercase, muted, small
    const QString headerStyle = QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2; letter-spacing: 1px;")
        .arg(FontSizeMeta)
        .arg(ColorTextMuted);

    // ---- TRENDING section ----
    auto *trendingLabel = new QLabel(QStringLiteral("TRENDING"), page);
    trendingLabel->setStyleSheet(headerStyle);
    layout->addWidget(trendingLabel);

    layout->addWidget(buildCarousel(m_service->fetchTrending()));

    // ---- BROWSE CATEGORIES section ----
    auto *categoriesLabel = new QLabel(QStringLiteral("BROWSE CATEGORIES"), page);
    categoriesLabel->setStyleSheet(headerStyle);
    layout->addWidget(categoriesLabel);

    layout->addWidget(buildCategoryGrid(m_service->fetchCategories()));

    // ---- NEW ARRIVALS section ----
    auto *arrivalsLabel = new QLabel(QStringLiteral("NEW ARRIVALS"), page);
    arrivalsLabel->setStyleSheet(headerStyle);
    layout->addWidget(arrivalsLabel);

    layout->addWidget(buildCarousel(m_service->fetchNewArrivals()));

    layout->addStretch();

    scrollArea->setWidget(page);
    return scrollArea;
}

// ---------------------------------------------------------------------------
// Horizontal carousel of BookMiniCards inside a QScrollArea
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildCarousel(const QList<ExploreBook> &books)
{
    // The carousel is a horizontally scrollable strip. We never show a
    // vertical scrollbar — cards are fixed-height so that's not needed.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidgetResizable(false);
    scrollArea->setFixedHeight(196); // card height (180) + breathing room (16)
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

    // Equalise column widths so cards fill available width evenly
    for (int c = 0; c < kCols; ++c)
        grid->setColumnStretch(c, 1);

    return container;
}

// ---------------------------------------------------------------------------
// Genre drill-down page — built once, repopulated on each category click
// ---------------------------------------------------------------------------

QWidget *ExploreScreen::buildGenreBooksPage()
{
    auto *page   = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(SpacingLG, SpacingMD, SpacingLG, SpacingLG);
    layout->setSpacing(SpacingMD);

    // --- Header row: back button + breadcrumb ---
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

    // Separator
    auto *sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    layout->addWidget(sep);

    // --- Scrollable book grid ---
    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background-color: %1;").arg(ColorBackground));

    // m_genreGridContainer is repopulated by appendGenreBooks each time
    m_genreGridContainer = new QWidget(scrollArea);
    m_genreGridLayout    = new QGridLayout(m_genreGridContainer);
    m_genreGridLayout->setContentsMargins(0, 0, 0, 0);
    m_genreGridLayout->setSpacing(SpacingMD);

    // Equal column stretches for the 4-column grid
    for (int c = 0; c < kGridColumns; ++c)
        m_genreGridLayout->setColumnStretch(c, 1);

    scrollArea->setWidget(m_genreGridContainer);
    layout->addWidget(scrollArea, 1);

    // --- Load more button ---
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

    // Clear the grid before repopulating
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
    const QList<ExploreBook> books =
        m_service->fetchBooksForGenre(m_currentGenre, offset, kGenrePageSize);

    if (books.isEmpty() && offset == 0) {
        // Genre exists but has no books yet — show a placeholder label
        auto *empty = new QLabel(
            QStringLiteral("No books found for this category."),
            m_genreGridContainer);
        empty->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
            .arg(ColorTextMuted).arg(FontSizeBody));
        // Span all four columns
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
        const int row = absoluteIdx / kGridColumns;
        const int col = absoluteIdx % kGridColumns;
        m_genreGridLayout->addWidget(card, row, col);
    }

    m_genreOffset = offset + books.size();

    // Show "Load more" if a full page was returned — there may be more
    m_loadMoreBtn->setVisible(books.size() == kGenrePageSize);
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
