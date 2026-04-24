#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

namespace bookhub::gui {

struct SearchParams {
    QString     keyword;
    QString     author;
    QStringList genres;
    int         yearFrom{0};
    int         yearTo{0};
    QStringList languages;
    QStringList sources;
    bool        ebookOnly{false};
    bool        audiobookOnly{false};
    QString     sortColumn{QStringLiteral("title")}; // "title" or "author"
};

struct SearchResult {
    QString     bookId;
    QString     title;
    QString     author;
    int         publishYear{0};
    QStringList languages;
    QStringList sources;
    QStringList formats;
    bool        inLibrary{false};
};

// ---------------------------------------------------------------------------
// SearchService — synchronous SQL queries on the GUI thread's default
// database connection. All methods called from the GUI thread only.
// ---------------------------------------------------------------------------

class SearchService : public QObject {
    Q_OBJECT
public:
    explicit SearchService(QObject *parent = nullptr);

    QList<SearchResult> search(const SearchParams &params,
                               int offset = 0,
                               int limit  = 50) const;

    // Separate count query so "Load more" doesn't recount.
    int count(const SearchParams &params) const;

    QStringList fetchDistinctLanguages() const;
    QStringList fetchDistinctSources()   const;
    QStringList fetchGenres()            const;
};

} // namespace bookhub::gui
