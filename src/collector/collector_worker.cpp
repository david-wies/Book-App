#include "collector_worker.h"
#include "database.h"
#include "book_discovery_service.h"
#include "gutenberg_adapter.h"
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

namespace classic_books {
namespace collector {

CollectorWorker::CollectorWorker(QObject* parent) : QThread(parent) {}

CollectorWorker::~CollectorWorker() {
    requestShutdownAfterCurrentUpdate();
    wait();
}

void CollectorWorker::requestShutdownAfterCurrentUpdate() {
    m_shutdownRequested.store(true);

    auto* discoveryService = m_discoveryService.data();
    if (!discoveryService) {
        quit();
        return;
    }

    QMetaObject::invokeMethod(discoveryService, [this]() {
        if (m_pollTimer) {
            m_pollTimer->stop();
        }

        if (!m_updateInProgress.load()) {
            QThread::currentThread()->quit();
        }
    }, Qt::QueuedConnection);
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
    connect(m_discoveryService, &BookDiscoveryService::updateStarted, this, [this]() {
        m_updateInProgress.store(true);
    }, Qt::DirectConnection);
    connect(m_discoveryService, &BookDiscoveryService::updateFinished, this, [this]() {
        m_updateInProgress.store(false);

        if (m_shutdownRequested.load()) {
            if (m_pollTimer) {
                m_pollTimer->stop();
            }
            QThread::currentThread()->quit();
        }
    }, Qt::DirectConnection);

    // 3. Start first discovery right away, and set up timer
    m_discoveryService->startDiscovery();

    QTimer timer;
    m_pollTimer = &timer;
    connect(&timer, &QTimer::timeout, this, [this]() {
        if (m_shutdownRequested.load()) {
            return;
        }

        qDebug() << "Collector timer tick: starting discovery...";
        m_discoveryService->startDiscovery();
    });
    // Fetch every 10 minutes (600,000 ms)
    timer.start(600000); 

    // Enter thread event loop
    exec();

    m_pollTimer = nullptr;
    m_discoveryService = nullptr;
    qDebug() << "Data collector background thread stopped.";
}

} // namespace collector
} // namespace classic_books
