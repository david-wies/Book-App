#pragma once

#include <QPointer>
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
    QPointer<class BookDiscoveryService> m_discoveryService;
    QPointer<QTimer> m_pollTimer;
    std::atomic_bool m_shutdownRequested{false};
    std::atomic_bool m_updateInProgress{false};
};

} // namespace collector
} // namespace bookhub
