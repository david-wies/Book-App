#pragma once

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include "source_adapter.h"

class BookDiscoveryServiceTest; // test friend — defined in tests/integration/

namespace bookhub::collector {

class BookDiscoveryService : public QObject {
    Q_OBJECT
public:
    explicit BookDiscoveryService(const QString& dbConnectionName, QObject* parent = nullptr);
    ~BookDiscoveryService() override = default;

    void addAdapter(ISourceAdapter* adapter);
    void startDiscovery();

signals:
    void updateStarted();
    void updateFinished();

private slots:
    void onBooksDiscovered(const QList<bookhub::collector::DiscoveredBook>& books);
    void onFetchCompleted(bool success, const QString& errorMessage);

private:
    friend class ::BookDiscoveryServiceTest;

    void insertBookIntoDatabase(const DiscoveredBook& book, const QString& sourceName);
    // No-transaction variant: the caller is responsible for the surrounding
    // batch transaction.  Returns true if the book was successfully written,
    // false if a SAVEPOINT-rollback is required.  Failures here log a warning
    // and instruct the caller to rollback this book's savepoint while keeping
    // the rest of the batch intact.
    bool insertBookRows(const DiscoveredBook& book, const QString& sourceName,
                        QSqlDatabase& db);

    QString m_dbConnectionName;
    QList<ISourceAdapter*> m_adapters;
    int m_activeFetches{0};
};

} // namespace bookhub::collector
