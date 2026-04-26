#include "search_service.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QStringList>

namespace bookhub::gui {

SearchService::SearchService(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Escape SQLite LIKE wildcards so user-supplied text is treated as literal.
// The backslash escape character is declared in every LIKE clause below.
static QString escapeLike(const QString &raw)
{
    QString out = raw;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('%'),  QStringLiteral("\\%"));
    out.replace(QLatin1Char('_'),  QStringLiteral("\\_"));
    return out;
}

// Builds the WHERE clause and binds values into `query` for all active
// filters. Returns false if binding fails. The IN-list filters (genres,
// languages, sources) use one `?` placeholder per value so no user input
// is ever interpolated into SQL text.
// Parameter order must exactly match the placeholders produced by buildSql.
static bool bindParams(QSqlQuery &query,
                       const SearchParams &p,
                       int offset,
                       int limit,
                       bool isCount)
{
    // keyword — only bound when the filter is active (clause omitted otherwise)
    const QString kwTrimmed = p.keyword.trimmed();
    if (!kwTrimmed.isEmpty()) {
        const QString kw = QStringLiteral("%") + escapeLike(kwTrimmed) + QStringLiteral("%");
        query.addBindValue(kw);
        query.addBindValue(kw);
    }

    // author filter — only bound when active
    const QString afTrimmed = p.author.trimmed();
    if (!afTrimmed.isEmpty()) {
        const QString af = QStringLiteral("%") + escapeLike(afTrimmed) + QStringLiteral("%");
        query.addBindValue(af);
    }

    // genre IN values
    for (const QString &g : p.genres)
        query.addBindValue(g);

    // year from
    query.addBindValue(p.yearFrom > 0 ? 1 : 0);
    query.addBindValue(p.yearFrom > 0 ? p.yearFrom : 0);

    // year to
    query.addBindValue(p.yearTo > 0 ? 1 : 0);
    query.addBindValue(p.yearTo > 0 ? p.yearTo : 0);

    // language IN values
    for (const QString &l : p.languages)
        query.addBindValue(l);

    // source IN values
    for (const QString &s : p.sources)
        query.addBindValue(s);

    // ebook available
    query.addBindValue(p.ebookOnly ? 1 : 0);

    // audiobook ready
    query.addBindValue(p.audiobookOnly ? 1 : 0);

    if (!isCount) {
        query.addBindValue(limit);
        query.addBindValue(offset);
    }

    return true;
}

// Generates the WHERE fragment + GROUP BY + ORDER BY + LIMIT/OFFSET.
// IN-list placeholders are generated here so bindParams and the SQL text
// always agree on the number of bound parameters.
static QString buildSql(const SearchParams &p, bool isCount)
{
    // Genre IN clause
    QString genreFilter;
    if (!p.genres.isEmpty()) {
        QStringList ph;
        ph.reserve(p.genres.size());
        for (int i = 0; i < p.genres.size(); ++i)
            ph.append(QStringLiteral("?"));
        genreFilter = QStringLiteral("AND g.genre_name IN (%1)").arg(ph.join(QLatin1String(",")));
    }

    // Language IN clause
    QString langFilter;
    if (!p.languages.isEmpty()) {
        QStringList ph;
        ph.reserve(p.languages.size());
        for (int i = 0; i < p.languages.size(); ++i)
            ph.append(QStringLiteral("?"));
        langFilter = QStringLiteral("AND e.language IN (%1)").arg(ph.join(QLatin1String(",")));
    }

    // Source IN clause
    QString srcFilter;
    if (!p.sources.isEmpty()) {
        QStringList ph;
        ph.reserve(p.sources.size());
        for (int i = 0; i < p.sources.size(); ++i)
            ph.append(QStringLiteral("?"));
        srcFilter = QStringLiteral("AND s.source_name IN (%1)").arg(ph.join(QLatin1String(",")));
    }

    // Keyword filter clause — omitted entirely when no keyword is set so no
    // binding placeholder is emitted. bindParams must bind in the same order.
    const QString kwFilter = p.keyword.trimmed().isEmpty()
        ? QString{}
        : QStringLiteral("AND (b.title LIKE ? ESCAPE '\\' OR b.author LIKE ? ESCAPE '\\')");

    // Author filter clause — omitted when author field is empty.
    const QString authorFilter = p.author.trimmed().isEmpty()
        ? QString{}
        : QStringLiteral("AND b.author LIKE ? ESCAPE '\\'");

    // Genre JOIN is only needed when a genre filter is active; otherwise the
    // LEFT JOIN avoids expanding rows for books that have multiple genres.
    const QString genreJoin = p.genres.isEmpty()
        ? QStringLiteral("LEFT JOIN book_genres bg ON b.book_id = bg.book_id\n"
                         "        LEFT JOIN genres g ON bg.genre_id = g.id")
        : QStringLiteral("JOIN book_genres bg ON b.book_id = bg.book_id\n"
                         "        JOIN genres g ON bg.genre_id = g.id");

    // The audiobook filter targets library_items.status — force an INNER JOIN
    // when that flag is set; otherwise LEFT JOIN suffices.
    const QString liJoin = p.audiobookOnly
        ? QStringLiteral("JOIN library_items li ON b.book_id = li.book_id")
        : QStringLiteral("LEFT JOIN library_items li ON b.book_id = li.book_id");

    // Whitelist the sort column to prevent SQL injection; only "author" is an
    // alternative — everything else falls back to the default "title".
    const QString orderCol = (p.sortColumn == QLatin1String("author"))
                             ? QStringLiteral("b.author")
                             : QStringLiteral("b.title");

    if (isCount) {
        return QStringLiteral(R"(
            SELECT COUNT(DISTINCT b.book_id)
            FROM books b
            JOIN editions e ON b.book_id = e.book_id
            %1
            LEFT JOIN formats f ON e.id = f.edition_id
            LEFT JOIN sources s ON f.id = s.format_id
            %2
            WHERE 1=1
              %3
              %4
              %5
              AND (? = 0 OR b.publish_year >= ?)
              AND (? = 0 OR b.publish_year <= ?)
              %6
              %7
              AND (? = 0 OR f.format_type IS NOT NULL)
              AND (? = 0 OR li.status = 'audiobook_ready')
        )").arg(genreJoin, liJoin, kwFilter, authorFilter, genreFilter)
           .arg(langFilter, srcFilter);
    }

    return QStringLiteral(R"(
        SELECT b.book_id, b.title, b.author, b.publish_year,
               GROUP_CONCAT(DISTINCT e.language)    AS languages,
               GROUP_CONCAT(DISTINCT s.source_name) AS sources,
               GROUP_CONCAT(DISTINCT f.format_type) AS formats,
               MAX(CASE WHEN li.book_id IS NOT NULL THEN 1 ELSE 0 END) AS in_library
        FROM books b
        JOIN editions e ON b.book_id = e.book_id
        %1
        LEFT JOIN formats f ON e.id = f.edition_id
        LEFT JOIN sources s ON f.id = s.format_id
        %2
        WHERE 1=1
          %3
          %4
          %5
          AND (? = 0 OR b.publish_year >= ?)
          AND (? = 0 OR b.publish_year <= ?)
          %6
          %7
          AND (? = 0 OR f.format_type IS NOT NULL)
          AND (? = 0 OR li.status = 'audiobook_ready')
        GROUP BY b.book_id
        ORDER BY %8
        LIMIT ? OFFSET ?
    )").arg(genreJoin, liJoin, kwFilter, authorFilter, genreFilter)
       .arg(langFilter, srcFilter, orderCol);
}

// ---------------------------------------------------------------------------

QList<SearchResult> SearchService::search(const SearchParams &params,
                                          int offset,
                                          int limit) const
{
    const QString sql = buildSql(params, false);
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(sql);
    bindParams(query, params, offset, limit, false);

    if (!query.exec()) {
        qWarning() << "SearchService::search failed:" << query.lastError().text();
        return {};
    }

    QList<SearchResult> results;
    while (query.next()) {
        SearchResult r;
        r.bookId      = query.value(0).toString();
        r.title       = query.value(1).toString();
        r.author      = query.value(2).toString();
        r.publishYear = query.value(3).toInt();

        const QString langs = query.value(4).toString();
        if (!langs.isEmpty())
            r.languages = langs.split(QLatin1Char(','), Qt::SkipEmptyParts);

        const QString srcs = query.value(5).toString();
        if (!srcs.isEmpty())
            r.sources = srcs.split(QLatin1Char(','), Qt::SkipEmptyParts);

        const QString fmts = query.value(6).toString();
        if (!fmts.isEmpty())
            r.formats = fmts.split(QLatin1Char(','), Qt::SkipEmptyParts);

        r.inLibrary = query.value(7).toInt() != 0;
        results.append(r);
    }
    return results;
}

int SearchService::count(const SearchParams &params) const
{
    const QString sql = buildSql(params, true);
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(sql);
    bindParams(query, params, 0, 0, true);

    if (!query.exec() || !query.next()) {
        qWarning() << "SearchService::count failed:" << query.lastError().text();
        return 0;
    }
    return query.value(0).toInt();
}

QStringList SearchService::fetchDistinctLanguages() const
{
    QSqlQuery query(QSqlDatabase::database());
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT language FROM editions ORDER BY language"))) {
        qWarning() << "SearchService::fetchDistinctLanguages failed:"
                   << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(query.value(0).toString());
    return result;
}

QStringList SearchService::fetchDistinctSources() const
{
    QSqlQuery query(QSqlDatabase::database());
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT source_name FROM sources ORDER BY source_name"))) {
        qWarning() << "SearchService::fetchDistinctSources failed:"
                   << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(query.value(0).toString());
    return result;
}

QStringList SearchService::fetchGenres() const
{
    QSqlQuery query(QSqlDatabase::database());
    if (!query.exec(QStringLiteral(
            "SELECT genre_name FROM genres ORDER BY genre_name"))) {
        qWarning() << "SearchService::fetchGenres failed:" << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(query.value(0).toString());
    return result;
}

} // namespace bookhub::gui
