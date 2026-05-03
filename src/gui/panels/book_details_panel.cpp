#include "book_details_panel.h"

#include "../services/book_details_service.h"
#include "../services/library_service.h"
#include "../style_tokens.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BookDetailsPanel::BookDetailsPanel(LibraryService *libraryService,
                                   QueryWorker    *worker,
                                   QWidget        *parent)
    : QWidget(parent)
    , m_libraryService(libraryService)
{
    m_detailsService = new BookDetailsService(this);
    m_detailsService->connectToWorker(worker);

    buildUi();

    connect(m_detailsService, &BookDetailsService::detailsCompleted,
            this, &BookDetailsPanel::onDetailsCompleted);
    connect(m_detailsService, &BookDetailsService::formatsCompleted,
            this, &BookDetailsPanel::onFormatsCompleted);
    connect(m_libraryService, &LibraryService::addBookCompleted,
            this, &BookDetailsPanel::onAddBookCompleted);
    connect(m_libraryService, &LibraryService::removeBookCompleted,
            this, &BookDetailsPanel::onRemoveBookCompleted);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BookDetailsPanel::loadBook(const QString &bookId)
{
    m_currentBookId = bookId;
    m_outerStack->setCurrentIndex(0); // show loading before the request fires
    m_pendingDetailsId = m_detailsService->peekNextId();
    m_detailsService->requestBookDetails(bookId);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void BookDetailsPanel::onDetailsCompleted(quint64 requestId, const BookDetails &details)
{
    if (requestId != m_pendingDetailsId)
        return;

    populateDetails(details);
    m_outerStack->setCurrentIndex(1); // reveal content

    if (!details.editions.isEmpty()) {
        m_pendingFormatsId = m_detailsService->peekNextId();
        m_detailsService->requestFormatsForEdition(details.editions.first().editionId);
    } else {
        populateFormats({});
    }
}

void BookDetailsPanel::onFormatsCompleted(quint64 requestId,
                                          const QList<BookFormatEntry> &formats)
{
    if (requestId != m_pendingFormatsId)
        return;
    populateFormats(formats);
}

void BookDetailsPanel::onLanguageChanged(int index)
{
    if (index < 0 || index >= m_editionIds.size())
        return;
    m_pendingFormatsId = m_detailsService->peekNextId();
    m_detailsService->requestFormatsForEdition(m_editionIds.at(index));
}

void BookDetailsPanel::onAddToLibraryClicked()
{
    const int editionId = m_editionIds.value(m_langCombo->currentIndex(), 0);
    m_libraryBtn->setEnabled(false);
    m_pendingAddId = m_libraryService->peekNextId();
    m_libraryService->requestAddBook(m_currentBookId, editionId);
}

void BookDetailsPanel::onRemoveFromLibraryClicked()
{
    m_libraryBtn->setEnabled(false);
    m_pendingRemoveId = m_libraryService->peekNextId();
    m_libraryService->requestRemoveBook(m_libraryItemId, m_currentBookId);
}

void BookDetailsPanel::onAddBookCompleted(quint64 requestId, const QString &/*bookId*/,
                                          bool success, int newId)
{
    if (requestId != m_pendingAddId)
        return;
    m_pendingAddId = 0;
    if (success) {
        m_inLibrary     = true;
        m_libraryItemId = newId;
    }
    setLibraryButtonState(m_inLibrary);
}

void BookDetailsPanel::onRemoveBookCompleted(quint64 requestId, const QString &/*bookId*/, bool success)
{
    if (requestId != m_pendingRemoveId)
        return;
    m_pendingRemoveId = 0;
    if (success) {
        m_inLibrary     = false;
        m_libraryItemId = 0;
    }
    setLibraryButtonState(m_inLibrary);
}

void BookDetailsPanel::onToggleSummary()
{
    m_summaryExpanded = !m_summaryExpanded;
    if (m_summaryExpanded) {
        m_summaryLabel->setText(m_fullSummary);
        m_showMoreBtn->setText(QStringLiteral("Show less"));
    } else {
        m_summaryLabel->setText(
            m_fullSummary.left(kSummaryCollapseLen) + QStringLiteral("…"));
        m_showMoreBtn->setText(QStringLiteral("Show more"));
    }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void BookDetailsPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -----------------------------------------------------------------------
    // Back button bar (40 px)
    // -----------------------------------------------------------------------
    auto *backBar = new QWidget(this);
    backBar->setFixedHeight(40);
    backBar->setObjectName(QStringLiteral("detailsBackBar"));
    backBar->setStyleSheet(QStringLiteral(
        "#detailsBackBar {"
        "  background-color: %1;"
        "  border-bottom: 1px solid %2;"
        "}").arg(ColorBackground, ColorBorder));

    auto *backLayout = new QHBoxLayout(backBar);
    backLayout->setContentsMargins(SpacingSM, 0, SpacingSM, 0);
    backLayout->setSpacing(0);

    auto *backBtn = new QPushButton(QStringLiteral("← Back"), backBar);
    backBtn->setFlat(true);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setToolTip(QStringLiteral("Close book details"));
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: %2pt; border: none; background: transparent; }"
        "QPushButton:hover { color: %3; }")
        .arg(ColorAccent).arg(FontSizeBody).arg(ColorAccentHover));
    connect(backBtn, &QPushButton::clicked, this, &BookDetailsPanel::dismissed);

    backLayout->addWidget(backBtn);
    backLayout->addStretch();
    root->addWidget(backBar);

    // -----------------------------------------------------------------------
    // Outer stack: 0 = loading, 1 = content
    // -----------------------------------------------------------------------
    m_outerStack = new QStackedWidget(this);
    root->addWidget(m_outerStack, 1);

    // --- Loading page (index 0) ---
    auto *loadingPage   = new QWidget(m_outerStack);
    auto *loadingLayout = new QVBoxLayout(loadingPage);
    auto *loadingLabel  = new QLabel(QStringLiteral("Loading…"), loadingPage);
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(ColorTextMuted).arg(FontSizeBody));
    loadingLayout->addStretch();
    loadingLayout->addWidget(loadingLabel, 0, Qt::AlignCenter);
    loadingLayout->addStretch();
    m_outerStack->addWidget(loadingPage); // index 0

    // --- Content page (index 1) ---
    auto *contentPage   = new QWidget(m_outerStack);
    auto *contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Scroll area
    auto *scrollArea = new QScrollArea(contentPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }"
        "QScrollArea > QWidget > QWidget { background: %1; }")
        .arg(ColorSurface));

    auto *inner = new QWidget(scrollArea);
    inner->setObjectName(QStringLiteral("detailsInner"));
    inner->setStyleSheet(QStringLiteral("#detailsInner { background: %1; }").arg(ColorSurface));

    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(SpacingLG, SpacingLG, SpacingLG, SpacingLG);
    innerLayout->setSpacing(SpacingSM);

    // Cover placeholder
    auto *coverLabel = new QLabel(inner);
    coverLabel->setFixedSize(200, 280);
    coverLabel->setAlignment(Qt::AlignCenter);
    coverLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background-color: %1;"
        "  border-radius: %2px;"
        "  border: 1px solid %3;"
        "  font-size: 48pt;"
        "}").arg(ColorBackground).arg(RadiusMD).arg(ColorBorder));
    coverLabel->setText(QStringLiteral("📖"));
    innerLayout->addWidget(coverLabel, 0, Qt::AlignHCenter);
    innerLayout->addSpacing(SpacingSM);

    // Title
    m_titleLabel = new QLabel(inner);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;")
        .arg(FontSizeTitle).arg(ColorTextPrimary));
    innerLayout->addWidget(m_titleLabel);

    // Author
    m_authorLabel = new QLabel(inner);
    m_authorLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; color: %2;")
        .arg(FontSizeBody + 1).arg(ColorTextMuted));
    innerLayout->addWidget(m_authorLabel);

    // Meta: year + genres
    m_metaLabel = new QLabel(inner);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; color: %2;")
        .arg(FontSizeMeta).arg(ColorTextMuted));
    innerLayout->addWidget(m_metaLabel);

    // Separator
    auto *sep1 = new QFrame(inner);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    innerLayout->addWidget(sep1);

    // Language section
    auto *langRow    = new QWidget(inner);
    auto *langLayout = new QHBoxLayout(langRow);
    langLayout->setContentsMargins(0, 0, 0, 0);
    langLayout->setSpacing(SpacingSM);

    m_langLabel = new QLabel(QStringLiteral("Language:"), langRow);
    m_langLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    langLayout->addWidget(m_langLabel);

    m_singleLangLabel = new QLabel(langRow);
    m_singleLangLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; color: %2;"
        "background: %3; border: 1px solid %4; border-radius: %5px; padding: 2px 8px;")
        .arg(FontSizeBadge)
        .arg(ColorLangBadgeText, ColorLangBadgeBg, ColorLangBadgeBorder)
        .arg(RadiusPill));

    m_langCombo = new QComboBox(langRow);
    m_langCombo->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(FontSizeBody));
    connect(m_langCombo, &QComboBox::currentIndexChanged,
            this, &BookDetailsPanel::onLanguageChanged);

    langLayout->addWidget(m_singleLangLabel);
    langLayout->addWidget(m_langCombo);
    langLayout->addStretch();
    innerLayout->addWidget(langRow);

    // Separator
    auto *sep2 = new QFrame(inner);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    innerLayout->addWidget(sep2);

    // Summary
    auto *summaryHeader = new QLabel(QStringLiteral("Summary"), inner);
    summaryHeader->setStyleSheet(QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    innerLayout->addWidget(summaryHeader);

    m_summaryLabel = new QLabel(inner);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet(QStringLiteral(
        "font-size: %1pt; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    innerLayout->addWidget(m_summaryLabel);

    m_showMoreBtn = new QPushButton(QStringLiteral("Show more"), inner);
    m_showMoreBtn->setFlat(true);
    m_showMoreBtn->setCursor(Qt::PointingHandCursor);
    m_showMoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: %2pt; border: none;"
        "  text-align: left; padding: 0; background: transparent; }"
        "QPushButton:hover { color: %3; }")
        .arg(ColorAccent).arg(FontSizeBody).arg(ColorAccentHover));
    m_showMoreBtn->setVisible(false);
    connect(m_showMoreBtn, &QPushButton::clicked,
            this, &BookDetailsPanel::onToggleSummary);
    innerLayout->addWidget(m_showMoreBtn);

    // Separator
    auto *sep3 = new QFrame(inner);
    sep3->setFrameShape(QFrame::HLine);
    sep3->setStyleSheet(QStringLiteral("color: %1;").arg(ColorBorder));
    innerLayout->addWidget(sep3);

    // Formats section
    auto *formatsHeader = new QLabel(QStringLiteral("Available formats"), inner);
    formatsHeader->setStyleSheet(QStringLiteral(
        "font-size: %1pt; font-weight: bold; color: %2;")
        .arg(FontSizeBody).arg(ColorTextPrimary));
    innerLayout->addWidget(formatsHeader);

    m_formatsWidget = new QWidget(inner);
    m_formatsLayout = new QVBoxLayout(m_formatsWidget);
    m_formatsLayout->setContentsMargins(0, 0, 0, 0);
    m_formatsLayout->setSpacing(SpacingXS);
    innerLayout->addWidget(m_formatsWidget);

    innerLayout->addStretch();
    scrollArea->setWidget(inner);
    contentLayout->addWidget(scrollArea, 1);

    // -----------------------------------------------------------------------
    // Action bar (pinned below scroll, always visible)
    // -----------------------------------------------------------------------
    auto *actionBar = new QWidget(contentPage);
    actionBar->setObjectName(QStringLiteral("detailsActionBar"));
    actionBar->setStyleSheet(QStringLiteral(
        "#detailsActionBar {"
        "  background-color: %1;"
        "  border-top: 1px solid %2;"
        "}").arg(ColorSurface, ColorBorder));

    auto *actionLayout = new QVBoxLayout(actionBar);
    actionLayout->setContentsMargins(SpacingMD, SpacingSM, SpacingMD, SpacingSM);
    actionLayout->setSpacing(SpacingSM);

    m_libraryBtn = new QPushButton(actionBar);
    m_libraryBtn->setFixedHeight(40);
    m_libraryBtn->setCursor(Qt::PointingHandCursor);
    connect(m_libraryBtn, &QPushButton::clicked, this, [this] {
        if (m_inLibrary)
            onRemoveFromLibraryClicked();
        else
            onAddToLibraryClicked();
    });
    actionLayout->addWidget(m_libraryBtn);
    setLibraryButtonState(false); // sets initial style + text

    auto *secondaryRow    = new QWidget(actionBar);
    auto *secondaryLayout = new QHBoxLayout(secondaryRow);
    secondaryLayout->setContentsMargins(0, 0, 0, 0);
    secondaryLayout->setSpacing(SpacingSM);

    const QString secondaryStyle = QStringLiteral(
        "QPushButton {"
        "  border: 1px solid %1; border-radius: %2px;"
        "  font-size: %3pt; color: %4; background: transparent; padding: 8px;"
        "}"
        "QPushButton:hover { background: %5; }"
        "QPushButton:disabled { color: %6; border-color: %6; }")
        .arg(ColorBorder).arg(RadiusMD).arg(FontSizeBody)
        .arg(ColorTextPrimary, ColorBackground, ColorTextMuted);

    m_downloadBtn = new QPushButton(QStringLiteral("↓ Download"), secondaryRow);
    m_downloadBtn->setStyleSheet(secondaryStyle);
    m_downloadBtn->setToolTip(QStringLiteral("Download this book"));
    m_downloadBtn->setEnabled(false); // Task 11
    connect(m_downloadBtn, &QPushButton::clicked, this,
            [this] { emit downloadRequested(m_currentBookId); });
    secondaryLayout->addWidget(m_downloadBtn);

    m_audiobookBtn = new QPushButton(QStringLiteral("♪ Audiobook"), secondaryRow);
    m_audiobookBtn->setStyleSheet(secondaryStyle);
    m_audiobookBtn->setToolTip(QStringLiteral("Audiobook conversion — coming soon"));
    m_audiobookBtn->setEnabled(false); // Task 12
    connect(m_audiobookBtn, &QPushButton::clicked, this,
            [this] { emit audiobookRequested(m_currentBookId); });
    secondaryLayout->addWidget(m_audiobookBtn);

    actionLayout->addWidget(secondaryRow);
    contentLayout->addWidget(actionBar);

    m_outerStack->addWidget(contentPage); // index 1
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BookDetailsPanel::populateDetails(const BookDetails &details)
{
    m_inLibrary     = details.inLibrary;
    m_libraryItemId = details.libraryItemId;

    m_titleLabel->setText(details.title.isEmpty()
        ? QStringLiteral("Unknown Title") : details.title);
    m_authorLabel->setText(details.author.isEmpty()
        ? QStringLiteral("Unknown Author") : details.author);

    QStringList metaParts;
    if (details.publishYear > 0)
        metaParts << QString::number(details.publishYear);
    if (!details.genres.isEmpty())
        metaParts << details.genres.join(QStringLiteral(", "));
    m_metaLabel->setText(metaParts.join(QStringLiteral("  •  ")));
    m_metaLabel->setVisible(!metaParts.isEmpty());

    // Populate edition IDs before populating combo (avoids stale index in
    // onLanguageChanged which fires when the combo transitions from empty to
    // the first item).
    m_editionIds.clear();
    for (const auto &ed : details.editions)
        m_editionIds.append(ed.editionId);

    {
        QSignalBlocker blocker(m_langCombo);
        m_langCombo->clear();
        for (const auto &ed : details.editions)
            m_langCombo->addItem(ed.language);
    }

    const qsizetype edCount = details.editions.size();
    const bool multiLang = edCount > 1;
    m_langCombo->setVisible(multiLang);
    m_singleLangLabel->setVisible(!multiLang && edCount == 1);
    m_langLabel->setVisible(edCount > 0);
    if (!multiLang && edCount == 1)
        m_singleLangLabel->setText(details.editions.first().language);

    // Summary
    m_fullSummary    = details.summary.isEmpty()
        ? QStringLiteral("No summary available for this edition.")
        : details.summary;
    m_summaryExpanded = false;

    if (m_fullSummary.length() > kSummaryCollapseLen) {
        m_summaryLabel->setText(
            m_fullSummary.left(kSummaryCollapseLen) + QStringLiteral("…"));
        m_showMoreBtn->setText(QStringLiteral("Show more"));
        m_showMoreBtn->setVisible(true);
    } else {
        m_summaryLabel->setText(m_fullSummary);
        m_showMoreBtn->setVisible(false);
    }

    setLibraryButtonState(m_inLibrary);
    m_downloadBtn->setEnabled(false);

    // Clear formats; they'll be loaded by the requestFormatsForEdition call
    // that follows immediately in onDetailsCompleted.
    clearFormatsSection();
    auto *placeholder = new QLabel(QStringLiteral("Loading formats…"),
                                   m_formatsWidget);
    placeholder->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
        .arg(ColorTextMuted).arg(FontSizeBody));
    m_formatsLayout->addWidget(placeholder);
}

