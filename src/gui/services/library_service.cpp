#include "library_service.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace bookhub::gui {

LibraryService::LibraryService(QObject *parent)
    : QObject(parent)
{}

QList<LibraryItem> LibraryService::fetchItems(const QString &sortColumn) const
{
    // Build ORDER BY clause from caller-supplied sort key.
    // The whitelist prevents SQL injection — only known column tokens are accepted.
    QString orderBy;
    if (sortColumn == QLatin1String("title")) {
        orderBy = QStringLiteral("b.title ASC");
    } else if (sortColumn == QLatin1String("author")) {
        orderBy = QStringLiteral("b.author ASC");
    } else if (sortColumn == QLatin1String("status")) {
        orderBy = QStringLiteral("li.status ASC");
    } else {
        // Default: newest first
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
            COALESCE(s.source_name, '') AS source_name,
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

    QSqlQuery query(QSqlDatabase::database());
    if (!query.exec(sql)) {
        qWarning() << "LibraryService::fetchItems failed:" << query.lastError().text();
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

int LibraryService::addBook(const QString &bookId, int editionId)
{
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO library_items (book_id, edition_id, status) VALUES (?, ?, 'saved')"
    ));
    query.addBindValue(bookId);
    // Store NULL when no specific edition is chosen — the FK allows NULL.
    if (editionId > 0)
        query.addBindValue(editionId);
    else
        query.addBindValue(QVariant(QMetaType::fromType<int>()));

    if (!query.exec()) {
        qWarning() << "LibraryService::addBook failed:" << query.lastError().text();
        return -1;
    }

    const int newId = query.lastInsertId().toInt();
    if (newId == 0)
        return 0;

    emit libraryChanged();
    return newId;
}

bool LibraryService::removeBook(int libraryItemId)
{
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(QStringLiteral("DELETE FROM library_items WHERE id = ?"));
    query.addBindValue(libraryItemId);

    if (!query.exec()) {
        qWarning() << "LibraryService::removeBook failed:" << query.lastError().text();
        return false;
    }

    if (query.numRowsAffected() == 0)
        return true;

    emit libraryChanged();
    return true;
}

bool LibraryService::updateStatus(int libraryItemId, const QString &status)
{
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(QStringLiteral("UPDATE library_items SET status = ? WHERE id = ?"));
    query.addBindValue(status);
    query.addBindValue(libraryItemId);

    if (!query.exec()) {
        qWarning() << "LibraryService::updateStatus failed:" << query.lastError().text();
        return false;
    }

    if (query.numRowsAffected() == 0)
        return true;

    emit libraryChanged();
    return true;
}

} // namespace bookhub::gui
