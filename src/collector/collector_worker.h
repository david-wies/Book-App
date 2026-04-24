#pragma once

#include <QThread>
#include <QTimer>
#include <atomic>

namespace bookhub {
namespace collector {

class CollectorWorker : public QThread {
    Q_OBJECT
public:
    explicit CollectorWorker(QObject* parent = nullptr);
    ~CollectorWorker() override;

    void requestShutdownAfterCurrentUpdate();

protected:
    void run() override;

private:
    // Owned by the collector thread's run() stack frame; only accessed from
    // the collector thread except through QMetaObject::invokeMethod queued calls.
    class BookDiscoveryService* m_discoveryService{nullptr};
    QTimer* m_pollTimer{nullptr};
    std::atomic_bool m_shutdownRequested{false};
    std::atomic_bool m_updateInProgress{false};
};

} // namespace collector
} // namespace bookhub
