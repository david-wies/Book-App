#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace bookhub::db {

static const QString kDatabaseFileName = QStringLiteral("bookhub.db");

QString databaseFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(dir);
    return QDir(dir).filePath(kDatabaseFileName);
}

bool initializeDatabase(const QString &filePath, const QString &connectionName)
{
    if (QSqlDatabase::contains(connectionName)) {
        return true;
    }

    auto db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(filePath);

    if (!db.open()) {
        qWarning() << "Failed to open SQLite database:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec("PRAGMA foreign_keys = ON;")) {
        qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
    }

    // WAL mode allows the GUI thread to read while the collector holds a write
    // lock, preventing UI stalls during background discovery runs.
    if (!query.exec("PRAGMA journal_mode = WAL;")) {
        qWarning() << "Failed to enable WAL journal mode:" << query.lastError().text();
    }

    qDebug() << "Opened SQLite database at" << filePath << "with connection:" << connectionName;
    return true;
}

bool createSchema(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList tables = {
        R"(CREATE TABLE IF NOT EXISTS books (
            book_id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            author TEXT,
            publish_year INTEGER,
            summary TEXT
        ))",
        R"(CREATE TABLE IF NOT EXISTS book_identifiers (
            book_id TEXT NOT NULL REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            type    TEXT NOT NULL,
            value   TEXT NOT NULL,
            UNIQUE(type, value)
        ))",
        R"(CREATE TABLE IF NOT EXISTS editions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id TEXT NOT NULL,
            language TEXT NOT NULL,
            publisher TEXT,
            publish_date TEXT,
            UNIQUE (book_id, language),
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS genres (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            genre_name TEXT UNIQUE NOT NULL
        ))",
        R"(CREATE TABLE IF NOT EXISTS book_genres (
            book_id TEXT,
            genre_id INTEGER,
            PRIMARY KEY (book_id, genre_id),
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (genre_id) REFERENCES genres(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS formats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            edition_id INTEGER NOT NULL,
            format_type TEXT NOT NULL,
            UNIQUE(edition_id, format_type),
            FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS sources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            format_id INTEGER NOT NULL,
            source_name TEXT NOT NULL,
            download_link TEXT NOT NULL,
            UNIQUE(format_id, source_name),
            FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS library_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id TEXT NOT NULL UNIQUE,
            edition_id INTEGER,
            added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            status TEXT,
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (edition_id) REFERENCES editions(id)
        ))",
        R"(CREATE TABLE IF NOT EXISTS voices (
            id                   INTEGER PRIMARY KEY AUTOINCREMENT,
            name                 TEXT NOT NULL,
            type                 TEXT NOT NULL CHECK(type IN ('preset', 'custom')),
            engine               TEXT NOT NULL CHECK(engine IN ('sherpa_onnx', 'pocket_tts')),
            model_path           TEXT,
            config_path          TEXT,
            reference_audio_path TEXT,
            created_at           TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ))"
    };

    for (const QString &sql : tables) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to create table:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }

    if (!query.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        qWarning() << "Failed to set user_version:" << query.lastError().text();
        return false;
    }

    qDebug() << "Database schema created successfully.";
    return true;
}

