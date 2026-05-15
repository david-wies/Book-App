#include "search_service.h"
#include "../query_worker.h"

#include "shared/database.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QStringList>

namespace bookhub::gui {

SearchService::SearchService(QObject *parent)
    : QObject(parent)
{}

void SearchService::connectToWorker(QueryWorker *worker)
{
    // Route requests to worker (Qt::AutoConnection → queued cross-thread, direct same-thread)
    connect(this, &SearchService::searchRequested,
            worker, &QueryWorker::handleSearchRequest);
    connect(this, &SearchService::countRequested,
            worker, &QueryWorker::handleCountRequest);
    connect(this, &SearchService::languagesRequested,
            worker, &QueryWorker::handleLanguagesRequest);
    connect(this, &SearchService::sourcesRequested,
            worker, &QueryWorker::handleSourcesRequest);
    connect(this, &SearchService::genresRequested,
            worker, &QueryWorker::handleGenresRequest);

    // Relay results back from worker to callers
    connect(worker, &QueryWorker::searchCompleted, this,
            [this](quint64 id, QList<SearchResult> results) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit searchCompleted(id, std::move(results));
            });
    connect(worker, &QueryWorker::countCompleted, this,
            [this](quint64 id, int count) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit countCompleted(id, count);
            });
    connect(worker, &QueryWorker::languagesCompleted, this,
            [this](quint64 id, QStringList langs) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit languagesCompleted(id, std::move(langs));
            });
    connect(worker, &QueryWorker::sourcesCompleted, this,
            [this](quint64 id, QStringList srcs) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit sourcesCompleted(id, std::move(srcs));
            });
    connect(worker, &QueryWorker::genresCompleted, this,
            [this](quint64 id, QStringList genres) {
                Q_ASSERT(m_pendingCount > 0);
                --m_pendingCount;
                emit genresCompleted(id, std::move(genres));
            });
}

bool SearchService::isBusy() const
{
    return m_pendingCount > 0;
}

quint64 SearchService::peekNextId() const
{
    return m_nextRequestId.load(std::memory_order_relaxed);
}

quint64 SearchService::requestSearch(const SearchParams &params, int offset, int limit)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit searchRequested(id, params, offset, limit);
    return id;
}

quint64 SearchService::requestCount(const SearchParams &params)
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit countRequested(id, params);
    return id;
}

quint64 SearchService::requestLanguages()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit languagesRequested(id);
    return id;
}

quint64 SearchService::requestSources()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit sourcesRequested(id);
    return id;
}

quint64 SearchService::requestGenres()
{
    const quint64 id = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    ++m_pendingCount;
    emit genresRequested(id);
    return id;
}

// ---------------------------------------------------------------------------
// Internal helpers (used by the internal free functions below)
// ---------------------------------------------------------------------------

namespace internal {

// Escape SQLite LIKE wildcards so user-supplied text is treated as literal.
static QString escapeLike(const QString &raw)
{
    QString out = raw;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('%'),  QStringLiteral("\\%"));
    out.replace(QLatin1Char('_'),  QStringLiteral("\\_"));
    return out;
}

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

    const QString kwFilter = p.keyword.trimmed().isEmpty()
        ? QString{}
        : QStringLiteral("AND (b.title LIKE ? ESCAPE '\\' OR b.author LIKE ? ESCAPE '\\')");

    const QString authorFilter = p.author.trimmed().isEmpty()
        ? QString{}
        : QStringLiteral("AND b.author LIKE ? ESCAPE '\\'");

    const QString genreJoin = p.genres.isEmpty()
        ? QStringLiteral("LEFT JOIN book_genres bg ON b.book_id = bg.book_id\n"
                         "        LEFT JOIN genres g ON bg.genre_id = g.id")
        : QStringLiteral("JOIN book_genres bg ON b.book_id = bg.book_id\n"
                         "        JOIN genres g ON bg.genre_id = g.id");

    const QString liJoin = p.audiobookOnly
        ? QStringLiteral("JOIN library_items li ON b.book_id = li.book_id")
        : QStringLiteral("LEFT JOIN library_items li ON b.book_id = li.book_id");

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
// Public internal API
// ---------------------------------------------------------------------------

QList<SearchResult> runSearch(const SearchParams &params, int offset, int limit,
                               const QString &connectionName)
{
    const QString sql = buildSql(params, false);
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(sql);
    bindParams(query, params, offset, limit, false);

    if (!query.exec()) {
        qWarning() << "internal::runSearch failed:" << query.lastError().text();
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

int runCount(const SearchParams &params, const QString &connectionName)
{
    const QString sql = buildSql(params, true);
    QSqlQuery query(QSqlDatabase::database(connectionName));
    query.prepare(sql);
    bindParams(query, params, 0, 0, true);

    if (!query.exec() || !query.next()) {
        qWarning() << "internal::runCount failed:" << query.lastError().text();
        return 0;
    }
    return query.value(0).toInt();
}

QStringList fetchLanguages(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT language FROM editions ORDER BY language"))) {
        qWarning() << "internal::fetchLanguages failed:" << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(bookhub::db::languageNameForCode(query.value(0).toString()));
    // sort + removeDuplicates is required even though the SQL uses
    // SELECT DISTINCT: languageNameForCode() collapses synonymous ISO codes
    // ("en" and "eng" both map to "English"), so distinct DB rows can yield
    // duplicate display names that the database has no way to merge.
    result.sort();
    result.removeDuplicates();
    return result;
}

QStringList fetchSources(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT source_name FROM sources ORDER BY source_name"))) {
        qWarning() << "internal::fetchSources failed:" << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(query.value(0).toString());
    return result;
}

QStringList fetchGenres(const QString &connectionName)
{
    QSqlQuery query(QSqlDatabase::database(connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT genre_name FROM genres ORDER BY genre_name"))) {
        qWarning() << "internal::fetchGenres failed:" << query.lastError().text();
        return {};
    }
    QStringList result;
    while (query.next())
        result.append(query.value(0).toString());
    return result;
}

} // namespace internal

} // namespace bookhub::gui
