#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>

#include "gui/panels/book_details_panel.h"
#include "gui/services/library_service.h"

#include "support/test_database_utils.h"
#include "support/test_query_worker.h"

#include <QtTest>
#include <memory>

using namespace bookhub::gui;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool execSql(const QSqlDatabase &db, const QString &sql)
{
    return bookhub::tests::execSql(db, sql);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class BookDetailsPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void loadBook_populatesMetadata();
    void loadBook_showsLoadingThenContent();
    void loadBook_notInLibrary_showsAddButton();
    void loadBook_inLibrary_showsInLibraryButton();
    void loadBook_multipleEditions_showsComboBox();
    void loadBook_singleEdition_showsSingleLabel();
    void loadBook_longSummary_showsShowMoreButton();
    void loadBook_shortSummary_hidesShowMoreButton();
    void loadBook_populatesFormats();
    void addToLibrary_updatesButtonToInLibrary();
    void removeFromLibrary_updatesButtonToAdd();
    void dismissButton_emitsDismissedSignal();
    void languageChange_requestsFormatsForNewEdition();
    void showMore_expandsAndCollapsesSummary();

private:
    // Insert a minimal book + edition + format + source. Returns editionId.
    int insertBook(const QString &bookId, const QString &title = QStringLiteral("Test Title"),
                   const QString &author = QStringLiteral("Test Author"),
                   const QString &language = QStringLiteral("en"))
    {
        const auto &db = m_db->database();
        execSql(db, QStringLiteral(
            "INSERT INTO books (book_id, title, author, publish_year, summary) "
            "VALUES ('%1', '%2', '%3', 1900, 'A short summary.')")
            .arg(bookId, title, author));
        execSql(db, QStringLiteral(
            "INSERT INTO editions (book_id, language) VALUES ('%1', '%2')")
            .arg(bookId, language));
        const int edId = m_db->scalarInt(QStringLiteral(
            "SELECT id FROM editions WHERE book_id='%1' AND language='%2'")
            .arg(bookId, language));
        execSql(db, QStringLiteral(
            "INSERT INTO formats (edition_id, format_type) VALUES (%1, 'epub')")
            .arg(edId));
        const int fmtId = m_db->scalarInt(QStringLiteral(
            "SELECT id FROM formats WHERE edition_id=%1").arg(edId));
        execSql(db, QStringLiteral(
            "INSERT INTO sources (format_id, source_name, download_link) "
            "VALUES (%1, 'Gutenberg', 'https://gutenberg.org/ebooks/1')")
            .arg(fmtId));
        return edId;
    }

    void addToLibrary(const QString &bookId, int editionId)
    {
        execSql(m_db->database(), QStringLiteral(
            "INSERT INTO library_items (book_id, edition_id, status) "
            "VALUES ('%1', %2, 'saved')").arg(bookId).arg(editionId));
    }

    std::unique_ptr<bookhub::tests::TestDatabase> m_db;
    TestQueryWorker  *m_worker{};
    LibraryService   *m_libraryService{};
};

void BookDetailsPanelTest::init()
{
    m_db = std::make_unique<bookhub::tests::TestDatabase>();
    QVERIFY(m_db->open());
    QVERIFY(m_db->createSchema());

    m_worker = new TestQueryWorker();

    m_libraryService = new LibraryService();
    m_libraryService->connectToWorker(m_worker);
}

