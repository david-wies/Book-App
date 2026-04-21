#pragma once

#include <QObject>
#include <QList>
#include "source_adapter.h"

namespace classic_books::collector {

class BookDiscoveryService : public QObject {
    Q_OBJECT
public:
    explicit BookDiscoveryService(const QString& dbConnectionName, QObject* parent = nullptr);
    ~BookDiscoveryService() override = default;

    void addAdapter(ISourceAdapter* adapter);
    void startDiscovery();

private slots:
    void onBooksDiscovered(const QList<classic_books::collector::DiscoveredBook>& books);
    void onFetchCompleted(bool success, const QString& errorMessage);

private:
    void insertBookIntoDatabase(const DiscoveredBook& book, const QString& sourceName);

    QString m_dbConnectionName;
    QList<ISourceAdapter*> m_adapters;
};

} // namespace classic_books::collector
