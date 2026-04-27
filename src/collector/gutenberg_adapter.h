#pragma once

#include "source_adapter.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class GutenbergIdResolutionTest; // test friend — defined in tests/unit/

namespace bookhub::collector {

class GutenbergAdapter : public ISourceAdapter {
    Q_OBJECT
public:
    explicit GutenbergAdapter(QObject* parent = nullptr);

    QString sourceName() const override { return "Gutenberg"; }
    void fetchBooks() override;

private slots:
    void onNetworkReply(QNetworkReply* reply);

private:
    friend class ::GutenbergIdResolutionTest;

    // Streams the archive directly from the network reply in chunks to avoid
    // materialising the full ~800 MB download into a single QByteArray.
    void extractAndParseArchive(QNetworkReply* reply);
    void parseSingleRdf(const QByteArray& data, const QString& entryName, QList<DiscoveredBook>& batch);
    QString normalizeFormatName(const QString& url, const QString& mimeType);

    static QString resolveBookId(const QStringList& rawIdentifiers, const QString& gutenbergId);
    static QString normalizeLccn(const QString& raw);

    QNetworkAccessManager m_networkManager;
};

} // namespace bookhub::collector
