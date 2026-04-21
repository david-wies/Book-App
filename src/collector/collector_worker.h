#pragma once

#include <QThread>

namespace classic_books {
namespace collector {

class CollectorWorker : public QThread {
    Q_OBJECT
public:
    explicit CollectorWorker(QObject* parent = nullptr);
    ~CollectorWorker() override;

protected:
    void run() override;

private:
    class BookDiscoveryService* m_discoveryService{nullptr};
};

} // namespace collector
} // namespace classic_books
