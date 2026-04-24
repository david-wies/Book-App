#include <QMainWindow>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QLabel>

#define private public
#include "gui/main_window.h"
#undef private

#include "gui/style_tokens.h"
#include "support/test_database_utils.h"

#include <QPushButton>
#include <QtTest>

using namespace bookhub::gui;

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void window_initializesWithExpectedDefaults();
    void navigation_buttonsSwitchPages();
    void collectorSlots_updateStatusUi();
};

void MainWindowTest::initTestCase()
{
    bookhub::tests::isolateSettings(QStringLiteral("bookhub-test-main-window"));
}

void MainWindowTest::window_initializesWithExpectedDefaults()
{
    MainWindow window;
    QCOMPARE(window.minimumWidth(), WindowMinWidth);
    QCOMPARE(window.minimumHeight(), WindowMinHeight);
    QCOMPARE(window.m_stack->currentIndex(), 0);
}

void MainWindowTest::navigation_buttonsSwitchPages()
{
    MainWindow window;
    auto *searchButton = qobject_cast<QPushButton *>(window.m_navGroup->button(1));
    auto *exploreButton = qobject_cast<QPushButton *>(window.m_navGroup->button(2));
    QVERIFY(searchButton);
    QVERIFY(exploreButton);

    searchButton->click();
    QCOMPARE(window.m_stack->currentIndex(), 1);
    QVERIFY(searchButton->isChecked());

    exploreButton->click();
    QCOMPARE(window.m_stack->currentIndex(), 2);
    QVERIFY(exploreButton->isChecked());
    QVERIFY(!searchButton->isChecked());
}

void MainWindowTest::collectorSlots_updateStatusUi()
{
    MainWindow window;
    window.onCollectorStatusChanged(QStringLiteral("Collector: running"));
    QCOMPARE(window.m_statusLabel->text(), QStringLiteral("Collector: running"));
    QVERIFY(window.m_statusDot->styleSheet().contains(QStringLiteral("#d97706"), Qt::CaseInsensitive));

    window.onCollectorErrorOccurred(QStringLiteral("network failed"));
    QCOMPARE(window.m_statusLabel->text(), QStringLiteral("Collector error: network failed"));
    QVERIFY(window.m_statusDot->styleSheet().contains(QStringLiteral("#dc2626"), Qt::CaseInsensitive));
}

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
