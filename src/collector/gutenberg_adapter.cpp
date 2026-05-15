#include "gutenberg_adapter.h"

#include "shared/database.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include <archive.h>
#include <archive_entry.h>

namespace bookhub::collector {

namespace {

constexpr auto kGutenbergRdfUrl =
    "https://www.gutenberg.org/cache/epub/feeds/rdf-files.tar.bz2";

// Persist bytes_downloaded to sync_state at most every 4 MB while the response
// streams in.  Bounds DB writes to a few hundred over an ~800 MB download.
constexpr qint64 kProgressFlushBytes = qint64{4} * 1024 * 1024;

// Flush books to the DB after this many books OR this many archive entries —
// whichever first.  The entry cap keeps the parse cursor (last_parsed_entry)
// fresh even through long stretches of non-RDF tar entries.
constexpr int kBatchBookLimit = 500;
constexpr int kBatchEntryLimit = 1000;

// Minimum book count for the "DB has real data, use conditional GET" branch.
// The dev-only sample data seeds 3 gutenberg:* rows; any real Gutenberg fetch
// produces tens of thousands.  This threshold distinguishes the two without a
// dedicated marker column.
constexpr qint64 kMinBooksForConditionalGet = 100;

QString cacheDir()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir{}.mkpath(base + QStringLiteral("/gutenberg"));
    return base + QStringLiteral("/gutenberg");
}

} // namespace

GutenbergAdapter::GutenbergAdapter(const QString& dbConnectionName, QObject* parent)
    : ISourceAdapter(parent),
      m_dbConnectionName(dbConnectionName),
      m_networkManager(this) {}

QString GutenbergAdapter::archiveCachePath() const
{
    return cacheDir() + QStringLiteral("/rdf-files.tar.bz2");
}

void GutenbergAdapter::fetchBooks() {
    qDebug() << "GutenbergAdapter: Evaluating sync state...";

    const QString archivePath = archiveCachePath();
    const auto state = db::getSyncState(adapterId(), m_dbConnectionName);
    const qint64 bookCount = db::countBooksForAdapter(adapterId(), m_dbConnectionName);

    // Resume parse — the archive should already be on disk in full.
    if (state && state->status == QLatin1String("in_progress")
            && state->phase == QLatin1String("parsing")) {
        QFileInfo fi(state->archivePath);
        if (fi.exists() && state->bytesTotal > 0 && fi.size() == state->bytesTotal) {
            qDebug() << "GutenbergAdapter: Resuming parse from"
                     << (state->lastParsedEntry.isEmpty() ? QStringLiteral("(beginning)")
                                                          : state->lastParsedEntry);
            parseCachedArchive(state->archivePath, state->lastParsedEntry);
            return;
        }
        qDebug() << "GutenbergAdapter: parse-resume cache missing or size mismatch; "
                    "restarting fresh.";
    }

    // Resume download — partial archive on disk; send Range + If-Range.
    if (state && state->status == QLatin1String("in_progress")
            && state->phase == QLatin1String("downloading")) {
        QFileInfo fi(state->archivePath);
        if (fi.exists() && state->bytesDownloaded > 0
                && fi.size() == state->bytesDownloaded
                && !state->downloadEtag.isEmpty()) {
            qDebug() << "GutenbergAdapter: Resuming download from byte"
                     << state->bytesDownloaded;
            startFetch(FetchContext::ResumeDownload, state->bytesDownloaded,
                       QString{}, QString{}, state->downloadEtag);
            return;
        }
        qDebug() << "GutenbergAdapter: download-resume cache inconsistent; "
                    "restarting fresh.";
    }

    // Conditional refresh — completed before with non-empty DB.
    if (state && state->status == QLatin1String("completed")
            && !state->lastModified.isEmpty()
            && bookCount >= kMinBooksForConditionalGet) {
        // Pre-v9 rows have an empty validator_type but always stored a
        // Last-Modified value (Gutenberg never sent ETag), so default to that.
        const QString validatorType =
            state->validatorType.isEmpty() ? QStringLiteral("last_modified")
                                           : state->validatorType;
        const QString headerName = (validatorType == QLatin1String("etag"))
            ? QStringLiteral("If-None-Match")
            : QStringLiteral("If-Modified-Since");
        qDebug().noquote()
            << QStringLiteral("GutenbergAdapter: Conditional fetch (%1: %2)")
                   .arg(headerName, state->lastModified);
        startFetch(FetchContext::Conditional, 0,
                   state->lastModified, validatorType, QString{});
        return;
    }

    // Fresh fetch — no row, prior failure, or DB empty.
    qDebug() << "GutenbergAdapter: Fresh fetch.";
    db::beginFetch(adapterId(), QString::fromLatin1(kGutenbergRdfUrl),
                   archivePath, m_dbConnectionName);
    startFetch(FetchContext::Fresh, 0, QString{}, QString{}, QString{});
}