void BookDetailsPanelTest::cleanup()
{
    delete m_libraryService;
    m_libraryService = nullptr;
    delete m_worker;
    m_worker = nullptr;
    m_db.reset();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void BookDetailsPanelTest::loadBook_populatesMetadata()
{
    insertBook(QStringLiteral("test:1"), QStringLiteral("Pride and Prejudice"),
               QStringLiteral("Jane Austen"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:1"));

    QCOMPARE(panel.m_titleLabel->text(), QStringLiteral("Pride and Prejudice"));
    QCOMPARE(panel.m_authorLabel->text(), QStringLiteral("Jane Austen"));
    QVERIFY(panel.m_metaLabel->text().contains(QStringLiteral("1900")));
}

void BookDetailsPanelTest::loadBook_showsLoadingThenContent()
{
    insertBook(QStringLiteral("test:2"));

    BookDetailsPanel panel(m_libraryService, m_worker);

    // Before loadBook: outer stack index is 0 (loading)
    QCOMPARE(panel.m_outerStack->currentIndex(), 0);

    panel.loadBook(QStringLiteral("test:2"));

    // TestQueryWorker is synchronous — content should be visible immediately
    QCOMPARE(panel.m_outerStack->currentIndex(), 1);
}

void BookDetailsPanelTest::loadBook_notInLibrary_showsAddButton()
{
    insertBook(QStringLiteral("test:3"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:3"));

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("Add")));
    QVERIFY(panel.m_libraryBtn->isEnabled());
}

void BookDetailsPanelTest::loadBook_inLibrary_showsInLibraryButton()
{
    const int edId = insertBook(QStringLiteral("test:4"));
    addToLibrary(QStringLiteral("test:4"), edId);

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:4"));

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("In Library")));
    QVERIFY(panel.m_libraryBtn->isEnabled());
}

void BookDetailsPanelTest::loadBook_multipleEditions_showsComboBox()
{
    const auto &db = m_db->database();
    execSql(db, QStringLiteral(
        "INSERT INTO books (book_id, title, author) VALUES ('test:5', 'Multi', 'Auth')"));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:5', 'en')"));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:5', 'fr')"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:5"));

    QVERIFY(!panel.m_langCombo->isHidden());
    QVERIFY(panel.m_singleLangLabel->isHidden());
    QCOMPARE(panel.m_langCombo->count(), 2);
}