bool verifySchemaVersion(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.exec("PRAGMA user_version");
    if (!q.next()) {
        qCritical("Failed to read PRAGMA user_version");
        return false;
    }
    int version = q.value(0).toInt();
    if (version == kSchemaVersion)
        return true;

    // version 0 covers two cases:
    // 1. Fresh DB with no tables — proceed; createSchema() sets the version next.
    // 2. Legacy pre-versioned schema with a books table present — fall through to
    //    mismatch handling below so the user is prompted to migrate.
    if (version == 0) {
        QSqlQuery tableCheck(db);
        tableCheck.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='books'");
        if (!tableCheck.next())
            return true; // no tables yet — fresh DB
    }

    // Migration: version 1 → 2
    // Adds UNIQUE(edition_id, format_type) on formats,
    // UNIQUE(format_id, source_name) on sources, and UNIQUE(book_id) on
    // library_items. SQLite does not support ADD CONSTRAINT, so we recreate
    // each table using the standard rename-insert-drop pattern.
    if (version == 1) {
        qDebug() << "Migrating database schema from version 1 to 2...";
        QStringList migration = {
            // Must be outside a transaction; disables FK checks during table recreation.
            "PRAGMA foreign_keys = OFF",
            "BEGIN",
            R"(CREATE TABLE formats_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                edition_id INTEGER NOT NULL,
                format_type TEXT NOT NULL,
                UNIQUE(edition_id, format_type),
                FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
            ))",
            "INSERT OR IGNORE INTO formats_new SELECT * FROM formats",
            "DROP TABLE formats",
            "ALTER TABLE formats_new RENAME TO formats",
            R"(CREATE TABLE sources_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                format_id INTEGER NOT NULL,
                source_name TEXT NOT NULL,
                download_link TEXT NOT NULL,
                UNIQUE(format_id, source_name),
                FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
            ))",
            "INSERT OR IGNORE INTO sources_new SELECT * FROM sources",
            "DROP TABLE sources",
            "ALTER TABLE sources_new RENAME TO sources",
            R"(CREATE TABLE library_items_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                book_id TEXT NOT NULL UNIQUE,
                edition_id INTEGER,
                added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                status TEXT,
                FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
                FOREIGN KEY (edition_id) REFERENCES editions(id)
            ))",
            "INSERT OR IGNORE INTO library_items_new SELECT * FROM library_items",
            "DROP TABLE library_items",
            "ALTER TABLE library_items_new RENAME TO library_items",
            QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion),
            "COMMIT",
            "PRAGMA foreign_keys = ON"
        };

        QSqlQuery mq(db);
        for (const QString &sql : migration) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v1→v2 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }
        qDebug() << "Migration to schema version 2 complete.";
        return true;
    }

    // Migration: version 2 → 3
    // Adds voices table for preset and custom voice storage
    if (version == 2) {
        qDebug() << "Migrating database schema from version 2 to 3...";
        QStringList migration = {
            "BEGIN",
            R"(CREATE TABLE IF NOT EXISTS voices (
                id                   INTEGER PRIMARY KEY AUTOINCREMENT,
                name                 TEXT NOT NULL,
                type                 TEXT NOT NULL CHECK(type IN ('preset', 'custom')),
                engine               TEXT NOT NULL CHECK(engine IN ('sherpa_onnx', 'pocket_tts')),
                model_path           TEXT,
                config_path          TEXT,
                reference_audio_path TEXT,
                created_at           TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            ))",
            R"(INSERT OR IGNORE INTO voices (name, type, engine) VALUES
                ('Classic Storyteller', 'preset', 'sherpa_onnx'),
                ('Warm Listener',       'preset', 'sherpa_onnx'),
                ('Crisp Narrator',      'preset', 'sherpa_onnx')
            )",
            QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion),
            "COMMIT"
        };

        QSqlQuery mq(db);
        for (const QString &sql : migration) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v2→v3 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                return false;
            }
        }
        qDebug() << "Migration to schema version 3 complete.";
        return true;
    }

    qCritical("Database schema version mismatch: expected %d, found %d. "
              "Run tools/migrate_db.py to upgrade the database.",
              kSchemaVersion, version);
    return false;
}

bool insertSampleData(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList inserts = {
        R"(INSERT OR IGNORE INTO books (book_id, title, author, publish_year) VALUES
            ('lccn:n78095332', 'Pride and Prejudice', 'Jane Austen', 1813),
            ('lccn:n79025140', 'The Adventures of Huckleberry Finn', 'Mark Twain', 1884),
            ('gutenberg:1184', 'The Count of Monte Cristo', 'Alexandre Dumas', 1844)
        )",
        R"(INSERT OR IGNORE INTO book_identifiers (book_id, type, value) VALUES
            ('lccn:n78095332', 'lccn', 'n78095332'),
            ('lccn:n78095332', 'gutenberg', '1342'),
            ('lccn:n79025140', 'lccn', 'n79025140'),
            ('lccn:n79025140', 'gutenberg', '76'),
            ('gutenberg:1184', 'gutenberg', '1184')
        )",
        R"(INSERT OR IGNORE INTO editions (id, book_id, language) VALUES
            (1, 'lccn:n78095332', 'English'),
            (2, 'lccn:n78095332', 'French'),
            (3, 'lccn:n79025140', 'English'),
            (4, 'gutenberg:1184', 'English'),
            (5, 'gutenberg:1184', 'French')
        )",
        R"(INSERT OR IGNORE INTO genres (id, genre_name) VALUES
            (1, 'Romance'),
            (2, 'Classic'),
            (3, 'Adventure'),
            (4, 'Historical Fiction')
        )",
        R"(INSERT OR IGNORE INTO book_genres (book_id, genre_id) VALUES
            ('lccn:n78095332', 1), ('lccn:n78095332', 2),
            ('lccn:n79025140', 3), ('lccn:n79025140', 2),
            ('gutenberg:1184', 3), ('gutenberg:1184', 4), ('gutenberg:1184', 2)
        )",
        R"(INSERT OR IGNORE INTO formats (id, edition_id, format_type) VALUES
            (1, 1, 'epub_1'), (2, 1, 'pdf_1'), (3, 3, 'epub_1'), (4, 4, 'epub_1')
        )",
        R"(INSERT OR IGNORE INTO sources (id, format_id, source_name, download_link) VALUES
            (1, 1, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.epub.images'),
            (2, 2, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.pdf.images'),
            (3, 3, 'Gutenberg', 'https://www.gutenberg.org/ebooks/76.epub.images'),
            (4, 4, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1184.epub.images')
        )",
        R"(INSERT OR IGNORE INTO library_items (book_id, edition_id, status) VALUES
            ('lccn:n78095332', 1, 'saved'),
            ('lccn:n79025140', 3, 'downloaded')
        )",
        R"(INSERT OR IGNORE INTO voices (name, type, engine) VALUES
            ('Classic Storyteller', 'preset', 'sherpa_onnx'),
            ('Warm Listener',       'preset', 'sherpa_onnx'),
            ('Crisp Narrator',      'preset', 'sherpa_onnx')
        )"
    };

    for (const QString &sql : inserts) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to insert sample data:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }

    qDebug() << "Sample data inserted successfully.";
    return true;
}

} // namespace bookhub::db