void GutenbergAdapter::startFetch(FetchContext ctx, qint64 resumeFromByte,
                                  const QString& conditionalValidator,
                                  const QString& conditionalValidatorType,
                                  const QString& ifRangeValidator) {
    m_context = ctx;
    m_bytesWrittenThisRun = 0;
    m_startingOffset = resumeFromByte;
    m_lastPersistedBytes = resumeFromByte;
    m_serverValidator.clear();
    m_serverValidatorType.clear();
    m_headerDecided = false;
    m_writingToCache = false;
    m_cacheFile.reset();

    QNetworkRequest request{QUrl(QString::fromLatin1(kGutenbergRdfUrl))};

    switch (ctx) {
    case FetchContext::Conditional:
        if (!conditionalValidator.isEmpty()) {
            const char* header =
                (conditionalValidatorType == QLatin1String("etag"))
                    ? "If-None-Match"
                    : "If-Modified-Since";
            request.setRawHeader(header, conditionalValidator.toUtf8());
        }
        break;
    case FetchContext::ResumeDownload: {
        const QByteArray rangeVal =
            QByteArray("bytes=") + QByteArray::number(resumeFromByte) + "-";
        request.setRawHeader("Range", rangeVal);
        if (!ifRangeValidator.isEmpty()) {
            request.setRawHeader("If-Range", ifRangeValidator.toUtf8());
        }
        break;
    }
    case FetchContext::Fresh:
    case FetchContext::ResumeParse:
        break;
    }

    m_currentReply = m_networkManager.get(request);
    connect(m_currentReply, &QNetworkReply::metaDataChanged,
            this, &GutenbergAdapter::onMetaDataChanged);
    connect(m_currentReply, &QNetworkReply::readyRead,
            this, &GutenbergAdapter::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &GutenbergAdapter::onFinished);
}

void GutenbergAdapter::onMetaDataChanged()
{
    if (!m_currentReply || m_headerDecided)
        return;

    const QVariant statusVar =
        m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusVar.isValid())
        return; // headers not fully available yet

    const int status = statusVar.toInt();

    const QByteArray lastModified = m_currentReply->rawHeader("Last-Modified");
    const QByteArray etag = m_currentReply->rawHeader("ETag");
    if (!lastModified.isEmpty()) {
        m_serverValidator = QString::fromUtf8(lastModified);
        m_serverValidatorType = QStringLiteral("last_modified");
    } else if (!etag.isEmpty()) {
        m_serverValidator = QString::fromUtf8(etag);
        m_serverValidatorType = QStringLiteral("etag");
    } else {
        m_serverValidator.clear();
        m_serverValidatorType.clear();
    }

    const QString archivePath = archiveCachePath();

    auto openTruncate = [&]() -> bool {
        m_cacheFile = std::make_unique<QFile>(archivePath);
        if (!m_cacheFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            abortWithError(QStringLiteral("Failed to open cache file for writing: %1")
                               .arg(m_cacheFile->errorString()));
            return false;
        }
        m_writingToCache = true;
        return true;
    };

    auto openAppend = [&]() -> bool {
        m_cacheFile = std::make_unique<QFile>(archivePath);
        if (!m_cacheFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
            abortWithError(QStringLiteral("Failed to open cache file for append: %1")
                               .arg(m_cacheFile->errorString()));
            return false;
        }
        m_writingToCache = true;
        return true;
    };

    switch (m_context) {
    case FetchContext::Fresh:
        if (status == 200) {
            if (!openTruncate()) return;
            db::recordDownloadProgress(adapterId(), 0, 0,
                                       m_serverValidator, m_serverValidatorType,
                                       m_dbConnectionName);
            m_headerDecided = true;
        } else {
            abortWithError(
                QStringLiteral("Fresh fetch: unexpected HTTP status %1").arg(status));
        }
        break;

    case FetchContext::Conditional:
        if (status == 304) {
            m_writingToCache = false;
            m_headerDecided = true;
        } else if (status == 200) {
            // Server has new data — transition to in_progress + download fresh.
            if (!transitionToFreshDownload(archivePath)) return;
        } else {
            abortWithError(
                QStringLiteral("Conditional fetch: unexpected HTTP status %1").arg(status));
        }
        break;

    case FetchContext::ResumeDownload:
        if (status == 206) {
            if (!openAppend()) return;
            m_headerDecided = true;
        } else if (status == 200) {
            // If-Range validator did not match — server's resource changed.
            qDebug() << "GutenbergAdapter: If-Range mismatch (got 200); restarting download.";
            if (!transitionToFreshDownload(archivePath)) return;
        } else if (status == 416) {
            // Cache is past server size — discard and let next tick start fresh.
            // abortWithError nulls m_currentReply *before* aborting, so the
            // queued onFinished short-circuits and cannot overwrite this message
            // with the generic "Operation canceled" failure.  No need to set
            // m_headerDecided / m_writingToCache: every reader of those flags
            // gates on m_currentReply being non-null, which abortWithError
            // clears.
            qDebug() << "GutenbergAdapter: 416 Range Not Satisfiable; clearing cache.";
            clearCachedArchive();
            abortWithError(
                QStringLiteral("Range request returned 416; cache cleared."));
            return;
        } else {
            abortWithError(
                QStringLiteral("Resume download: unexpected HTTP status %1").arg(status));
        }
        break;

    case FetchContext::ResumeParse:
        // Not used — parse-resume path bypasses startFetch().
        break;
    }
}