void BookDetailsPanelTest::loadBook_singleEdition_showsSingleLabel()
{
    insertBook(QStringLiteral("test:6"), QStringLiteral("Solo"), QStringLiteral("A"),
               QStringLiteral("de"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:6"));

    QVERIFY(panel.m_langCombo->isHidden());
    QVERIFY(!panel.m_singleLangLabel->isHidden());
    QCOMPARE(panel.m_singleLangLabel->text(), QStringLiteral("German"));
}

void BookDetailsPanelTest::loadBook_longSummary_showsShowMoreButton()
{
    const auto &db = m_db->database();
    const QString longSummary = QString(400, QChar('x'));
    execSql(db, QStringLiteral(
        "INSERT INTO books (book_id, title, summary) VALUES ('test:7', 'Long', '%1')")
        .arg(longSummary));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:7', 'en')"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:7"));

    QVERIFY(!panel.m_showMoreBtn->isHidden());
    QCOMPARE(panel.m_showMoreBtn->text(), QStringLiteral("Show more"));
}

void BookDetailsPanelTest::loadBook_shortSummary_hidesShowMoreButton()
{
    insertBook(QStringLiteral("test:8"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:8"));

    QVERIFY(panel.m_showMoreBtn->isHidden());
}

void BookDetailsPanelTest::loadBook_populatesFormats()
{
    insertBook(QStringLiteral("test:9"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:9"));

    // The formats section should contain at least one row (one format widget)
    const auto rows = panel.m_formatsWidget->findChildren<QWidget *>(
        QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(!rows.isEmpty());

    // The format row should contain a button labelled "[Gutenberg]"
    const auto buttons = panel.m_formatsWidget->findChildren<QPushButton *>();
    const bool hasGutenberg = std::any_of(buttons.begin(), buttons.end(),
        [](QPushButton *b) { return b->text().contains(QStringLiteral("Gutenberg")); });
    QVERIFY(hasGutenberg);
}

void BookDetailsPanelTest::addToLibrary_updatesButtonToInLibrary()
{
    insertBook(QStringLiteral("test:10"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:10"));

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("Add")));

    panel.onAddToLibraryClicked();

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("In Library")));
    QVERIFY(panel.m_inLibrary);
    QVERIFY(panel.m_libraryItemId > 0);
}

void BookDetailsPanelTest::removeFromLibrary_updatesButtonToAdd()
{
    const int edId = insertBook(QStringLiteral("test:11"));
    addToLibrary(QStringLiteral("test:11"), edId);

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:11"));

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("In Library")));

    panel.onRemoveFromLibraryClicked();

    QVERIFY(panel.m_libraryBtn->text().contains(QStringLiteral("Add")));
    QVERIFY(!panel.m_inLibrary);
    QCOMPARE(panel.m_libraryItemId, 0);
}

void BookDetailsPanelTest::dismissButton_emitsDismissedSignal()
{
    insertBook(QStringLiteral("test:12"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    QSignalSpy spy(&panel, &BookDetailsPanel::dismissed);

    const auto buttons = panel.findChildren<QPushButton *>();
    auto it = std::find_if(buttons.begin(), buttons.end(),
                           [](QPushButton *b) {
                               return b->text().contains(QStringLiteral("Back"));
                           });
    QVERIFY(it != buttons.end());
    (*it)->click();

    QCOMPARE(spy.count(), 1);
}

void BookDetailsPanelTest::languageChange_requestsFormatsForNewEdition()
{
    const auto &db = m_db->database();
    execSql(db, QStringLiteral(
        "INSERT INTO books (book_id, title, author) VALUES ('test:13', 'Bi', 'Auth')"));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:13', 'en')"));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:13', 'fr')"));
    // Add a format for the French edition
    const int frEdId = m_db->scalarInt(QStringLiteral(
        "SELECT id FROM editions WHERE book_id='test:13' AND language='fr'"));
    execSql(db, QStringLiteral(
        "INSERT INTO formats (edition_id, format_type) VALUES (%1, 'pdf')").arg(frEdId));
    const int fmtId = m_db->scalarInt(QStringLiteral(
        "SELECT id FROM formats WHERE edition_id=%1").arg(frEdId));
    execSql(db, QStringLiteral(
        "INSERT INTO sources (format_id, source_name, download_link) "
        "VALUES (%1, 'BNF', 'https://bnf.fr/1')").arg(fmtId));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:13"));

    // Switch to French (index 1 — sorted alphabetically: English=0, French=1)
    const int frIndex = panel.m_langCombo->findText(QStringLiteral("French"));
    QVERIFY(frIndex >= 0);
    panel.m_langCombo->setCurrentIndex(frIndex);

    // Formats should now show pdf/BNF
    const auto buttons = panel.m_formatsWidget->findChildren<QPushButton *>();
    const bool hasBnf = std::any_of(buttons.begin(), buttons.end(),
        [](QPushButton *b) { return b->text().contains(QStringLiteral("BNF")); });
    QVERIFY(hasBnf);
}

void BookDetailsPanelTest::showMore_expandsAndCollapsesSummary()
{
    const auto &db = m_db->database();
    const QString longSummary = QString(400, QChar('a'));
    execSql(db, QStringLiteral(
        "INSERT INTO books (book_id, title, summary) VALUES ('test:14', 'S', '%1')")
        .arg(longSummary));
    execSql(db, QStringLiteral(
        "INSERT INTO editions (book_id, language) VALUES ('test:14', 'en')"));

    BookDetailsPanel panel(m_libraryService, m_worker);
    panel.loadBook(QStringLiteral("test:14"));

    QVERIFY(!panel.m_showMoreBtn->isHidden());
    // Initially collapsed
    QVERIFY(panel.m_summaryLabel->text().endsWith(QStringLiteral("…")));

    panel.m_showMoreBtn->click(); // expand
    QCOMPARE(panel.m_summaryLabel->text(), longSummary);
    QCOMPARE(panel.m_showMoreBtn->text(), QStringLiteral("Show less"));

    panel.m_showMoreBtn->click(); // collapse
    QVERIFY(panel.m_summaryLabel->text().endsWith(QStringLiteral("…")));
    QCOMPARE(panel.m_showMoreBtn->text(), QStringLiteral("Show more"));
}

QTEST_MAIN(BookDetailsPanelTest)
#include "test_book_details_panel.moc"