void BookDetailsPanel::populateFormats(const QList<BookFormatEntry> &formats)
{
    clearFormatsSection();

    if (formats.isEmpty()) {
        m_downloadBtn->setEnabled(false);
        auto *noFmt = new QLabel(
            QStringLiteral("No downloadable formats available yet."),
            m_formatsWidget);
        noFmt->setWordWrap(true);
        noFmt->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt;")
            .arg(ColorTextMuted).arg(FontSizeBody));
        m_formatsLayout->addWidget(noFmt);
        return;
    }

    m_downloadBtn->setEnabled(true);

    for (const auto &fmt : formats) {
        auto *row       = new QWidget(m_formatsWidget);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, SpacingXS, 0, SpacingXS);
        rowLayout->setSpacing(SpacingSM);

        auto *fmtLabel = new QLabel(fmt.formatType, row);
        fmtLabel->setFixedWidth(36);
        fmtLabel->setStyleSheet(QStringLiteral(
            "font-size: %1pt; font-weight: bold; color: %2;")
            .arg(FontSizeBody).arg(ColorTextPrimary));
        rowLayout->addWidget(fmtLabel);

        for (const auto &src : fmt.sources) {
            auto *srcBtn = new QPushButton(
                QStringLiteral("[%1]").arg(src.sourceName), row);
            srcBtn->setToolTip(src.downloadLink);
            srcBtn->setCursor(Qt::PointingHandCursor);
            srcBtn->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  color: %1; background: %2; border: 1px solid %3;"
                "  border-radius: %4px; padding: 2px 8px; font-size: %5pt;"
                "}"
                "QPushButton:hover { background: %6; }")
                .arg(ColorSrcBadgeText, ColorSrcBadgeBg, ColorSrcBadgeBorder)
                .arg(RadiusPill).arg(FontSizeBadge)
                .arg(ColorSrcBadgeHover));
            rowLayout->addWidget(srcBtn);
        }

        rowLayout->addStretch();
        m_formatsLayout->addWidget(row);
    }
}

