#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>

namespace classic_books::collector {

// Representation of a book discovered from a source
struct DiscoveredBook {
    QString sourceId;      // e.g. "gutenberg_1342"
    QString title;
    QStringList authors;
    QStringList languages;
    QStringList subjects;
    
    // Map of format type (e.g. "epub") to download link
    QMap<QString, QString> formats;
};

class ISourceAdapter : public QObject {
    Q_OBJECT
public:
    explicit ISourceAdapter(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ISourceAdapter() = default;

    virtual QString sourceName() const = 0;
    
    // Starts fetching data. When done, should emit booksDiscovered and fetchCompleted.
    virtual void fetchBooks() = 0;

signals:
    void booksDiscovered(const QList<classic_books::collector::DiscoveredBook>& books);
    void fetchCompleted(bool success, const QString& errorMessage = QString());
};

} // namespace classic_books::collector
