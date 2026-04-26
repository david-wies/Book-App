#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListView>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QSplitter>
#include <QCoreApplication>
#include <QSettings>

#include "gui/screens/search_screen.h"

#include "gui/widgets/search_result_delegate.h"
#include "support/test_database_utils.h"

#include <QtTest>
#include <memory>

using namespace bookhub::gui;

class SearchScreenTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void debounceAndEnterTriggerSearch();
    void clearFiltersAndAddToLibrary_updateUiState();
    void loadMoreAndYearClamp_work();

private:
    std::unique_ptr<bookhub::tests::TestDatabase> m_db;
};

void SearchScreenTest::init()
{
    bookhub::tests::isolateSettings(QStringLiteral("bookhub-test-search-screen"));
    QCoreApplication::setOrganizationName(QStringLiteral("BookHub"));
    QCoreApplication::setApplicationName(QStringLiteral("BookHub"));
    QSettings settings(QStringLiteral("BookHub"), QStringLiteral("BookHub"));
    settings.clear();

    m_db = std::make_unique<bookhub::tests::TestDatabase>();
    QVERIFY(m_db->open());
    QVERIFY(m_db->createSchema());
    QVERIFY(m_db->insertSampleData());

    for (int i = 0; i < 60; ++i) {
        const int id = 1000 + i;
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO books (book_id, title, author, publish_year) "
            "VALUES ('bulk:%1', 'Bulk Book %1', 'Bulk Author', 1900)").arg(id)));
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO editions (book_id, language) VALUES ('bulk:%1', 'English')").arg(id)));
        const int editionId = m_db->scalarInt(QStringLiteral(
            "SELECT id FROM editions WHERE book_id = 'bulk:%1'").arg(id));
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO formats (edition_id, format_type) VALUES (%1, 'epub')").arg(editionId)));
        const int formatId = m_db->scalarInt(QStringLiteral(
            "SELECT id FROM formats WHERE edition_id = %1").arg(editionId));
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO sources (format_id, source_name, download_link) "
            "VALUES (%1, 'Gutenberg', 'https://example.test/%2.epub')").arg(formatId).arg(id)));
    }
}

void SearchScreenTest::cleanup()
{
    m_db.reset();
}

void SearchScreenTest::debounceAndEnterTriggerSearch()
{
    SearchScreen screen;

    screen.m_searchBar->setText(QStringLiteral("Pride"));
    QCOMPARE(screen.m_resultsStack->currentIndex(), 0);

    screen.triggerSearchNow();
    QCOMPARE(screen.m_resultsStack->currentIndex(), 1);
    QVERIFY(screen.m_model->rowCount() >= 1);

    screen.m_searchBar->clear();
    screen.m_searchBar->setText(QStringLiteral("Huckleberry"));
    QTest::keyClick(screen.m_searchBar, Qt::Key_Return);
    QCOMPARE(screen.m_resultsStack->currentIndex(), 1);
    QVERIFY(screen.m_model->rowCount() >= 1);
}

void SearchScreenTest::clearFiltersAndAddToLibrary_updateUiState()
{
    SearchScreen screen;
    screen.m_searchBar->setText(QStringLiteral("Pride"));
    screen.triggerSearchNow();

    QVERIFY(screen.m_model->rowCount() >= 1);
    const QString firstBookId = screen.m_model->item(0)->data(SearchRole::BookId).toString();
    screen.onAddToLibrary(firstBookId);
    QCOMPARE(screen.m_model->item(0)->data(SearchRole::InLibrary).toBool(), true);

    screen.onClearFilters();
    QCOMPARE(screen.m_resultsStack->currentIndex(), 0);
    QCOMPARE(screen.m_model->rowCount(), 0);
    QVERIFY(screen.m_searchBar->text().isEmpty());
}

void SearchScreenTest::loadMoreAndYearClamp_work()
{
    SearchScreen screen;
    screen.m_searchBar->setText(QStringLiteral("Bulk"));
    screen.triggerSearchNow();

    const int initialRows = screen.m_model->rowCount();
    QVERIFY(initialRows == 50);
    QVERIFY(!screen.m_loadMoreBtn->isHidden());

    screen.onLoadMore();
    QVERIFY(screen.m_model->rowCount() > initialRows);

    screen.m_yearTo->setValue(1800);
    screen.m_yearFrom->setValue(1900);
    QCOMPARE(screen.m_yearTo->value(), 1900);
}

QTEST_MAIN(SearchScreenTest)

#include "test_search_screen.moc"
