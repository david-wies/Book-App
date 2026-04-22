#include "gutenberg_adapter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QXmlStreamReader>
#include <QRegularExpression>

namespace classic_books::collector {

GutenbergAdapter::GutenbergAdapter(QObject* parent) 
    : ISourceAdapter(parent), m_networkManager(this) {
    connect(&m_networkManager, &QNetworkAccessManager::finished, this, &GutenbergAdapter::onNetworkReply);
}

GutenbergAdapter::~GutenbergAdapter() {
    if (m_extractProcess) {
        m_extractProcess->kill();
        m_extractProcess->waitForFinished();
    }
}

void GutenbergAdapter::fetchBooks() {
    qDebug() << "GutenbergAdapter: Checking for RDF catalog updates...";
    
    QNetworkRequest request(QUrl("https://www.gutenberg.org/cache/epub/feeds/rdf-files.tar.bz2"));
    
    // Check If-Modified-Since to prevent redundant downloads
    QSettings settings("ClassicBooks", "AudiobookHub");
    QString lastModified = settings.value("gutenberg_last_modified").toString();
    if (!lastModified.isEmpty()) {
        request.setRawHeader("If-Modified-Since", lastModified.toUtf8());
    }

    m_networkManager.get(request);
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
                QSettings settings("ClassicBooks", "AudiobookHub");
                settings.setValue("gutenberg_last_modified", QString::fromUtf8(lastModified));
            }

            // Save to temp file
            QTemporaryDir tempDir;
            tempDir.setAutoRemove(false); // We will manage cleanup manually
            m_tempArchiveDir = tempDir.path();
            m_archivePath = QDir(m_tempArchiveDir).filePath("rdf-files.tar.bz2");
            m_extractDir = QDir(m_tempArchiveDir).filePath("extracted_rdf");
            QDir().mkpath(m_extractDir);

            QFile file(m_archivePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                
                // Start extraction process
                qDebug() << "GutenbergAdapter: Extracting archive at" << m_archivePath;
                m_extractProcess = new QProcess(this);
                connect(m_extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                        this, &GutenbergAdapter::onExtractionFinished);
                
                m_extractProcess->setWorkingDirectory(m_tempArchiveDir);
                // Extract into the extracted_rdf folder by changing directory first or using tar -C
                m_extractProcess->start("tar", QStringList() << "-xjf" << "rdf-files.tar.bz2" << "-C" << "extracted_rdf");
            } else {
                emit fetchCompleted(false, "Failed to save downloaded archive.");
                tempDir.remove();
            }
        } else if (statusCode == 304) {
            // Not Modified
            qDebug() << "GutenbergAdapter: RDF catalog is up-to-date (304 Not Modified). No changes needed.";
            emit fetchCompleted(true);
        } else {
            emit fetchCompleted(false, QString("Unexpected HTTP status code: %1").arg(statusCode));
        }
    } else if (reply->error() == QNetworkReply::ContentAccessDenied && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 304) {
        qDebug() << "GutenbergAdapter: RDF catalog is up-to-date (304 Not Modified). No changes needed.";
        emit fetchCompleted(true);
    } else {
        qDebug() << "GutenbergAdapter: Network error:" << reply->errorString();
        emit fetchCompleted(false, reply->errorString());
    }
}

void GutenbergAdapter::onExtractionFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_extractProcess->deleteLater();
    m_extractProcess = nullptr;

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        qDebug() << "GutenbergAdapter: Extraction successful. Starting RDF parsing...";
        
        // Parse the extracted files synchronously in this background thread
        parseExtractedRdfFiles(m_extractDir);
        
        qDebug() << "GutenbergAdapter: Parsing complete.";
        emit fetchCompleted(true);
    } else {
        emit fetchCompleted(false, "Failed to extract tar.bz2 archive.");
    }
    
    // Clean up temporary directory
    if (!m_tempArchiveDir.isEmpty()) {
        QDir(m_tempArchiveDir).removeRecursively();
        m_tempArchiveDir.clear();
    }
}

QString GutenbergAdapter::normalizeFormatName(const QString& url, const QString& mimeType) {
    Q_UNUSED(url);
    if (mimeType.isEmpty()) {
        return QString();
    }
    
    QString lowerMime = mimeType.toLower();
    if (lowerMime.contains("image")) {
        return QString();
    }

    if (lowerMime == "application/epub+zip") return "epub";
    if (lowerMime == "text/plain") return "text_plain";
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

void GutenbergAdapter::parseExtractedRdfFiles(const QString& extractDir) {
    QDirIterator it(extractDir, QStringList() << "*.rdf", QDir::Files, QDirIterator::Subdirectories);
    
    QList<DiscoveredBook> batch;
    int parsedCount = 0;
    
    while (it.hasNext()) {
        QString filePath = it.next();
        parseSingleRdf(filePath, batch);
        
        if (batch.size() >= 500) {
            emit booksDiscovered(batch);
            parsedCount += batch.size();
            qDebug() << "GutenbergAdapter: Emitted batch of" << batch.size() << "books. Total:" << parsedCount;
            batch.clear();
        }
    }
    
    if (!batch.isEmpty()) {
        emit booksDiscovered(batch);
        parsedCount += batch.size();
        qDebug() << "GutenbergAdapter: Emitted final batch of" << batch.size() << "books. Total:" << parsedCount;
    }
}

void GutenbergAdapter::parseSingleRdf(const QString& filePath, QList<DiscoveredBook>& batch) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QXmlStreamReader xml(&file);
    DiscoveredBook book;
    
    // Fallback ID from filename if missing in XML
    QFileInfo fileInfo(filePath);
    QString baseName = fileInfo.baseName();
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
            } else if (name == "title") {
                book.title = xml.readElementText().trimmed();
                if (!path.isEmpty()) path.removeLast(); // text read moves past EndElement
            } else if (name == "identifier") {
                rawIdentifiers.append(xml.readElementText().trimmed());
                if (!path.isEmpty()) path.removeLast();
            } else if (name == "name" && path.size() >= 3 && path.at(path.size()-2) == "agent" && path.at(path.size()-3) == "creator") {
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
        if (lower.startsWith("lccn:") || lower.startsWith("http://id.loc.gov/authorities/names/")) {
            // Extract the raw LCCN value and normalize it; store only the digits+alpha, no prefix
            QString lccnNormalized = normalizeLccn(raw);
            // lccnNormalized is "lccn:XYZ" — store just the part after the colon as value
            int colon = lccnNormalized.indexOf(':');
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

    // Post-2001 LCCNs have a 10-digit numeric portion — do not zero-pad those
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
            if (lower.startsWith("lccn:") || lower.startsWith("http://id.loc.gov/authorities/names/")) {
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

} // namespace classic_books::collector