void BookDetailsPanel::setLibraryButtonState(bool inLibrary)
{
    if (inLibrary) {
        m_libraryBtn->setText(QStringLiteral("✓ In Library  (click to remove)"));
        m_libraryBtn->setToolTip(QStringLiteral("Remove from library"));
        m_libraryBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background-color: %1; color: white;"
            "  border-radius: %2px; font-size: %3pt; border: none;"
            "}"
            "QPushButton:hover { background-color: #15803D; }"
            "QPushButton:disabled { background-color: %4; color: %5; }")
            .arg(ColorSuccess).arg(RadiusMD).arg(FontSizeBody)
            .arg(ColorBorder, ColorTextMuted));
    } else {
        m_libraryBtn->setText(QStringLiteral("＋ Add to Library"));
        m_libraryBtn->setToolTip(QStringLiteral("Add to library"));
        m_libraryBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background-color: %1; color: white;"
            "  border-radius: %2px; font-size: %3pt; border: none;"
            "}"
            "QPushButton:hover { background-color: %4; }"
            "QPushButton:disabled { background-color: %5; color: %6; }")
            .arg(ColorAccent).arg(RadiusMD).arg(FontSizeBody)
            .arg(ColorAccentHover, ColorBorder, ColorTextMuted));
    }
    m_libraryBtn->setEnabled(true);
}

void BookDetailsPanel::clearFormatsSection()
{
    while (QLayoutItem *item = m_formatsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

} // namespace bookhub::gui
