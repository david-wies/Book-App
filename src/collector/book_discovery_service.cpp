#include "book_discovery_service.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QUuid>

namespace classic_books::collector {

BookDiscoveryService::BookDiscoveryService(const QString& dbConnectionName, QObject* parent)
    : QObject(parent), m_dbConnectionName(dbConnectionName) {}

void BookDiscoveryService::addAdapter(ISourceAdapter* adapter) {
    if (adapter) {
        adapter->setParent(this);
        connect(adapter, &ISourceAdapter::booksDiscovered, this, &BookDiscoveryService::onBooksDiscovered);
        connect(adapter, &ISourceAdapter::fetchCompleted, this, &BookDiscoveryService::onFetchCompleted);
        m_adapters.append(adapter);
    }
}

void BookDiscoveryService::startDiscovery() {
    qDebug() << "BookDiscoveryService: Starting discovery across" << m_adapters.size() << "adapters.";
    for (auto adapter : m_adapters) {
        adapter->fetchBooks();
    }
}

void BookDiscoveryService::onBooksDiscovered(const QList<classic_books::collector::DiscoveredBook>& books) {
    auto adapter = qobject_cast<ISourceAdapter*>(sender());
    if (!adapter) return;
    
    QString sourceName = adapter->sourceName();
    qDebug() << "BookDiscoveryService:" << sourceName << "discovered" << books.size() << "books.";
    
    for (const auto& book : books) {
        insertBookIntoDatabase(book, sourceName);
    }
    
    qDebug() << "BookDiscoveryService: Finished updating the database for" << books.size() << "books from" << sourceName;
}

void BookDiscoveryService::onFetchCompleted(bool success, const QString& errorMessage) {
    auto adapter = qobject_cast<ISourceAdapter*>(sender());
    QString sourceName = adapter ? adapter->sourceName() : "Unknown";
    
    if (success) {
        qDebug() << "BookDiscoveryService:" << sourceName << "fetch completed successfully.";
    } else {
        qWarning() << "BookDiscoveryService:" << sourceName << "fetch failed:" << errorMessage;
    }
}

void BookDiscoveryService::insertBookIntoDatabase(const DiscoveredBook& book, const QString& sourceName) {
    QSqlDatabase db = QSqlDatabase::database(m_dbConnectionName);
    if (!db.isOpen()) {
        qWarning() << "BookDiscoveryService: Database connection" << m_dbConnectionName << "is not open.";
        return;
    }
    
    QSqlQuery query(db);
    
    // Gutenberg IDs are typically numbers, use as ISBN for MVP (prefix with source)
    QString pseudoIsbn = sourceName.toLower() + "_" + book.sourceId;
    
    // Authors string
    QString authorsStr = book.authors.join(", ");
    QString primaryLang = book.languages.isEmpty() ? "Unknown" : book.languages.first();

    // Insert Book
    query.prepare("INSERT OR IGNORE INTO books (isbn, title, author, language) VALUES (?, ?, ?, ?)");
    query.addBindValue(pseudoIsbn);
    query.addBindValue(book.title);
    query.addBindValue(authorsStr);
    query.addBindValue(primaryLang);
    
    if (!query.exec()) {
        qWarning() << "Failed to insert book:" << query.lastError().text();
        return;
    }
    
    // Note: To keep things simple for MVP, we just map formats and insert.
    // In a production app, we would query the book_isbn first to verify we are adding formats.
    
    // Insert Formats
    for (auto it = book.formats.constBegin(); it != book.formats.constEnd(); ++it) {
        query.prepare("INSERT INTO formats (book_isbn, format_type) VALUES (?, ?)");
        query.addBindValue(pseudoIsbn);
        query.addBindValue(it.key());
        
        if (query.exec()) {
            int formatId = query.lastInsertId().toInt();
            
            // Insert Source for format
            QSqlQuery sourceQuery(db);
            sourceQuery.prepare("INSERT INTO sources (format_id, source_name, download_link) VALUES (?, ?, ?)");
            sourceQuery.addBindValue(formatId);
            sourceQuery.addBindValue(sourceName);
            sourceQuery.addBindValue(it.value());
            if (!sourceQuery.exec()) {
                qWarning() << "Failed to insert source:" << sourceQuery.lastError().text();
            }
        }
    }
}

} // namespace classic_books::collector
