#include "gutenberg_adapter.h"
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QXmlStreamReader>
#include <QRegularExpression>

#include <archive.h>
#include <archive_entry.h>

namespace bookhub::collector {

GutenbergAdapter::GutenbergAdapter(QObject* parent)
    : ISourceAdapter(parent), m_networkManager(this) {}

void GutenbergAdapter::fetchBooks() {
    qDebug() << "GutenbergAdapter: Checking for RDF catalog updates...";

    QNetworkRequest request(QUrl("https://www.gutenberg.org/cache/epub/feeds/rdf-files.tar.bz2"));

    // Check If-Modified-Since to prevent redundant downloads
    QSettings settings;
    QString lastModified = settings.value("gutenberg_last_modified").toString();
    if (!lastModified.isEmpty()) {
        request.setRawHeader("If-Modified-Since", lastModified.toUtf8());
    }

    QNetworkReply* reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onNetworkReply(reply);
    });
}

void GutenbergAdapter::onNetworkReply(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() == QNetworkReply::NoError) {
        // HTTP 200 OK: New file downloaded
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 200) {
            qDebug() << "GutenbergAdapter: Downloaded new RDF catalog.";

            // Save Last-Modified header
            QByteArray lastModified = reply->rawHeader("Last-Modified");
            if (!lastModified.isEmpty()) {
                QSettings settings;
                settings.setValue("gutenberg_last_modified", QString::fromUtf8(lastModified));
            }

            qDebug() << "GutenbergAdapter: Extracting and parsing archive...";
            extractAndParseArchive(reply);
        } else if (statusCode == 304) {
            // Not Modified
            qDebug() << "GutenbergAdapter: RDF catalog is up-to-date (304 Not Modified). No changes needed.";
            emit fetchCompleted(true);
        } else {
            emit fetchCompleted(false, QString("Unexpected HTTP status code: %1").arg(statusCode));
        }
    } else if (reply->error() == QNetworkReply::ContentAccessDenied &&
               reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 304) {
        qDebug() << "GutenbergAdapter: RDF catalog is up-to-date (304 Not Modified). No changes needed.";
        emit fetchCompleted(true);
    } else {
        qDebug() << "GutenbergAdapter: Network error:" << reply->errorString();
        emit fetchCompleted(false, reply->errorString());
    }
}

