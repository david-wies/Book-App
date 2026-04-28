#include "library_service.h"
#include "../query_worker.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace bookhub::gui {

LibraryService::LibraryService(QObject *parent)
    : QObject(parent)
{}

void LibraryService::connectToWorker(QueryWorker *worker)
{
    connect(this, &LibraryService::fetchItemsRequested,
            worker, &QueryWorker::handleFetchItemsRequest);
    connect(this, &LibraryService::addBookRequested,
            worker, &QueryWorker::handleAddBookRequest);
    connect(this, &LibraryService::removeBookRequested,
            worker, &QueryWorker::handleRemoveBookRequest);
    connect(this, &LibraryService::removeBookByBookIdRequested,
            worker, &QueryWorker::handleRemoveBookByBookIdRequest);
    connect(this, &LibraryService::updateStatusRequested,
            worker, &QueryWorker::handleUpdateStatusRequest);

    connect(worker, &QueryWorker::fetchItemsCompleted, this,
            [this](quint64 id, QList<LibraryItem> items) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit fetchItemsCompleted(id, items);
            });
    connect(worker, &QueryWorker::addBookCompleted, this,
            [this](quint64 id, QString bookId, bool success, int newId) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit addBookCompleted(id, bookId, success, newId);
                if (success)
                    emit libraryChanged();
            });
    connect(worker, &QueryWorker::removeBookCompleted, this,
            [this](quint64 id, QString bookId, bool success) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit removeBookCompleted(id, bookId, success);
                if (success)
                    emit libraryChanged();
            });
    connect(worker, &QueryWorker::removeBookByBookIdCompleted, this,
            [this](quint64 id, QString bookId, bool success) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit removeBookByBookIdCompleted(id, bookId, success);
                if (success)
                    emit libraryChanged();
            });
    connect(worker, &QueryWorker::updateStatusCompleted, this,
            [this](quint64 id, bool success) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit updateStatusCompleted(id, success);
                if (success)
                    emit libraryChanged();
            });
}

bool LibraryService::isBusy() const
{
    return m_pendingCount > 0;
}

quint64 LibraryService::requestFetchItems(const QString &sortColumn)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit fetchItemsRequested(id, sortColumn);
    return id;
}

quint64 LibraryService::requestAddBook(const QString &bookId, int editionId)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit addBookRequested(id, bookId, editionId);
    return id;
}

quint64 LibraryService::requestRemoveBook(int libraryItemId, const QString &bookId)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit removeBookRequested(id, libraryItemId, bookId);
    return id;
}

quint64 LibraryService::requestRemoveBookByBookId(const QString &bookId)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit removeBookByBookIdRequested(id, bookId);
    return id;
}

quint64 LibraryService::requestUpdateStatus(int libraryItemId, const QString &status)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit updateStatusRequested(id, libraryItemId, status);
    return id;
}

// ---------------------------------------------------------------------------
// Internal free functions
// ---------------------------------------------------------------------------

namespace internal {

QList<LibraryItem> fetchItems(const QString &sortColumn, const QString &connectionName)
{
    QString orderBy;
    if (sortColumn == QLatin1String("title")) {
        orderBy = QStringLiteral("b.title ASC");
    } else if (sortColumn == QLatin1String("author")) {
        orderBy = QStringLiteral("b.author ASC");
    } else if (sortColumn == QLatin1String("status")) {
        orderBy = QStringLiteral("li.status ASC");
    } else {
        orderBy = QStringLiteral("li.added_date DESC");
    }

    const QString sql = QStringLiteral(R"(
        SELECT
            li.id,
            li.book_id,
            COALESCE(li.edition_id, 0)  AS edition_id,
            b.title,
            b.author,
            b.publish_year,
            COALESCE(e.language, '')    AS language,
            COALESCE(MAX(s.source_name), '') AS source_name,
            COALESCE(li.status, 'saved') AS status,
            li.added_date
        FROM library_items li
        JOIN books b ON b.book_id = li.book_id
        LEFT JOIN editions e ON e.id = li.edition_id
        LEFT JOIN formats  f ON f.edition_id = e.id
        LEFT JOIN sources  s ON s.format_id = f.id
        GROUP BY li.id
        ORDER BY %1
    )").arg(orderBy);

    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(sql)) {
        qWarning() << "internal::fetchItems failed:" << query.lastError().text();
        return {};
    }

    QList<LibraryItem> items;
    while (query.next()) {
        LibraryItem item;
        item.id          = query.value(0).toInt();
        item.bookId      = query.value(1).toString();
        item.editionId   = query.value(2).toInt();
        item.title       = query.value(3).toString();
        item.author      = query.value(4).toString();
        item.publishYear = query.value(5).toInt();
        item.language    = query.value(6).toString();
        item.sourceName  = query.value(7).toString();
        item.status      = query.value(8).toString();
        item.addedDate   = query.value(9).toDateTime();
        items.append(item);
    }
    return items;
}

int addBook(const QString &bookId, int editionId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO library_items (book_id, edition_id, status) VALUES (?, ?, 'saved')"
    ));
    query.addBindValue(bookId);
    if (editionId > 0)
        query.addBindValue(editionId);
    else
        query.addBindValue(QVariant(QMetaType::fromType<int>()));

    if (!query.exec()) {
        qWarning() << "internal::addBook failed:" << query.lastError().text();
        return -1;
    }

    if (query.numRowsAffected() <= 0)
        return -1;

    return query.lastInsertId().toInt();
}

bool removeBook(int libraryItemId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral("DELETE FROM library_items WHERE id = ?"));
    query.addBindValue(libraryItemId);

    if (!query.exec()) {
        qWarning() << "internal::removeBook failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool removeBookByBookId(const QString &bookId, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral("DELETE FROM library_items WHERE book_id = ?"));
    query.addBindValue(bookId);

    if (!query.exec()) {
        qWarning() << "internal::removeBookByBookId failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool updateStatus(int libraryItemId, const QString &status, const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(QStringLiteral("UPDATE library_items SET status = ? WHERE id = ?"));
    query.addBindValue(status);
    query.addBindValue(libraryItemId);

    if (!query.exec()) {
        qWarning() << "internal::updateStatus failed:" << query.lastError().text();
        return false;
    }
    return true;
}

} // namespace internal

} // namespace bookhub::gui
