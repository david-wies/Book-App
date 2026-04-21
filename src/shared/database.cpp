#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace classic_books::db {

static const QString kDatabaseFileName = QStringLiteral("classic_books.db");

QString databaseFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(kDatabaseFileName);
}

bool initializeDatabase(const QString &filePath, const QString &connectionName)
{
    // Check if connection already exists
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
            isbn TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            author TEXT,
            language TEXT,
            publish_year INTEGER,
            publication_date TEXT
        ))",
        R"(CREATE TABLE IF NOT EXISTS genres (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            genre_name TEXT UNIQUE NOT NULL
        ))",
        R"(CREATE TABLE IF NOT EXISTS book_genres (
            book_isbn TEXT,
            genre_id INTEGER,
            PRIMARY KEY (book_isbn, genre_id),
            FOREIGN KEY (book_isbn) REFERENCES books(isbn) ON DELETE CASCADE,
            FOREIGN KEY (genre_id) REFERENCES genres(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS formats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_isbn TEXT NOT NULL,
            format_type TEXT NOT NULL,
            FOREIGN KEY (book_isbn) REFERENCES books(isbn) ON DELETE CASCADE
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
            book_isbn TEXT NOT NULL,
            added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            status TEXT,
            FOREIGN KEY (book_isbn) REFERENCES books(isbn) ON DELETE CASCADE
        ))"
    };

    for (const QString &sql : tables) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to create table:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }
    
    qDebug() << "Database schema created successfully.";
    return true;
}

bool insertSampleData(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList inserts = {
        R"(INSERT OR IGNORE INTO books (isbn, title, author, language, publish_year) VALUES 
            ('978-0141439518', 'Pride and Prejudice', 'Jane Austen', 'English', 1813),
            ('978-0486280615', 'The Adventures of Huckleberry Finn', 'Mark Twain', 'English', 1884),
            ('978-0140449266', 'The Count of Monte Cristo', 'Alexandre Dumas', 'English', 1844)
        )",
        R"(INSERT OR IGNORE INTO genres (id, genre_name) VALUES 
            (1, 'Romance'),
            (2, 'Classic'),
            (3, 'Adventure'),
            (4, 'Historical Fiction')
        )",
        R"(INSERT OR IGNORE INTO book_genres (book_isbn, genre_id) VALUES 
            ('978-0141439518', 1),
            ('978-0141439518', 2),
            ('978-0486280615', 3),
            ('978-0486280615', 2),
            ('978-0140449266', 3),
            ('978-0140449266', 4),
            ('978-0140449266', 2)
        )",
        R"(INSERT OR IGNORE INTO formats (id, book_isbn, format_type) VALUES 
            (1, '978-0141439518', 'epub'),
            (2, '978-0141439518', 'pdf'),
            (3, '978-0486280615', 'epub'),
            (4, '978-0140449266', 'epub')
        )",
        R"(INSERT OR IGNORE INTO sources (id, format_id, source_name, download_link) VALUES 
            (1, 1, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.epub.images'),
            (2, 2, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.pdf.images'),
            (3, 3, 'Gutenberg', 'https://www.gutenberg.org/ebooks/76.epub.images'),
            (4, 4, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1184.epub.images')
        )",
        R"(INSERT OR IGNORE INTO library_items (book_isbn, status) VALUES 
            ('978-0141439518', 'saved'),
            ('978-0486280615', 'downloaded')
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

} // namespace classic_books::db
