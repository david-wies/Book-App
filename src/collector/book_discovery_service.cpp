#include "book_discovery_service.h"
#include "shared/database.h"
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
    m_activeFetches += static_cast<int>(m_adapters.size());
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

    const QString sourceName = adapter->sourceName();
    qDebug() << "BookDiscoveryService:" << sourceName << "discovered" << books.size() << "books.";

    if (books.isEmpty())
        return;

    QSqlDatabase db = QSqlDatabase::database(m_dbConnectionName);
    if (!db.isOpen()) {
        qWarning() << "BookDiscoveryService: Database connection"
                   << m_dbConnectionName << "is not open.";
        return;
    }

    // Wrap the whole batch in one transaction: 500 individual transactions per
    // batch caused SQLITE_BUSY contention with GUI writes once the WAL grew
    // large.  Per-book SAVEPOINTs preserve the old failure isolation — a single
    // malformed book rolls back its own rows without losing the rest of the batch.
    if (!db.transaction()) {
        qWarning() << "BookDiscoveryService: Failed to start batch transaction:"
                   << db.lastError().text() << "— falling back to per-book transactions.";
        for (const auto& book : books)
            insertBookIntoDatabase(book, sourceName);
        return;
    }

    int committed = 0;
    int failed = 0;
    for (const auto& book : books) {
        QSqlQuery sp(db);
        if (!sp.exec(QStringLiteral("SAVEPOINT book_sp"))) {
            qWarning() << "BookDiscoveryService: SAVEPOINT failed:" << sp.lastError().text();
            ++failed;
            continue;
        }
        if (insertBookRows(book, sourceName, db)) {
            if (!sp.exec(QStringLiteral("RELEASE book_sp"))) {
                qWarning() << "BookDiscoveryService: RELEASE failed:" << sp.lastError().text();
            }
            ++committed;
        } else {
            // insertBookRows already logged the specific failure.  Roll back this
            // book's rows; subsequent books in the batch continue.
            sp.exec(QStringLiteral("ROLLBACK TO book_sp"));
            sp.exec(QStringLiteral("RELEASE book_sp"));
            ++failed;
        }
    }

    if (!db.commit()) {
        qWarning() << "BookDiscoveryService: Batch commit failed:" << db.lastError().text();
        db.rollback();
        return;
    }

    qDebug() << "BookDiscoveryService: Committed" << committed << "books"
             << "(failed:" << failed << ") from" << sourceName;
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
    const qsizetype colon = bookId.indexOf(':');
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

    if (!insertBookRows(book, sourceName, db)) {
        db.rollback();
        return;
    }

    if (!db.commit()) {
        qWarning() << "BookDiscoveryService: Failed to commit transaction:" << db.lastError().text();
        db.rollback();
    }
}

bool BookDiscoveryService::insertBookRows(const DiscoveredBook& book, const QString& sourceName,
                                          QSqlDatabase& db) {
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
                    return false;
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
        return false;
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
    const QString primaryLang = book.languages.isEmpty()
        ? QStringLiteral("Unknown")
        : bookhub::db::languageNameForCode(book.languages.first());

    query.prepare("INSERT OR IGNORE INTO editions (book_id, language) VALUES (?, ?)");
    query.addBindValue(effectiveId);
    query.addBindValue(primaryLang);
    if (!query.exec()) {
        qWarning() << "Failed to insert edition:" << query.lastError().text();
        return false;
    }

    query.prepare("SELECT id FROM editions WHERE book_id = ? AND language = ?");
    query.addBindValue(effectiveId);
    query.addBindValue(primaryLang);
    if (!query.exec() || !query.next()) {
        qWarning() << "Failed to retrieve edition id:" << query.lastError().text();
        return false;
    }
    const int editionId = query.value(0).toInt();

    for (auto it = book.formats.constBegin(); it != book.formats.constEnd(); ++it) {
        const QString formatType = bookhub::db::stripFormatTypeSuffix(it.key());
        query.prepare("INSERT OR IGNORE INTO formats (edition_id, format_type) VALUES (?, ?)");
        query.addBindValue(editionId);
        query.addBindValue(formatType);
        if (!query.exec()) {
            qWarning() << "Failed to insert format:" << query.lastError().text();
            continue;
        }

        // Retrieve the format id whether the row was just inserted or already existed.
        QSqlQuery fmtQuery(db);
        fmtQuery.prepare("SELECT id FROM formats WHERE edition_id = ? AND format_type = ?");
        fmtQuery.addBindValue(editionId);
        fmtQuery.addBindValue(formatType);
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

    return true;
}

} // namespace bookhub::collector
