#include "book_discovery_service.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace bookhub::collector {

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
    if (m_activeFetches > 0) {
        qDebug() << "BookDiscoveryService: Discovery already in progress; skipping overlapping run.";
        return;
    }

    qDebug() << "BookDiscoveryService: Starting discovery across" << m_adapters.size() << "adapters.";
    if (m_adapters.isEmpty()) {
        return;
    }

    const bool wasIdle = (m_activeFetches == 0);
    m_activeFetches += m_adapters.size();
    if (wasIdle) {
        emit updateStarted();
    }

    for (auto adapter : m_adapters) {
        adapter->fetchBooks();
    }
}

void BookDiscoveryService::onBooksDiscovered(const QList<bookhub::collector::DiscoveredBook>& books) {
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

    if (m_activeFetches > 0) {
        --m_activeFetches;
    }

    if (m_activeFetches == 0) {
        emit updateFinished();
    }
}

static int idPriority(const QString& bookId)
{
    const int colon = bookId.indexOf(':');
    const QString prefix = (colon >= 0) ? bookId.left(colon) : bookId;
    if (prefix == "lccn")  return 1;
    if (prefix == "oclc")  return 2;
    if (prefix == "isbn")  return 3;
    return 4;
}

void BookDiscoveryService::insertBookIntoDatabase(const DiscoveredBook& book, const QString& sourceName) {
    QSqlDatabase db = QSqlDatabase::database(m_dbConnectionName);
    if (!db.isOpen()) {
        qWarning() << "BookDiscoveryService: Database connection" << m_dbConnectionName << "is not open.";
        return;
    }

    if (!db.transaction()) {
        qWarning() << "BookDiscoveryService: Failed to start transaction:" << db.lastError().text();
        return;
    }

    QSqlQuery query(db);
    QString effectiveId = book.resolvedId;

    // Phase 1 — deduplicate: find an existing book that shares any of the incoming identifiers
    if (!book.identifiers.isEmpty()) {
        QStringList clauses(book.identifiers.size(), QStringLiteral("(type = ? AND value = ?)"));
        QString sql = "SELECT book_id FROM book_identifiers WHERE " + clauses.join(" OR ") + " LIMIT 1";
        query.prepare(sql);
        for (const auto& id : book.identifiers) {
            query.addBindValue(id.type);
            query.addBindValue(id.value);
        }
        if (query.exec() && query.next()) {
            effectiveId = query.value(0).toString();

            // Phase 1b — promote to a higher-priority ID if the incoming one outranks the stored one
            if (idPriority(book.resolvedId) < idPriority(effectiveId)) {
                query.prepare("UPDATE books SET book_id = ? WHERE book_id = ?");
                query.addBindValue(book.resolvedId);
                query.addBindValue(effectiveId);
                if (!query.exec()) {
                    qWarning() << "Failed to promote book_id:" << query.lastError().text();
                    db.rollback();
                    return;
                }
                effectiveId = book.resolvedId;
            }
        }
    }

    // Phase 2 — insert the book row (no-op if it already exists)
    query.prepare("INSERT OR IGNORE INTO books (book_id, title, author, publish_year, summary) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(effectiveId);
    query.addBindValue(book.title);
    query.addBindValue(book.authors.join(", "));
    query.addBindValue(QVariant());   // publish_year not available from RDF
    query.addBindValue(QVariant());   // summary not available from RDF
    if (!query.exec()) {
        qWarning() << "Failed to insert book:" << query.lastError().text();
        db.rollback();
        return;
    }

    // Phase 3 — register all known identifiers for cross-source deduplication
    for (const auto& id : book.identifiers) {
        query.prepare("INSERT OR IGNORE INTO book_identifiers (book_id, type, value) VALUES (?, ?, ?)");
        query.addBindValue(effectiveId);
        query.addBindValue(id.type);
        query.addBindValue(id.value);
        if (!query.exec()) {
            qWarning() << "Failed to insert book identifier:" << query.lastError().text();
        }
    }

    // Phase 4 — edition, formats, and sources
    const QString primaryLang = book.languages.isEmpty() ? "Unknown" : book.languages.first();

    query.prepare("INSERT OR IGNORE INTO editions (book_id, language) VALUES (?, ?)");
    query.addBindValue(effectiveId);
    query.addBindValue(primaryLang);
    if (!query.exec()) {
        qWarning() << "Failed to insert edition:" << query.lastError().text();
        db.rollback();
        return;
    }

    query.prepare("SELECT id FROM editions WHERE book_id = ? AND language = ?");
    query.addBindValue(effectiveId);
    query.addBindValue(primaryLang);
    if (!query.exec() || !query.next()) {
        qWarning() << "Failed to retrieve edition id:" << query.lastError().text();
        db.rollback();
        return;
    }
    const int editionId = query.value(0).toInt();

    for (auto it = book.formats.constBegin(); it != book.formats.constEnd(); ++it) {
        query.prepare("INSERT OR IGNORE INTO formats (edition_id, format_type) VALUES (?, ?)");
        query.addBindValue(editionId);
        query.addBindValue(it.key());
        if (!query.exec()) {
            qWarning() << "Failed to insert format:" << query.lastError().text();
            continue;
        }

        // Retrieve the format id whether the row was just inserted or already existed.
        QSqlQuery fmtQuery(db);
        fmtQuery.prepare("SELECT id FROM formats WHERE edition_id = ? AND format_type = ?");
        fmtQuery.addBindValue(editionId);
        fmtQuery.addBindValue(it.key());
        if (!fmtQuery.exec() || !fmtQuery.next()) {
            qWarning() << "Failed to retrieve format id:" << fmtQuery.lastError().text();
            continue;
        }
        const int formatId = fmtQuery.value(0).toInt();

        QSqlQuery sourceQuery(db);
        sourceQuery.prepare("INSERT OR IGNORE INTO sources (format_id, source_name, download_link) VALUES (?, ?, ?)");
        sourceQuery.addBindValue(formatId);
        sourceQuery.addBindValue(sourceName);
        sourceQuery.addBindValue(it.value());
        if (!sourceQuery.exec()) {
            qWarning() << "Failed to insert source:" << sourceQuery.lastError().text();
        }
    }

    // Phase 5 — genres
    // Genre inserts are best-effort within the loop: a single genre failure is
    // logged and skipped rather than rolling back the whole transaction.
    for (const QString &subject : book.subjects) {
        QSqlQuery gq(db);
        gq.prepare(QStringLiteral("INSERT OR IGNORE INTO genres (genre_name) VALUES (?)"));
        gq.addBindValue(subject);
        if (!gq.exec())
            qWarning() << "BookDiscoveryService: Failed to insert genre:" << gq.lastError().text();

        QSqlQuery bgq(db);
        bgq.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO book_genres (book_id, genre_id) "
            "SELECT ?, id FROM genres WHERE genre_name = ?"));
        bgq.addBindValue(effectiveId);
        bgq.addBindValue(subject);
        if (!bgq.exec())
            qWarning() << "BookDiscoveryService: Failed to insert book_genre:" << bgq.lastError().text();
    }

    if (!db.commit()) {
        qWarning() << "BookDiscoveryService: Failed to commit transaction:" << db.lastError().text();
        db.rollback();
    }
}

} // namespace bookhub::collector
