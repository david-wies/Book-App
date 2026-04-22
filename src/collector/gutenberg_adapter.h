#pragma once

#include "source_adapter.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>

namespace classic_books::collector {

class GutenbergAdapter : public ISourceAdapter {
    Q_OBJECT
public:
    explicit GutenbergAdapter(QObject* parent = nullptr);
    ~GutenbergAdapter() override;

    QString sourceName() const override { return "Gutenberg"; }
    void fetchBooks() override;

private slots:
    void onNetworkReply(QNetworkReply* reply);
    void onExtractionFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void parseExtractedRdfFiles(const QString& extractDir);
    void parseSingleRdf(const QString& filePath, QList<DiscoveredBook>& batch);
    QString normalizeFormatName(const QString& url, const QString& mimeType);

    static QString resolveBookId(const QStringList& rawIdentifiers, const QString& gutenbergId);
    static QString normalizeLccn(const QString& raw);

    QNetworkAccessManager m_networkManager;
    QProcess* m_extractProcess{nullptr};
    
    QString m_tempArchiveDir;
    QString m_archivePath;
    QString m_extractDir;
};

} // namespace classic_books::collector
