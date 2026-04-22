#pragma once

#include <QObject>
#include <QList>
#include "source_adapter.h"

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
    void insertBookIntoDatabase(const DiscoveredBook& book, const QString& sourceName);

    QString m_dbConnectionName;
    QList<ISourceAdapter*> m_adapters;
    int m_activeFetches{0};
};

} // namespace bookhub::collector
