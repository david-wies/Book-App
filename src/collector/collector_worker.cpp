#include "collector_worker.h"
#include "database.h"
#include "book_discovery_service.h"
#include "gutenberg_adapter.h"
#include <QDebug>
#include <QTimer>

namespace classic_books {
namespace collector {

CollectorWorker::CollectorWorker(QObject* parent) : QThread(parent) {}

CollectorWorker::~CollectorWorker() {
    requestInterruption();
    quit();
    wait();
}

void CollectorWorker::run() {
    qDebug() << "Data collector background thread started.";

    // 1. Initialize thread-local database connection
    QString connectionName = "collector_connection";
    if (!classic_books::db::initializeDatabase(classic_books::db::databaseFilePath(), connectionName)) {
        qWarning() << "Collector thread failed to initialize database.";
        return;
    }

    // 2. Setup Discovery Service
    BookDiscoveryService discoveryService(connectionName);
    m_discoveryService = &discoveryService;
    m_discoveryService->addAdapter(new GutenbergAdapter(m_discoveryService));

    // 3. Start first discovery right away, and set up timer
    m_discoveryService->startDiscovery();

    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [this]() {
        qDebug() << "Collector timer tick: starting discovery...";
        m_discoveryService->startDiscovery();
    });
    // Fetch every 10 minutes (600,000 ms)
    timer.start(600000); 

    // Enter thread event loop
    exec();

    qDebug() << "Data collector background thread stopped.";
}

} // namespace collector
} // namespace classic_books
