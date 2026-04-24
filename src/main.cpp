#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QMessageBox>
#include "shared/database.h"
#include "collector/collector_worker.h"
#include "gui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (!bookhub::db::initializeDatabase(bookhub::db::databaseFilePath())) {
        QMessageBox::critical(nullptr,
            QStringLiteral("Database error"),
            QStringLiteral("Failed to open the database. The application cannot start."));
        return 1;
    }

    if (!bookhub::db::verifySchemaVersion()) {
        return 1;
    }
    bookhub::db::createSchema();
    bookhub::db::insertSampleData();

    // Start background collector thread
    bookhub::collector::CollectorWorker collector;
    collector.start();

    const QString iconPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("book_reader_icon.jpg"));
    const QIcon appIcon(iconPath);
    app.setWindowIcon(appIcon);

    bookhub::gui::MainWindow window;
    window.setWindowIcon(appIcon);

    // NOTE: CollectorWorker does not yet emit status signals. When those are
    // added (future task), connect them here with Qt::QueuedConnection so the
    // GUI thread is never called directly from the collector thread:
    //   QObject::connect(&collector, &CollectorWorker::statusChanged,
    //                    &window, &MainWindow::onCollectorStatusChanged,
    //                    Qt::QueuedConnection);

    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &collector, [&collector]() {
        collector.requestShutdownAfterCurrentUpdate();
    });
    QObject::connect(&collector, &QThread::finished,
                     &app, [&app, &window]() {
        if (!window.isVisible())
            app.quit();
    });

    window.show();

    const int result = app.exec();

    if (collector.isRunning()) {
        collector.requestShutdownAfterCurrentUpdate();
        collector.wait();
    }

    return result;
}
