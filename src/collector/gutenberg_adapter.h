#pragma once

#include "source_adapter.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <cstdint>
#include <memory>

class GutenbergIdResolutionTest; // test friend — defined in tests/unit/

namespace bookhub::collector {

class GutenbergAdapter : public ISourceAdapter {
    Q_OBJECT
public:
    explicit GutenbergAdapter(const QString& dbConnectionName = QString{},
                              QObject* parent = nullptr);

    QString sourceName() const override { return QStringLiteral("Gutenberg"); }
    QString adapterId() const { return QStringLiteral("gutenberg"); }
    void fetchBooks() override;

private slots:
    void onReadyRead();
    void onMetaDataChanged();
    void onFinished();

private:
    friend class ::GutenbergIdResolutionTest;

    enum class FetchContext : std::uint8_t {
        Fresh,            ///< No prior data — full download, no conditional headers
        Conditional,      ///< Existing completed fetch — send If-Modified-Since
        ResumeDownload,   ///< Resuming an interrupted download — send Range + If-Range
        ResumeParse       ///< Cached archive intact, resume parse only
    };

    QString archiveCachePath() const;
    void startFetch(FetchContext ctx, qint64 resumeFromByte,
                    const QString& conditionalValidator,
                    const QString& conditionalValidatorType,
                    const QString& ifRangeValidator);
    void parseCachedArchive(const QString& archivePath,
                            const QString& resumeAfterEntry);
    void finishParseSuccess(const QString& lastModified,
                            const QString& validatorType);
    void abortWithError(const QString& message);
    void clearCachedArchive();
    // Server-confirmed unconditional 200 while we expected 304/206: discard any
    // partial state and start a fresh full download.  Caller must return early
    // if this returns false (cache open failed and abortWithError fired).
    bool transitionToFreshDownload(const QString& archivePath);

    void parseSingleRdf(const QByteArray& data, const QString& entryName,
                        QList<DiscoveredBook>& batch);
    QString normalizeFormatName(const QString& url, const QString& mimeType);

    static QString resolveBookId(const QStringList& rawIdentifiers, const QString& gutenbergId);
    static QString normalizeLccn(const QString& raw);

    QString m_dbConnectionName;
    QNetworkAccessManager m_networkManager;
    QNetworkReply* m_currentReply{nullptr};
    std::unique_ptr<QFile> m_cacheFile;

    FetchContext m_context{FetchContext::Fresh};
    qint64 m_bytesWrittenThisRun{0};
    qint64 m_startingOffset{0};
    qint64 m_lastPersistedBytes{0};
    QString m_serverValidator;     ///< Last-Modified (preferred) or ETag from response
    QString m_serverValidatorType; ///< "last_modified" or "etag" — matches
                                   ///< m_serverValidator's source header so the
                                   ///< next conditional GET uses the correct
                                   ///< If-Modified-Since vs If-None-Match.
    bool m_headerDecided{false};   ///< true once metaDataChanged finalized the body decision
    bool m_writingToCache{false};  ///< true if response body should be streamed to m_cacheFile
};

} // namespace bookhub::collector