void GutenbergAdapter::extractAndParseArchive(QNetworkReply* reply) {
    // Read directly from the network reply in chunks so the full ~800 MB
    // compressed archive is never materialised in a single QByteArray.
    struct ReadCtx {
        QNetworkReply* reply;
        QByteArray buf;
    };
    ReadCtx ctx{reply, {}};

    auto readCb = [](archive*, void* data, const void** buffer) -> la_ssize_t {
        auto* ctx = static_cast<ReadCtx*>(data);
        ctx->buf = ctx->reply->read(65536);
        *buffer = ctx->buf.constData();
        return static_cast<la_ssize_t>(ctx->buf.size());
    };

    struct ArchiveDeleter {
        void operator()(archive* a) const noexcept { archive_read_free(a); }
    };

    std::unique_ptr<archive, ArchiveDeleter> a{archive_read_new()};
    if (!a) {
        emit fetchCompleted(false, "Failed to allocate archive reader.");
        return;
    }

    archive_read_support_filter_bzip2(a.get());
    archive_read_support_format_tar(a.get());

    if (archive_read_open(a.get(), &ctx, nullptr, readCb, nullptr) != ARCHIVE_OK) {
        emit fetchCompleted(false,
            QString("Failed to open archive: %1").arg(archive_error_string(a.get())));
        return;
    }

    QList<DiscoveredBook> batch;
    int totalParsed = 0;
    archive_entry* entry = nullptr;

    while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname) continue;

        const QString entryName = QString::fromUtf8(pathname);
        if (!entryName.endsWith(".rdf", Qt::CaseInsensitive)) {
            archive_read_data_skip(a.get());
            continue;
        }

        QByteArray rdfData;
        if (archive_entry_size_is_set(entry)) {
            const la_int64_t entrySize = archive_entry_size(entry);
            if (entrySize <= 0) continue;
            rdfData.resize(static_cast<qsizetype>(entrySize));
            const la_ssize_t bytesRead = archive_read_data(
                a.get(), rdfData.data(), static_cast<std::size_t>(entrySize));
            if (bytesRead != entrySize) continue;
        } else {
            // Size not set in header — read in chunks (shouldn't happen for tar, but be safe)
            constexpr std::size_t kChunk = 65536;
            char buf[kChunk];
            la_ssize_t n;
            while ((n = archive_read_data(a.get(), buf, kChunk)) > 0)
                rdfData.append(buf, static_cast<qsizetype>(n));
            if (n < 0) continue;
        }

        parseSingleRdf(rdfData, entryName, batch);

        if (batch.size() >= 500) {
            emit booksDiscovered(batch);
            totalParsed += static_cast<int>(batch.size());
            qDebug() << "GutenbergAdapter: Emitted batch of" << batch.size()
                     << "books. Total:" << totalParsed;
            batch.clear();
        }
    }

    if (!batch.isEmpty()) {
        emit booksDiscovered(batch);
        totalParsed += static_cast<int>(batch.size());
        qDebug() << "GutenbergAdapter: Emitted final batch of" << batch.size()
                 << "books. Total:" << totalParsed;
    }

    qDebug() << "GutenbergAdapter: Parsing complete. Total books:" << totalParsed;
    emit fetchCompleted(true);
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
                // Restrict to <pgterms:ebook>/<dcterms:title> to avoid picking up
                // title-like elements in nested file descriptions.  Use simplified()
                // to collapse embedded newlines/whitespace that appear in some RDF entries.
                book.title = xml.readElementText().simplified();
                // Strip MARC 21 subfield markers that Gutenberg embeds verbatim in some
                // title strings.  The RDF often includes MARC punctuation immediately
                // before the marker (e.g. "Main title : $b Subtitle"), so the regex also
                // consumes any trailing MARC punctuation chars (:;/,) to avoid producing
                // double colons like "Main title :: Subtitle".
                static const QRegularExpression reMarc(QStringLiteral("[\\s:;/,]*\\$[a-z]\\s*"));
                book.title.replace(reMarc, QStringLiteral(": "));
                book.title = book.title.simplified();
                // A title that begins with a $b marker (no main text before it) will
                // produce a leading ": " after the substitution.  Strip it so the stored
                // title starts with real content rather than punctuation.
                if (book.title.startsWith(QStringLiteral(": ")))
                    book.title = book.title.mid(2);
                if (!path.isEmpty()) path.removeLast(); // text read moves past EndElement
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
            // Extract the raw LCCN value and normalize it; store only the digits+alpha, no prefix.
            // Note: /authorities/names/ URIs identify persons, not works — they are skipped here
            // and in resolveBookId() so they never become book primary keys.
            QString lccnNormalized = normalizeLccn(raw);
            // lccnNormalized is "lccn:XYZ" — store just the part after the colon as value
            qsizetype colon = lccnNormalized.indexOf(':');
            if (colon >= 0) {
                book.identifiers.append(BookIdentifier{"lccn", lccnNormalized.mid(colon + 1)});
            }
        } else if (lower.startsWith("oclc:")) {
            book.identifiers.append(BookIdentifier{"oclc", raw.mid(5)});
        } else {
            // Check for 10 or 13 consecutive digits (ISBN), stripping hyphens first
            QString stripped = raw;
            stripped.remove('-');
            static const QRegularExpression reDigits("^\\d{10}$|^\\d{13}$");
            if (reDigits.match(stripped).hasMatch()) {
                QString isbn13;
                if (stripped.length() == 10) {
                    // Convert ISBN-10 to ISBN-13: prepend "978", drop old check digit, compute EAN-13 check
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
            // Unknown format — skip
        }
    }

    // Gutenberg ID is always appended as a fallback identifier
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
                        genre = keyword; // Use the matched keyword as a clean genre
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

        // Use the simplified genre/subject
        book.subjects.clear();
        if (!genre.isEmpty()) book.subjects.append(genre);

        batch.append(book);
    }
}

QString GutenbergAdapter::normalizeLccn(const QString& raw)
{
    QString lccn = raw;

    // Strip URI prefix if present
    const QString uriPrefix = "http://id.loc.gov/authorities/names/";
    if (lccn.startsWith(uriPrefix)) {
        lccn = lccn.mid(uriPrefix.length());
    } else if (lccn.toLower().startsWith("lccn:")) {
        lccn = lccn.mid(5);
    }

    // Separate leading alpha prefix from numeric suffix
    int splitPos = 0;
    while (splitPos < lccn.length() && lccn.at(splitPos).isLetter()) {
        ++splitPos;
    }
    QString alpha = lccn.left(splitPos);
    QString digits = lccn.mid(splitPos);

    // Pre-2001 LCCNs: 1–6 digit suffix, zero-padded to 6 digits (8 chars total with 2-letter prefix).
    // Post-2001 LCCNs: exactly 8 digits with no alpha prefix — do not zero-pad those.
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
            // /authorities/names/ URIs identify persons/corporate bodies, not works.
            // Accept only explicit "lccn:" prefixed identifiers as work-level LCCNs.
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
