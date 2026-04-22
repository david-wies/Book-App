#pragma once

#include "source_adapter.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

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
    void extractAndParseArchive(const QByteArray& archiveData);
    void parseSingleRdf(const QByteArray& data, const QString& entryName, QList<DiscoveredBook>& batch);
    QString normalizeFormatName(const QString& url, const QString& mimeType);

    static QString resolveBookId(const QStringList& rawIdentifiers, const QString& gutenbergId);
    static QString normalizeLccn(const QString& raw);

    QNetworkAccessManager m_networkManager;
};

} // namespace bookhub::collector
