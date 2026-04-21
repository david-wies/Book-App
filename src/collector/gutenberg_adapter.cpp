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
    QString formatName = "UNKNOWN";
    QString lowerUrl = url.toLower();
    QString lowerMime = mimeType.toLower();

    if (lowerMime.contains("epub") || lowerUrl.endsWith(".epub")) formatName = "EPUB";
    else if (lowerMime.contains("pdf") || lowerUrl.endsWith(".pdf")) formatName = "PDF";
    else if (lowerMime.contains("html") || lowerUrl.endsWith(".html") || lowerUrl.endsWith(".htm")) formatName = "HTML";
    else if (lowerMime.contains("plain") || lowerUrl.endsWith(".txt")) formatName = "TXT";
    else if (lowerMime.contains("mobipocket") || lowerUrl.endsWith(".mobi")) formatName = "MOBI";
    else if (lowerMime.contains("zip") || lowerUrl.endsWith(".zip")) formatName = "ZIP";
    else if (lowerUrl.endsWith(".jpg") || lowerUrl.endsWith(".jpeg")) formatName = "COVER";
    
    return formatName;
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

    QString currentTag;
    QString currentParentTag;
    QString currentFormatUrl;
    QString currentFormatMime;
    bool inCreator = false;
    
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        
        if (token == QXmlStreamReader::StartElement) {
            QString name = xml.name().toString();
            
            if (name == "ebook") {
                QString about = xml.attributes().value("rdf:about").toString();
                QRegularExpression re("ebooks/(\\d+)");
                QRegularExpressionMatch match = re.match(about);
                if (match.hasMatch()) {
                    book.sourceId = match.captured(1);
                }
            } else if (name == "creator") {
                inCreator = true;
            } else if (name == "title") {
                book.title = xml.readElementText().trimmed();
            } else if (name == "name" && inCreator) {
                book.authors.append(xml.readElementText().trimmed());
            } else if (name == "value") {
                if (currentParentTag == "subject") {
                    book.subjects.append(xml.readElementText().trimmed());
                } else if (currentParentTag == "language") {
                    book.languages.append(xml.readElementText().trimmed());
                } else if (currentParentTag == "format") {
                    currentFormatMime = xml.readElementText().trimmed();
                }
            } else if (name == "file") {
                currentFormatUrl = xml.attributes().value("rdf:about").toString();
                currentFormatMime.clear();
            }
            
            if (name == "subject" || name == "language" || name == "format") {
                currentParentTag = name;
            }
            
        } else if (token == QXmlStreamReader::EndElement) {
            QString name = xml.name().toString();
            if (name == "creator") {
                inCreator = false;
            } else if (name == "subject" || name == "language" || name == "format") {
                currentParentTag.clear();
            } else if (name == "file") {
                if (!currentFormatUrl.isEmpty()) {
                    QString formatName = normalizeFormatName(currentFormatUrl, currentFormatMime);
                    if (formatName != "UNKNOWN" && formatName != "COVER" && formatName != "ZIP") {
                        book.formats[formatName] = currentFormatUrl;
                    }
                    currentFormatUrl.clear();
                    currentFormatMime.clear();
                }
            }
        }
    }

    if (!book.title.isEmpty() && !book.sourceId.isEmpty()) {
        batch.append(book);
    }
}

} // namespace classic_books::collector
