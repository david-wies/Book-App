#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>

#define private public
#include "gui/screens/explore_screen.h"
#undef private

#include "support/test_database_utils.h"

#include <QtTest>
#include <memory>

using namespace bookhub::gui;

class ExploreScreenTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void categoryClick_updatesBreadcrumbAndSupportsBack();
    void loadMoreAndBookClick_emitExpectedSignals();

private:
    std::unique_ptr<bookhub::tests::TestDatabase> m_db;
};

void ExploreScreenTest::init()
{
    m_db = std::make_unique<bookhub::tests::TestDatabase>();
    QVERIFY(m_db->open());
    QVERIFY(m_db->createSchema());

    QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
        "INSERT INTO genres (id, genre_name) VALUES (1, 'Epic')")));

    for (int i = 0; i < 45; ++i) {
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO books (book_id, title, author) VALUES ('epic:%1', 'Epic Book %1', 'Author %1')").arg(i)));
        QVERIFY(bookhub::tests::execSql(m_db->database(), QStringLiteral(
            "INSERT INTO book_genres (book_id, genre_id) VALUES ('epic:%1', 1)").arg(i)));
    }
}

void ExploreScreenTest::cleanup()
{
    m_db.reset();
}

void ExploreScreenTest::categoryClick_updatesBreadcrumbAndSupportsBack()
{
    ExploreScreen screen;
    screen.onCategoryClicked(QStringLiteral("Epic"));

    QCOMPARE(screen.m_exploreStack->currentIndex(), 1);
    QCOMPARE(screen.m_breadcrumb->text(), QStringLiteral("Epic"));

    auto buttons = screen.findChildren<QPushButton *>();
    auto it = std::find_if(buttons.begin(), buttons.end(), [](QPushButton *button) {
        return button->text().contains(QStringLiteral("Back"));
    });
    QVERIFY(it != buttons.end());
    (*it)->click();
    QCOMPARE(screen.m_exploreStack->currentIndex(), 0);
}

void ExploreScreenTest::loadMoreAndBookClick_emitExpectedSignals()
{
    ExploreScreen screen;
    QSignalSpy spy(&screen, &ExploreScreen::bookDetailsRequested);

    screen.onCategoryClicked(QStringLiteral("Epic"));
    QVERIFY(!screen.m_loadMoreBtn->isHidden());
    const int initialItemCount = screen.m_genreGridLayout->count();
    QVERIFY(initialItemCount >= 40);

    screen.onLoadMore();
    QVERIFY(screen.m_genreGridLayout->count() > initialItemCount);

    QWidget *firstCard = screen.m_genreGridLayout->itemAt(0)->widget();
    QVERIFY(firstCard);
    QTest::mouseClick(firstCard, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(ExploreScreenTest)

#include "test_explore_screen.moc"