void GutenbergAdapter::onReadyRead()
{
    if (!m_currentReply || !m_writingToCache || !m_cacheFile)
        return;

    const QByteArray chunk = m_currentReply->readAll();
    if (chunk.isEmpty())
        return;

    const qint64 written = m_cacheFile->write(chunk);
    if (written != chunk.size()) {
        abortWithError(QStringLiteral("Short write to cache file: %1")
                           .arg(m_cacheFile->errorString()));
        return;
    }
    m_bytesWrittenThisRun += written;

    const qint64 totalOnDisk = m_startingOffset + m_bytesWrittenThisRun;
    if (totalOnDisk - m_lastPersistedBytes >= kProgressFlushBytes) {
        m_cacheFile->flush();
        db::recordDownloadProgress(adapterId(), totalOnDisk, 0,
                                   m_serverValidator, m_serverValidatorType,
                                   m_dbConnectionName);
        m_lastPersistedBytes = totalOnDisk;
    }
}

void GutenbergAdapter::onFinished()
{
    if (!m_currentReply)
        return;

    QNetworkReply* reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();

    // Conditional fetch landed on 304 — also surfaces as ContentAccessDenied in Qt's
    // error mapping on some platforms, so check the status code rather than just error().
    if (status == 304) {
        if (m_cacheFile) m_cacheFile.reset();
        // Touch completed_at; last_modified + validator_type stay as they were.
        db::completeFetch(adapterId(), QString{}, QString{}, m_dbConnectionName);
        qDebug() << "GutenbergAdapter: 304 Not Modified — catalog is up to date.";
        emit fetchCompleted(true);
        return;
    }

    if (m_cacheFile) {
        m_cacheFile->flush();
        m_cacheFile->close();
    }

    if (err != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("Network error: %1").arg(reply->errorString());
        qWarning() << "GutenbergAdapter:" << msg;
        const qint64 totalOnDisk = m_startingOffset + m_bytesWrittenThisRun;
        // Persist whatever progress we have so the next tick can resume.
        if (m_writingToCache && totalOnDisk > 0) {
            db::recordDownloadProgress(adapterId(), totalOnDisk, 0,
                                       m_serverValidator, m_serverValidatorType,
                                       m_dbConnectionName);
        }
        db::failFetch(adapterId(), msg, m_dbConnectionName);
        m_cacheFile.reset();
        emit fetchCompleted(false, msg);
        return;
    }

    // Status sanity check — Fresh path expects 200, ResumeDownload expects 206
    // (or has already mutated to Fresh on If-Range mismatch).
    if (status != 200 && status != 206) {
        const QString msg = QStringLiteral("Unexpected final HTTP status %1").arg(status);
        db::failFetch(adapterId(), msg, m_dbConnectionName);
        m_cacheFile.reset();
        emit fetchCompleted(false, msg);
        return;
    }

    const qint64 totalOnDisk = m_startingOffset + m_bytesWrittenThisRun;
    db::recordDownloadComplete(adapterId(), totalOnDisk, m_dbConnectionName);
    m_cacheFile.reset();

    qDebug() << "GutenbergAdapter: Download complete," << totalOnDisk
             << "bytes. Beginning parse.";
    parseCachedArchive(archiveCachePath(), QString{});
}

