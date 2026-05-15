#include <QApplication>
#include <QDateTime>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include "shared/database.h"
#include "collector/collector_worker.h"
#include "gui/main_window.h"

namespace {
// One-time migration: pre-v8 builds stored the Gutenberg conditional-GET cache
// timestamp in QSettings ("gutenberg_last_modified").  v8 moved that into the
// sync_state table so freshness is coupled to the DB it describes.  Copy the
// legacy value into a 'completed' sync_state row, then clear the QSettings key
// so no future code path reads the now-stale fallback.
void migrateLegacyGutenbergCache()
{
    QSettings settings;
    const QString legacyValue =
        settings.value(QStringLiteral("gutenberg_last_modified")).toString();
    if (legacyValue.isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM sync_state WHERE adapter_id = 'gutenberg'"));
    if (q.exec() && q.next()) {
        // sync_state already has a row — don't clobber, but do remove the legacy key.
        settings.remove(QStringLiteral("gutenberg_last_modified"));
        return;
    }

    q.prepare(QStringLiteral(
        "INSERT INTO sync_state (adapter_id, status, last_modified, completed_at, "
        "books_processed, bytes_downloaded, bytes_total) "
        "VALUES ('gutenberg', 'completed', ?, ?, 0, 0, 0)"));
    q.addBindValue(legacyValue);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (q.exec()) {
        settings.remove(QStringLiteral("gutenberg_last_modified"));
        qDebug() << "Migrated legacy gutenberg_last_modified into sync_state.";
    } else {
        qWarning() << "Failed to migrate legacy gutenberg_last_modified:"
                   << q.lastError().text();
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("BookHub"));
    app.setApplicationName(QStringLiteral("BookHub"));

    if (!bookhub::db::initializeDatabase(bookhub::db::databaseFilePath())) {
        QMessageBox::critical(nullptr,
            QStringLiteral("Database error"),
            QStringLiteral("Failed to open the database. The application cannot start."));
        return 1;
    }

    if (!bookhub::db::verifySchemaVersion()) {
        QMessageBox::critical(nullptr,
            QStringLiteral("Database error"),
            QStringLiteral("Schema version mismatch. Run tools/migrate_db.py to upgrade.\n"
                           "The application cannot start."));
        return 1;
    }
    if (!bookhub::db::createSchema()) {
        QMessageBox::critical(nullptr,
            QStringLiteral("Database error"),
            QStringLiteral("Failed to create schema. The application cannot start."));
        return 1;
    }
#ifdef QT_DEBUG
    bookhub::db::insertSampleData();
#endif

    // Must run after createSchema() (sync_state table exists) and before the
    // collector starts so the adapter's first fetchBooks() call sees the row.
    migrateLegacyGutenbergCache();

    // Start background collector thread
    bookhub::collector::CollectorWorker collector;
    collector.start();

    const QIcon appIcon(QStringLiteral(":/book_reader_icon.jpg"));
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
