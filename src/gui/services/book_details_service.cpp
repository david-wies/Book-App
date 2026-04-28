#include "book_details_service.h"
#include "../query_worker.h"

#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace bookhub::gui {

BookDetailsService::BookDetailsService(QObject *parent)
    : QObject(parent)
{}

void BookDetailsService::connectToWorker(QueryWorker *worker)
{
    connect(this, &BookDetailsService::bookDetailsRequested,
            worker, &QueryWorker::handleBookDetailsRequest);
    connect(this, &BookDetailsService::formatsForEditionRequested,
            worker, &QueryWorker::handleFormatsForEditionRequest);

    connect(worker, &QueryWorker::bookDetailsCompleted, this,
            [this](quint64 id, BookDetails details) {
                emit detailsCompleted(id, details);
            });
    connect(worker, &QueryWorker::formatsForEditionCompleted, this,
            [this](quint64 id, QList<BookFormatEntry> formats) {
                emit formatsCompleted(id, formats);
            });
}

quint64 BookDetailsService::requestBookDetails(const QString &bookId)
{
    const quint64 id = m_nextRequestId++;
    emit bookDetailsRequested(id, bookId);
    return id;
}

quint64 BookDetailsService::requestFormatsForEdition(int editionId)
{
    const quint64 id = m_nextRequestId++;
    emit formatsForEditionRequested(id, editionId);
    return id;
}

// ---------------------------------------------------------------------------
// Internal free functions
// ---------------------------------------------------------------------------

namespace internal {

BookDetails fetchBookDetails(const QString &bookId, const QString &connectionName)
{
    BookDetails result;
    result.bookId = bookId;

    // Core book data + library status + genres (one aggregating query)
    const QString coreSql = QStringLiteral(R"(
        SELECT
            b.title,
            b.author,
            COALESCE(b.publish_year, 0)                       AS publish_year,
            COALESCE(b.summary, '')                           AS summary,
            COALESCE(GROUP_CONCAT(DISTINCT g.genre_name), '') AS genres,
            COALESCE(li.id, 0)                                AS library_item_id
        FROM books b
        LEFT JOIN book_genres    bg ON bg.book_id = b.book_id
        LEFT JOIN genres          g ON g.id        = bg.genre_id
        LEFT JOIN library_items  li ON li.book_id  = b.book_id
        WHERE b.book_id = ?
        GROUP BY b.book_id, li.id
    )");

    QSqlQuery coreQ(QSqlDatabase::database(connectionName));
    coreQ.prepare(coreSql);
    coreQ.addBindValue(bookId);

    if (!coreQ.exec() || !coreQ.next()) {
        qWarning() << "internal::fetchBookDetails (core) failed:"
                   << coreQ.lastError().text();
        return result;
    }

    result.title         = coreQ.value(0).toString();
    result.author        = coreQ.value(1).toString();
    result.publishYear   = coreQ.value(2).toInt();
    result.summary       = coreQ.value(3).toString();
    result.libraryItemId = coreQ.value(5).toInt();
    result.inLibrary     = result.libraryItemId > 0;

    const QString genreStr = coreQ.value(4).toString();
    if (!genreStr.isEmpty())
        result.genres = genreStr.split(QLatin1Char(','));

    // Editions (language list for selector)
    QSqlQuery edQ(QSqlDatabase::database(connectionName));
    edQ.prepare(QStringLiteral(
        "SELECT id, language FROM editions WHERE book_id = ? ORDER BY language"));
    edQ.addBindValue(bookId);

    if (!edQ.exec()) {
        qWarning() << "internal::fetchBookDetails (editions) failed:"
                   << edQ.lastError().text();
        return result;
    }

    while (edQ.next()) {
        BookEditionEntry ed;
        ed.editionId = edQ.value(0).toInt();
        ed.language  = edQ.value(1).toString();
        result.editions.append(ed);
    }

    return result;
}

QList<BookFormatEntry> fetchFormatsForEdition(int editionId,
                                               const QString &connectionName)
{
    const QString sql = QStringLiteral(R"(
        SELECT f.format_type, s.source_name, s.download_link
        FROM   formats f
        JOIN   sources s ON s.format_id = f.id
        WHERE  f.edition_id = ?
        ORDER  BY f.format_type, s.source_name
    )");

    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(sql);
    query.addBindValue(editionId);

    if (!query.exec()) {
        qWarning() << "internal::fetchFormatsForEdition failed:"
                   << query.lastError().text();
        return {};
    }

    QMap<QString, int> fmtIndex;
    QList<BookFormatEntry> formats;

    while (query.next()) {
        const QString fmt = query.value(0).toString();

        if (!fmtIndex.contains(fmt)) {
            BookFormatEntry entry;
            entry.formatType = fmt;
            formats.append(entry);
            fmtIndex[fmt] = static_cast<int>(formats.size()) - 1;
        }

        BookSourceEntry src;
        src.sourceName   = query.value(1).toString();
        src.downloadLink = query.value(2).toString();
        formats[fmtIndex[fmt]].sources.append(src);
    }

    return formats;
}

} // namespace internal

} // namespace bookhub::gui