void GutenbergAdapter::abortWithError(const QString& message)
{
    qWarning() << "GutenbergAdapter:" << message;
    if (m_currentReply) {
        QNetworkReply* reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (m_cacheFile) {
        m_cacheFile->close();
        m_cacheFile.reset();
    }
    db::failFetch(adapterId(), message, m_dbConnectionName);
    emit fetchCompleted(false, message);
}

void GutenbergAdapter::clearCachedArchive()
{
    QFile::remove(archiveCachePath());
}

bool GutenbergAdapter::transitionToFreshDownload(const QString& archivePath)
{
    db::beginFetch(adapterId(), QString::fromLatin1(kGutenbergRdfUrl),
                   archivePath, m_dbConnectionName);
    db::recordDownloadProgress(adapterId(), 0, 0,
                               m_serverValidator, m_serverValidatorType,
                               m_dbConnectionName);

    m_cacheFile = std::make_unique<QFile>(archivePath);
    if (!m_cacheFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        abortWithError(QStringLiteral("Failed to open cache file for writing: %1")
                           .arg(m_cacheFile->errorString()));
        return false;
    }
    m_writingToCache = true;
    m_startingOffset = 0;
    m_lastPersistedBytes = 0;
    m_bytesWrittenThisRun = 0;
    m_context = FetchContext::Fresh;
    m_headerDecided = true;
    return true;
}

void GutenbergAdapter::finishParseSuccess(const QString& lastModified,
                                          const QString& validatorType)
{
    db::completeFetch(adapterId(), lastModified, validatorType, m_dbConnectionName);
    clearCachedArchive();
    qDebug() << "GutenbergAdapter: Parse complete; cache cleared.";
    emit fetchCompleted(true);
}

void GutenbergAdapter::parseCachedArchive(const QString& archivePath,
                                          const QString& resumeAfterEntry)
{
    // Capture the validator for the *current* fetch.  Prefer what we just learned
    // from the network (m_serverValidator), but fall back to whatever the sync_state
    // row already has (set by an earlier successful download we're now resuming the
    // parse for).
    QString validatorForCompletion = m_serverValidator;
    QString validatorTypeForCompletion = m_serverValidatorType;
    if (validatorForCompletion.isEmpty()) {
        if (auto s = db::getSyncState(adapterId(), m_dbConnectionName)) {
            validatorForCompletion = s->downloadEtag;
            // Fall back to validator_type recorded earlier; if absent (e.g. v8
            // legacy row written before this column existed), treat as
            // Last-Modified — matches the historical Gutenberg behaviour.
            validatorTypeForCompletion = s->validatorType.isEmpty()
                ? QStringLiteral("last_modified")
                : s->validatorType;
        }
    }

    struct ArchiveDeleter {
        void operator()(archive* a) const noexcept { archive_read_free(a); }
    };
    std::unique_ptr<archive, ArchiveDeleter> a{archive_read_new()};
    if (!a) {
        const QString msg = QStringLiteral("Failed to allocate libarchive reader.");
        db::failFetch(adapterId(), msg, m_dbConnectionName);
        emit fetchCompleted(false, msg);
        return;
    }

    archive_read_support_filter_bzip2(a.get());
    archive_read_support_format_tar(a.get());

    const QByteArray pathBytes = archivePath.toUtf8();
    if (archive_read_open_filename(a.get(), pathBytes.constData(), 65536) != ARCHIVE_OK) {
        const QString msg = QStringLiteral("Failed to open cached archive: %1")
                                .arg(QString::fromUtf8(archive_error_string(a.get())));
        db::failFetch(adapterId(), msg, m_dbConnectionName);
        clearCachedArchive();
        emit fetchCompleted(false, msg);
        return;
    }

    QList<DiscoveredBook> batch;
    int totalParsedThisRun = 0;
    int entriesSinceLastFlush = 0;
    QString lastEntryInBatch;
    bool sawResumePoint = resumeAfterEntry.isEmpty();
    int skippedEntries = 0;
    archive_entry* entry = nullptr;

    auto flushBatch = [&](const QString& entryAtFlush) {
        if (batch.isEmpty() && entryAtFlush.isEmpty())
            return;
        const int delta = batch.size();
        if (!batch.isEmpty()) {
            emit booksDiscovered(batch);
            totalParsedThisRun += delta;
            qDebug() << "GutenbergAdapter: Committed batch of" << delta
                     << "books. Total this run:" << totalParsedThisRun;
            batch.clear();
        }
        if (!entryAtFlush.isEmpty()) {
            db::recordBatchCommit(adapterId(), entryAtFlush, delta, m_dbConnectionName);
        }
        entriesSinceLastFlush = 0;
    };

    while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname) {
            archive_read_data_skip(a.get());
            continue;
        }
        const QString entryName = QString::fromUtf8(pathname);

        if (!sawResumePoint) {
            archive_read_data_skip(a.get());
            ++skippedEntries;
            if (entryName == resumeAfterEntry)
                sawResumePoint = true;
            continue;
        }

        ++entriesSinceLastFlush;

        if (!entryName.endsWith(QStringLiteral(".rdf"), Qt::CaseInsensitive)) {
            archive_read_data_skip(a.get());
            if (entriesSinceLastFlush >= kBatchEntryLimit) {
                flushBatch(entryName);
            }
            continue;
        }

        QByteArray rdfData;
        if (archive_entry_size_is_set(entry)) {
            const la_int64_t entrySize = archive_entry_size(entry);
            if (entrySize <= 0) {
                archive_read_data_skip(a.get());
                continue;
            }
            rdfData.resize(static_cast<qsizetype>(entrySize));
            const la_ssize_t bytesRead = archive_read_data(
                a.get(), rdfData.data(), static_cast<std::size_t>(entrySize));
            if (bytesRead != entrySize) {
                qWarning() << "GutenbergAdapter: short read on" << entryName;
                continue;
            }
        } else {
            constexpr std::size_t kChunk = 65536;
            char buf[kChunk];
            la_ssize_t n;
            while ((n = archive_read_data(a.get(), buf, kChunk)) > 0)
                rdfData.append(buf, static_cast<qsizetype>(n));
            if (n < 0) continue;
        }

        parseSingleRdf(rdfData, entryName, batch);
        lastEntryInBatch = entryName;

        if (batch.size() >= kBatchBookLimit
                || entriesSinceLastFlush >= kBatchEntryLimit) {
            flushBatch(lastEntryInBatch);
        }
    }

    // Flush any remainder.
    if (!batch.isEmpty()) {
        flushBatch(lastEntryInBatch);
    } else if (!lastEntryInBatch.isEmpty()) {
        // Edge case: we processed entries past the resume point but the final
        // batch was already flushed at the limit boundary.  Update the cursor
        // anyway so the next run would not redo the last partial chunk.
        db::recordBatchCommit(adapterId(), lastEntryInBatch, 0, m_dbConnectionName);
    }

    // If we had a resume marker but never matched it in the archive, treat as
    // a corrupt cursor: fail (cache is cleared so next tick starts clean).
    if (!resumeAfterEntry.isEmpty() && !sawResumePoint) {
        const QString msg = QStringLiteral(
            "Resume marker '%1' not found in archive; cache will be refreshed.")
                .arg(resumeAfterEntry);
        qWarning() << "GutenbergAdapter:" << msg;
        db::failFetch(adapterId(), msg, m_dbConnectionName);
        clearCachedArchive();
        emit fetchCompleted(false, msg);
        return;
    }

    qDebug() << "GutenbergAdapter: Parse complete. Books this run:" << totalParsedThisRun
             << "Skipped entries (resume):" << skippedEntries;
    finishParseSuccess(validatorForCompletion, validatorTypeForCompletion);
}

