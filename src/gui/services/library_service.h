#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// LibraryItem — a single row from the library_items JOIN query.
// Keeps all display-relevant fields flat so the view model doesn't need to
// run additional queries per row.
// ---------------------------------------------------------------------------

struct LibraryItem {
    int         id{};
    QString     bookId;
    int         editionId{};
    QString     title;
    QString     author;
    int         publishYear{};
    QString     language;       // from editions.language
    QString     sourceName;     // from sources.source_name (first source found)
    QString     status;         // library_items.status
    QDateTime   addedDate;
};

// ---------------------------------------------------------------------------
// LibraryService — plain QObject that executes synchronous SQL queries on the
// GUI thread's default database connection.
//
// All methods must be called from the GUI thread only (Section 8.7 of spec).
// ---------------------------------------------------------------------------

class LibraryService : public QObject {
    Q_OBJECT
public:
    explicit LibraryService(QObject *parent = nullptr);

    // Returns all library items joined with books / editions / sources data.
    // Sort column is one of: "added_date", "title", "author", "status".
    QList<LibraryItem> fetchItems(const QString &sortColumn = QStringLiteral("added_date")) const;

    // Inserts a new library_items row for the given book/edition pair.
    // Returns the new row id, or -1 on failure.
    int addBook(const QString &bookId, int editionId);

    // Deletes the library_items row with the given id.
    bool removeBook(int libraryItemId);

    // Updates the status field for the given library_items row.
    bool updateStatus(int libraryItemId, const QString &status);

signals:
    // Emitted after any write operation so views can refresh.
    void libraryChanged();
};

} // namespace bookhub::gui
