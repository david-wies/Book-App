#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

namespace bookhub::gui {

struct BookSourceEntry {
    QString sourceName;
    QString downloadLink;
};

struct BookFormatEntry {
    QString                formatType;
    QList<BookSourceEntry> sources;
};

struct BookEditionEntry {
    int     editionId{};
    QString language;
};

struct BookDetails {
    QString                 bookId;
    QString                 title;
    QString                 author;
    int                     publishYear{0};
    QString                 summary;
    QStringList             genres;
    QList<BookEditionEntry> editions;
    bool                    inLibrary{false};
    int                     libraryItemId{0};
    QString                 libraryStatus;
};

class QueryWorker;

// ---------------------------------------------------------------------------
// BookDetailsService — async facade for loading a single book's full details.
//
// Two-step loading pattern:
//   1. requestBookDetails(bookId) → detailsCompleted — carries core metadata,
//      all editions (for the language selector), and current library status.
//   2. requestFormatsForEdition(editionId) → formatsCompleted — called once on
//      initial load and again whenever the user switches language editions.
// ---------------------------------------------------------------------------

class BookDetailsService : public QObject {
    Q_OBJECT
public:
    explicit BookDetailsService(QObject *parent = nullptr);

    void connectToWorker(QueryWorker *worker);

    // Call before request*() to capture the pending ID before synchronous dispatch.
    quint64 peekNextId() const { return m_nextRequestId; }

    quint64 requestBookDetails(const QString &bookId);
    quint64 requestFormatsForEdition(int editionId);

signals:
    // Result signals — delivered on the GUI thread
    void detailsCompleted(quint64 requestId, bookhub::gui::BookDetails details);
    void formatsCompleted(quint64 requestId, QList<bookhub::gui::BookFormatEntry> formats);

    // Internal request signals — routed to QueryWorker
    void bookDetailsRequested(quint64 requestId, QString bookId);
    void formatsForEditionRequested(quint64 requestId, int editionId);

private:
    quint64 m_nextRequestId{1};
};

// ---------------------------------------------------------------------------
// Internal free functions — called by QueryWorker slots and integration tests.
// Each takes an explicit connectionName so they work on any SQL connection.
// ---------------------------------------------------------------------------
namespace internal {
    BookDetails            fetchBookDetails(const QString &bookId,
                                           const QString &connectionName);
    QList<BookFormatEntry> fetchFormatsForEdition(int editionId,
                                                   const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