QString GutenbergAdapter::normalizeFormatName(const QString& url, const QString& mimeType) {
    Q_UNUSED(url);
    if (mimeType.isEmpty()) {
        return QString();
    }

    // Strip MIME parameters (e.g. "; charset=us-ascii") before matching — Gutenberg
    // sometimes uses parameterised MIME types that would otherwise leak into format_type.
    QString lowerMime = mimeType.toLower().trimmed();
    const int semiPos = lowerMime.indexOf(';');
    if (semiPos >= 0)
        lowerMime = lowerMime.left(semiPos).trimmed();

    if (lowerMime.contains("image")) {
        return QString();
    }

    if (lowerMime == "application/epub+zip") return "epub";
    if (lowerMime == "text/plain") return "plain";
    if (lowerMime == "text/html") return "html";
    if (lowerMime == "application/pdf") return "pdf";
    if (lowerMime == "application/x-mobipocket-ebook") return "mobi";
    if (lowerMime == "application/octet-stream") return "octet_stream";
    if (lowerMime == "text/rtf") return "rtf";
    if (lowerMime == "text/xml") return "xml";

    QStringList parts = lowerMime.split('/');
    if (!parts.isEmpty()) {
        return parts.last().replace('+', '_').replace('-', '_');
    }
    return lowerMime.replace('+', '_').replace('-', '_');
}

