#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>

namespace bookhub::collector {

struct BookIdentifier {
    QString type;   // "lccn", "oclc", "isbn", "gutenberg", "archive", "benyehuda"
    QString value;  // raw identifier value (no prefix)
};

struct DiscoveredBook {
    QString sourceId;
    QString resolvedId;
    QString title;
    QStringList authors;
    QStringList languages;
    QStringList subjects;
    QList<BookIdentifier> identifiers;
    QMap<QString, QString> formats;
};

class ISourceAdapter : public QObject {
    Q_OBJECT
public:
    explicit ISourceAdapter(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ISourceAdapter() = default;
    virtual QString sourceName() const = 0;
    virtual void fetchBooks() = 0;
signals:
    void booksDiscovered(const QList<bookhub::collector::DiscoveredBook>& books);
    void fetchCompleted(bool success, const QString& errorMessage = QString());
};

} // namespace bookhub::collector
