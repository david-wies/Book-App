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
    // NOTE: format map keys use the pattern "<normalized_mime>_<count>",
    // e.g. "epub_1", "epub_2", "text_plain_1". Count suffix avoids key
    // collisions when a book has multiple files of the same MIME type.
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