void GutenbergAdapter::parseSingleRdf(const QByteArray& data, const QString& entryName,
                                      QList<DiscoveredBook>& batch) {
    QXmlStreamReader xml(data);
    DiscoveredBook book;

    // Fallback ID from archive entry path (e.g. cache/epub/1342/pg1342.rdf → "1342")
    QString baseName = QFileInfo(entryName).baseName();
    if (baseName.startsWith("pg")) baseName = baseName.mid(2);
    book.sourceId = baseName;

    QStringList path;
    QString currentFormatUrl;
    QString currentFormatMime;
    QMap<QString, int> formatCounts;
    QStringList subjects;
    QStringList types;
    QStringList rawIdentifiers;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            QString name = xml.name().toString();
            path.append(name);

            if (name == "ebook") {
                QString about = xml.attributes().value("rdf:about").toString();
                QRegularExpression re("ebooks/(\\d+)");
                QRegularExpressionMatch match = re.match(about);
                if (match.hasMatch()) {
                    book.sourceId = match.captured(1);
                }
            } else if (name == "file") {
                currentFormatUrl = xml.attributes().value("rdf:about").toString();
                currentFormatMime.clear();
            } else if (name == "title" && path.size() >= 2
                       && path.at(path.size() - 2) == QLatin1String("ebook")) {
                book.title = xml.readElementText().simplified();
                static const QRegularExpression reMarc(QStringLiteral("[\\s:;/,]*\\$[a-z]\\s*"));
                book.title.replace(reMarc, QStringLiteral(": "));
                book.title = book.title.simplified();
                if (book.title.startsWith(QStringLiteral(": ")))
                    book.title = book.title.mid(2);
                if (!path.isEmpty()) path.removeLast();
            } else if (name == "identifier") {
                rawIdentifiers.append(xml.readElementText().trimmed());
                if (!path.isEmpty()) path.removeLast();
            } else if (name == "name" && path.size() >= 3 &&
                       path.at(path.size()-2) == "agent" && path.at(path.size()-3) == "creator") {
                book.authors.append(xml.readElementText().trimmed());
                if (!path.isEmpty()) path.removeLast();
            } else if (name == "value") {
                QString val = xml.readElementText().trimmed();
                if (!path.isEmpty()) path.removeLast();
                if (path.contains("subject")) {
                    subjects.append(val);
                } else if (path.contains("language")) {
                    book.languages.append(val);
                } else if (path.contains("format")) {
                    currentFormatMime = val;
                } else if (path.contains("type")) {
                    types.append(val);
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            QString name = xml.name().toString();
            if (name == "file") {
                if (!currentFormatUrl.isEmpty()) {
                    QString formatName = normalizeFormatName(currentFormatUrl, currentFormatMime);
                    if (!formatName.isEmpty()) {
                        formatCounts[formatName]++;
                        QString linkKey = QString("%1_%2").arg(formatName).arg(formatCounts[formatName]);
                        book.formats[linkKey] = currentFormatUrl;
                    }
                    currentFormatUrl.clear();
                    currentFormatMime.clear();
                }
            }
            if (!path.isEmpty()) {
                path.removeLast();
            }
        }
    }

    book.resolvedId = resolveBookId(rawIdentifiers, book.sourceId);

    for (const QString& raw : rawIdentifiers) {
        QString lower = raw.toLower();
        if (lower.startsWith("lccn:")) {
            QString lccnNormalized = normalizeLccn(raw);
            qsizetype colon = lccnNormalized.indexOf(':');
            if (colon >= 0) {
                book.identifiers.append(BookIdentifier{"lccn", lccnNormalized.mid(colon + 1)});
            }
        } else if (lower.startsWith("oclc:")) {
            book.identifiers.append(BookIdentifier{"oclc", raw.mid(5)});
        } else {
            QString stripped = raw;
            stripped.remove('-');
            static const QRegularExpression reDigits("^\\d{10}$|^\\d{13}$");
            if (reDigits.match(stripped).hasMatch()) {
                QString isbn13;
                if (stripped.length() == 10) {
                    QString base = "978" + stripped.left(9);
                    int sum = 0;
                    for (int i = 0; i < 12; ++i) {
                        int digit = base.at(i).digitValue();
                        sum += (i % 2 == 0) ? digit : digit * 3;
                    }
                    int check = (10 - (sum % 10)) % 10;
                    isbn13 = base + QString::number(check);
                } else {
                    isbn13 = stripped;
                }
                book.identifiers.append(BookIdentifier{"isbn", isbn13});
            }
        }
    }

    book.identifiers.append(BookIdentifier{"gutenberg", book.sourceId});

    if (!book.title.isEmpty() && !book.sourceId.isEmpty()) {
        QString genre;
        QStringList possibleKeywords = {
            "fiction", "novel", "poetry", "drama", "history",
            "biography", "science", "philosophy", "juvenile",
            "children's stories", "adventure", "mystery",
            "fantasy", "science fiction", "horror", "thriller",
            "romance", "short stories", "gothic fiction"
        };

        if (!subjects.isEmpty()) {
            for (const QString& subj : subjects) {
                QString lowerSubj = subj.toLower();
                for (const QString& keyword : possibleKeywords) {
                    if (lowerSubj.contains(keyword)) {
                        genre = keyword;
                        break;
                    }
                }
                if (!genre.isEmpty()) break;
            }
            if (genre.isEmpty()) {
                genre = subjects.mid(0, 2).join(", ");
            }
        } else if (!types.isEmpty()) {
            genre = types.first();
        }

        book.subjects.clear();
        if (!genre.isEmpty()) book.subjects.append(genre);

        batch.append(book);
    }
}

