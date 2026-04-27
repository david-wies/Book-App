#include <QWidget>
#include <QListView>
#include <QComboBox>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QStandardItemModel>

#include "gui/screens/library_screen.h"

#include "gui/widgets/book_card_delegate.h"
#include "gui/widgets/empty_state_widget.h"
#include "support/test_database_utils.h"
#include "support/test_query_worker.h"

#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest>
#include <memory>

using namespace bookhub::gui;

class LibraryScreenTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void emptyState_emitsExploreRequested();
    void populatedState_supportsSortingAndDetails();
    void viewMode_persistsAndRemoveActionDeletesRow();

private:
    std::unique_ptr<bookhub::tests::TestDatabase> m_db;
    TestQueryWorker *m_worker{};
};

void LibraryScreenTest::init()
{
    bookhub::tests::isolateSettings(QStringLiteral("bookhub-test-library-screen"));
    // Match the org/app name set by main.cpp so QSettings() in production code
    // resolves to the same path as QSettings("BookHub","BookHub") in the test.
    QCoreApplication::setOrganizationName(QStringLiteral("BookHub"));
    QCoreApplication::setApplicationName(QStringLiteral("BookHub"));
    QSettings settings(QStringLiteral("BookHub"), QStringLiteral("BookHub"));
    settings.clear();

    m_db = std::make_unique<bookhub::tests::TestDatabase>();
    QVERIFY(m_db->open());
    QVERIFY(m_db->createSchema());

    m_worker = new TestQueryWorker();
}

void LibraryScreenTest::cleanup()
{
    delete m_worker;
    m_worker = nullptr;
    m_db.reset();
}

void LibraryScreenTest::emptyState_emitsExploreRequested()
{
    LibraryScreen screen(m_worker);
    QSignalSpy spy(&screen, &LibraryScreen::exploreRequested);

    QCOMPARE(screen.m_stack->currentIndex(), 1);
    QVERIFY(screen.m_emptyState);
    emit screen.m_emptyState->ctaClicked();
    QCOMPARE(spy.count(), 1);
}

void LibraryScreenTest::populatedState_supportsSortingAndDetails()
{
    QVERIFY(m_db->insertSampleData());

    LibraryScreen screen(m_worker);
    QSignalSpy detailsSpy(&screen, &LibraryScreen::bookDetailsRequested);

    QCOMPARE(screen.m_stack->currentIndex(), 0);
    QVERIFY(screen.m_model->rowCount() >= 2);

    screen.m_sortCombo->setCurrentIndex(1);
    QCOMPARE(screen.m_model->item(0)->data(LibraryRole::Title).toString(),
             QStringLiteral("Pride and Prejudice"));

    const QModelIndex firstIndex = screen.m_model->index(0, 0);
    emit screen.m_listView->doubleClicked(firstIndex);
    QCOMPARE(detailsSpy.count(), 1);
}

void LibraryScreenTest::viewMode_persistsAndRemoveActionDeletesRow()
{
    QVERIFY(m_db->insertSampleData());

    LibraryScreen screen(m_worker);
    screen.show();
    auto *gridButton = qobject_cast<QPushButton *>(screen.m_viewGroup->button(1));
    QVERIFY(gridButton);
    gridButton->click();

    QSettings settings(QStringLiteral("BookHub"), QStringLiteral("BookHub"));
    QCOMPARE(settings.value(QStringLiteral("Library/viewMode")).toInt(), 1);
    QCOMPARE(screen.m_listView->viewMode(), QListView::IconMode);

    const int initialRows = screen.m_model->rowCount();
    const int libraryItemId = screen.m_model->item(0)->data(LibraryRole::LibraryItemId).toInt();

    QTimer::singleShot(0, &screen, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (auto *btn = box->button(QMessageBox::Yes))
                btn->click();
    });
    screen.onRemoveRequested(libraryItemId, screen.m_model->item(0)->data(LibraryRole::BookId).toString());
    QCOMPARE(m_db->scalarInt(QStringLiteral(
        "SELECT COUNT(*) FROM library_items WHERE id = %1").arg(libraryItemId)), 0);
    QCOMPARE(screen.m_model->rowCount(), initialRows - 1);
}

QTEST_MAIN(LibraryScreenTest)

#include "test_library_screen.moc"
