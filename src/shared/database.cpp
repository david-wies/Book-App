#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace bookhub::db {

static const QString kDatabaseFileName = QStringLiteral("bookhub.db");

QString databaseFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(kDatabaseFileName);
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
            FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS sources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            format_id INTEGER NOT NULL,
            source_name TEXT NOT NULL,
            download_link TEXT NOT NULL,
            FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS library_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id TEXT NOT NULL,
            edition_id INTEGER,
            added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            status TEXT,
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (edition_id) REFERENCES editions(id)
        ))"
    };

    for (const QString &sql : tables) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to create table:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }

    if (!query.exec("PRAGMA user_version = 1")) {
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

    // user_version=0 on a file that has never been initialised is a fresh DB —
    // createSchema() will set it to kSchemaVersion immediately after this call.
    if (version == 0) {
        QSqlQuery tableCheck(db);
        tableCheck.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='books'");
        if (!tableCheck.next())
            return true; // no tables yet — fresh DB
    }

    fprintf(stderr,
        "Database schema version mismatch: expected %d, found %d.\n"
        "Run tools/migrate_db.py to upgrade the database.\n",
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
            (1, 1, 'epub'), (2, 1, 'pdf'), (3, 3, 'epub'), (4, 4, 'epub')
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