QString GutenbergAdapter::normalizeLccn(const QString& raw)
{
    QString lccn = raw;

    const QString uriPrefix = "http://id.loc.gov/authorities/names/";
    if (lccn.startsWith(uriPrefix)) {
        lccn = lccn.mid(uriPrefix.length());
    } else if (lccn.toLower().startsWith("lccn:")) {
        lccn = lccn.mid(5);
    }

    int splitPos = 0;
    while (splitPos < lccn.length() && lccn.at(splitPos).isLetter()) {
        ++splitPos;
    }
    QString alpha = lccn.left(splitPos);
    QString digits = lccn.mid(splitPos);

    if (digits.length() < 8) {
        digits = digits.rightJustified(8, '0');
    }

    return "lccn:" + alpha + digits;
}

QString GutenbergAdapter::resolveBookId(const QStringList& rawIdentifiers, const QString& gutenbergId)
{
    QString lccnResult;
    QString oclcResult;
    QString isbnResult;

    for (const QString& raw : rawIdentifiers) {
        QString lower = raw.toLower();

        if (lccnResult.isEmpty()) {
            if (lower.startsWith("lccn:")) {
                lccnResult = normalizeLccn(raw);
                continue;
            }
        }

        if (oclcResult.isEmpty() && lower.startsWith("oclc:")) {
            oclcResult = "oclc:" + raw.mid(5);
            continue;
        }

        if (isbnResult.isEmpty()) {
            QString stripped = raw;
            stripped.remove('-');
            static const QRegularExpression reIsbn("^\\d{10}$|^\\d{13}$");
            if (reIsbn.match(stripped).hasMatch()) {
                if (stripped.length() == 10) {
                    QString base = "978" + stripped.left(9);
                    int sum = 0;
                    for (int i = 0; i < 12; ++i) {
                        int digit = base.at(i).digitValue();
                        sum += (i % 2 == 0) ? digit : digit * 3;
                    }
                    int check = (10 - (sum % 10)) % 10;
                    isbnResult = "isbn:" + base + QString::number(check);
                } else {
                    isbnResult = "isbn:" + stripped;
                }
            }
        }
    }

    if (!lccnResult.isEmpty()) return lccnResult;
    if (!oclcResult.isEmpty()) return oclcResult;
    if (!isbnResult.isEmpty()) return isbnResult;
    return "gutenberg:" + gutenbergId;
}

} // namespace bookhub::collector
