#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <atomic>

namespace bookhub::gui {

struct LibraryItem {
    int         id{};
    QString     bookId;
    int         editionId{};
    QString     title;
    QString     author;
    int         publishYear{};
    QString     language;
    QString     sourceName;
    QString     status;
    QDateTime   addedDate;
};

class QueryWorker;

// ---------------------------------------------------------------------------
// LibraryService — async facade for library read/write operations.
//
// Call connectToWorker() once after construction. Use request*() methods;
// each returns a requestId. Write completions also emit libraryChanged() on
// the GUI thread so LibraryScreen::reload() continues to work unchanged.
// ---------------------------------------------------------------------------

class LibraryService : public QObject {
    Q_OBJECT
public:
    explicit LibraryService(QObject *parent = nullptr);

    void connectToWorker(QueryWorker *worker);
    bool isBusy() const;

    quint64 requestFetchItems(const QString &sortColumn = QStringLiteral("added_date"));
    quint64 requestAddBook(const QString &bookId, int editionId);
    quint64 requestRemoveBook(int libraryItemId);
    quint64 requestUpdateStatus(int libraryItemId, const QString &status);

signals:
    // Result signals — delivered on the GUI thread
    void fetchItemsCompleted(quint64 requestId, QList<bookhub::gui::LibraryItem> items);
    void addBookCompleted(quint64 requestId, QString bookId, bool success, int newId);
    void removeBookCompleted(quint64 requestId, bool success);
    void updateStatusCompleted(quint64 requestId, bool success);

    // Emitted after any successful write so views can refresh
    void libraryChanged();

    // Internal request signals — routed to QueryWorker
    void fetchItemsRequested(quint64 requestId, QString sortColumn);
    void addBookRequested(quint64 requestId, QString bookId, int editionId);
    void removeBookRequested(quint64 requestId, int libraryItemId);
    void updateStatusRequested(quint64 requestId, int libraryItemId, QString status);

private:
    std::atomic<quint64> m_nextRequestId{1};
    int                  m_pendingCount{0};
};

// ---------------------------------------------------------------------------
// Internal free functions — called by QueryWorker and integration tests.
// Write functions do NOT emit libraryChanged; the service does that on the
// GUI thread after receiving the result signal.
// ---------------------------------------------------------------------------
namespace internal {
    QList<LibraryItem> fetchItems(const QString &sortColumn,
                                  const QString &connectionName);
    // Returns new row ID (> 0) on success, 0 if already exists, -1 on error.
    int addBook(const QString &bookId, int editionId,
                const QString &connectionName);
    bool removeBook(int libraryItemId, const QString &connectionName);
    bool updateStatus(int libraryItemId, const QString &status,
                      const QString &connectionName);
} // namespace internal

} // namespace bookhub::gui
