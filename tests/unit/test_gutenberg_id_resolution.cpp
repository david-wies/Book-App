#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "collector/gutenberg_adapter.h"

#include <QtTest>
#include <algorithm>

using namespace bookhub::collector;

class GutenbergIdResolutionTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizeLccn_handlesUriAndPadding();
    void resolveBookId_honorsPriorityAndIsbnConversion();
    void resolveBookId_ignoresAuthoritiesNamesUrls();
    void parseSingleRdf_ignoresImagesAndStoresIdentifiers();
    void normalizeFormatName_mapsKnownMimeTypesAndIgnoresImages();
    void parseSingleRdf_noLccnFromAuthoritiesNames();
    void parseSingleRdf_stripsMarc21SubfieldMarkers();
    void parseSingleRdf_titleCollapsesEmbeddedWhitespace();
    void parseSingleRdf_titleIgnoredOutsideEbookContext();
    void parseSingleRdf_languageCodeStoredRaw();
    void parseSingleRdf_multipleFormatsOfSameType_useSuffixedKeys();
};

void GutenbergIdResolutionTest::normalizeLccn_handlesUriAndPadding()
{
    QCOMPARE(GutenbergAdapter::normalizeLccn(QStringLiteral("http://id.loc.gov/authorities/names/n9530321")),
             QStringLiteral("lccn:n09530321"));
    QCOMPARE(GutenbergAdapter::normalizeLccn(QStringLiteral("lccn:2004045630")),
             QStringLiteral("lccn:2004045630"));
}

void GutenbergIdResolutionTest::resolveBookId_honorsPriorityAndIsbnConversion()
{
    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("oclc:1234"), QStringLiteral("1234567890")},
                 QStringLiteral("1342")),
             QStringLiteral("oclc:1234"));

    // /authorities/names/ is a person record — must not be treated as LCCN.
    // OCLC is next in priority and should win here.
    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("http://id.loc.gov/authorities/names/n78095332"),
                  QStringLiteral("oclc:1234"),
                  QStringLiteral("1234567890")},
                 QStringLiteral("1342")),
             QStringLiteral("oclc:1234"));

    // ISBN-10 alone falls back to isbn: after conversion to ISBN-13.
    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("1234567890")},
                 QStringLiteral("1342")),
             QStringLiteral("isbn:9781234567897"));

    QCOMPARE(GutenbergAdapter::resolveBookId({}, QStringLiteral("1342")),
             QStringLiteral("gutenberg:1342"));
}

void GutenbergIdResolutionTest::resolveBookId_ignoresAuthoritiesNamesUrls()
{
    // With only a names-authority URI and no other identifier, must fall back
    // to gutenberg:<id> rather than producing lccn:n... as the book key.
    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("http://id.loc.gov/authorities/names/n78095332")},
                 QStringLiteral("1342")),
             QStringLiteral("gutenberg:1342"));

    // When a valid OCLC number accompanies the names-authority URI, OCLC wins.
    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("http://id.loc.gov/authorities/names/n78095332"),
                  QStringLiteral("oclc:5551234")},
                 QStringLiteral("1342")),
             QStringLiteral("oclc:5551234"));
}

void GutenbergIdResolutionTest::parseSingleRdf_ignoresImagesAndStoresIdentifiers()
{
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;
    const auto hasIdentifier = [](const QList<BookIdentifier> &ids, const QString &type, const QString &value) {
        return std::any_of(ids.begin(), ids.end(), [&](const BookIdentifier &id) {
            return id.type == type && id.value == value;
        });
    };

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/1342">
            <title>Pride and Prejudice</title>
            <creator>
              <agent>
                <name>Jane Austen</name>
              </agent>
            </creator>
            <identifier>http://id.loc.gov/authorities/names/n78095332</identifier>
            <identifier>1234567890</identifier>
            <language><value>en</value></language>
            <subject><value>Romance fiction</value></subject>
            <file rdf:about="https://www.gutenberg.org/ebooks/1342.epub.images">
              <format><value>application/epub+zip</value></format>
            </file>
            <file rdf:about="https://www.gutenberg.org/cache/epub/1342/cover.jpg">
              <format><value>image/jpeg</value></format>
            </file>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/1342/pg1342.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    const DiscoveredBook &book = batch.first();
    QCOMPARE(book.sourceId, QStringLiteral("1342"));
    // n78095332 is Jane Austen's name-authority record, not the work — ISBN wins.
    QCOMPARE(book.resolvedId, QStringLiteral("isbn:9781234567897"));
    QVERIFY(book.formats.contains(QStringLiteral("epub_1")));
    QVERIFY(!book.formats.values().contains(QStringLiteral("https://www.gutenberg.org/cache/epub/1342/cover.jpg")));
    // The /authorities/names/ URI must not produce an 'lccn' identifier entry.
    QVERIFY(!hasIdentifier(book.identifiers, QStringLiteral("lccn"), QStringLiteral("n78095332")));
    QVERIFY(hasIdentifier(book.identifiers, QStringLiteral("isbn"), QStringLiteral("9781234567897")));
    QVERIFY(hasIdentifier(book.identifiers, QStringLiteral("gutenberg"), QStringLiteral("1342")));
}

void GutenbergIdResolutionTest::normalizeFormatName_mapsKnownMimeTypesAndIgnoresImages()
{
    GutenbergAdapter adapter;
    auto normalize = [&adapter](const QString &mime) {
        return adapter.normalizeFormatName(QString(), mime);
    };

    QVERIFY(normalize(QString()).isEmpty());
    QVERIFY(normalize(QStringLiteral("image/jpeg")).isEmpty());
    QVERIFY(normalize(QStringLiteral("image/png")).isEmpty());
    QVERIFY(normalize(QStringLiteral("image/webp")).isEmpty());

    QCOMPARE(normalize(QStringLiteral("application/epub+zip")), QStringLiteral("epub"));
    QCOMPARE(normalize(QStringLiteral("application/pdf")), QStringLiteral("pdf"));
    QCOMPARE(normalize(QStringLiteral("application/x-mobipocket-ebook")), QStringLiteral("mobi"));
    QCOMPARE(normalize(QStringLiteral("text/plain")), QStringLiteral("text_plain"));
    QCOMPARE(normalize(QStringLiteral("text/html")), QStringLiteral("html"));
    QCOMPARE(normalize(QStringLiteral("text/rtf")), QStringLiteral("rtf"));
    QCOMPARE(normalize(QStringLiteral("text/xml")), QStringLiteral("xml"));
    QCOMPARE(normalize(QStringLiteral("application/octet-stream")), QStringLiteral("octet_stream"));

    QCOMPARE(normalize(QStringLiteral("application/x-unknown")), QStringLiteral("x_unknown"));
    QCOMPARE(normalize(QStringLiteral("application/custom+format")), QStringLiteral("custom_format"));
}

void GutenbergIdResolutionTest::parseSingleRdf_noLccnFromAuthoritiesNames()
{
    // A bare /authorities/names/ URI in the RDF identifiers list must not produce
    // an 'lccn' entry in book.identifiers, and resolvedId must not start with "lccn:".
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/1342">
            <title>Pride and Prejudice</title>
            <identifier>http://id.loc.gov/authorities/names/n78095332</identifier>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/1342/pg1342.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    const DiscoveredBook &book = batch.first();
    QVERIFY(!book.resolvedId.startsWith(QStringLiteral("lccn:")));
    QCOMPARE(book.resolvedId, QStringLiteral("gutenberg:1342"));

    const bool hasLccn = std::any_of(book.identifiers.begin(), book.identifiers.end(),
        [](const BookIdentifier &id) { return id.type == QStringLiteral("lccn"); });
    QVERIFY(!hasLccn);
}

void GutenbergIdResolutionTest::parseSingleRdf_stripsMarc21SubfieldMarkers()
{
    // Gutenberg RDF sometimes stores MARC 21 subfield markers verbatim in title
    // strings.  Two common forms:
    // (a) bare marker:  "Main title $b Subtitle"
    // (b) with MARC punctuation before the marker: "Main title : $b Subtitle"
    // Both must produce "Main title: Subtitle" with a single colon, not ":: ".
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    // Case (a): bare $b marker — no preceding MARC punctuation
    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/2350">
            <title>His Last Bow $b Some Later Reminiscences of Sherlock Holmes</title>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/2350/pg2350.rdf"), batch);
    QCOMPARE(batch.size(), 1);
    QCOMPARE(batch.first().title,
             QStringLiteral("His Last Bow: Some Later Reminiscences of Sherlock Holmes"));

    // Case (b): MARC punctuation (" : ") already present before $b — must not produce "::"
    batch.clear();
    const QByteArray rdf2 = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/2350">
            <title>His Last Bow : $b Some Later Reminiscences of Sherlock Holmes</title>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf2, QStringLiteral("cache/epub/2350/pg2350.rdf"), batch);
    QCOMPARE(batch.size(), 1);
    QCOMPARE(batch.first().title,
             QStringLiteral("His Last Bow: Some Later Reminiscences of Sherlock Holmes"));
}

void GutenbergIdResolutionTest::parseSingleRdf_titleCollapsesEmbeddedWhitespace()
{
    // Gutenberg RDF entries sometimes store titles with embedded newlines and
    // leading whitespace on continuation lines.  simplified() must collapse
    // all interior whitespace to single spaces.
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/2350">
            <title>His Last Bow:
      Some Later Reminiscences of Sherlock Holmes</title>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/2350/pg2350.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    QCOMPARE(batch.first().title,
             QStringLiteral("His Last Bow: Some Later Reminiscences of Sherlock Holmes"));
}

void GutenbergIdResolutionTest::parseSingleRdf_titleIgnoredOutsideEbookContext()
{
    // A <title> element that is not a direct child of <ebook> must not
    // overwrite the real book title.  This guards against hypothetical RDF
    // structures where a secondary description element also carries a <title>.
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/1342">
            <title>Pride and Prejudice</title>
            <description>
              <title>Ignored nested title</title>
            </description>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/1342/pg1342.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    QCOMPARE(batch.first().title, QStringLiteral("Pride and Prejudice"));
}

void GutenbergIdResolutionTest::parseSingleRdf_languageCodeStoredRaw()
{
    // The adapter stores whatever language string appears in the RDF verbatim.
    // ISO code normalisation happens later in BookDiscoveryService, not here.
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/9999">
            <title>A Dutch Book</title>
            <language><value>nl</value></language>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/9999/pg9999.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    QCOMPARE(batch.first().languages, QStringList{QStringLiteral("nl")});
}

void GutenbergIdResolutionTest::parseSingleRdf_multipleFormatsOfSameType_useSuffixedKeys()
{
    // When a book has two files with the same MIME type (e.g. epub with images
    // and epub without), the adapter assigns _1 / _2 suffixed keys so neither
    // URL is lost before BookDiscoveryService merges them into the DB.
    GutenbergAdapter adapter;
    QList<DiscoveredBook> batch;

    const QByteArray rdf = R"(
        <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
          <ebook rdf:about="https://www.gutenberg.org/ebooks/1342">
            <title>Pride and Prejudice</title>
            <file rdf:about="https://example.test/1342.epub.images">
              <format><value>application/epub+zip</value></format>
            </file>
            <file rdf:about="https://example.test/1342.epub.noimages">
              <format><value>application/epub+zip</value></format>
            </file>
          </ebook>
        </rdf:RDF>
    )";

    adapter.parseSingleRdf(rdf, QStringLiteral("cache/epub/1342/pg1342.rdf"), batch);

    QCOMPARE(batch.size(), 1);
    const QMap<QString, QString> &fmts = batch.first().formats;
    QVERIFY(fmts.contains(QStringLiteral("epub_1")));
    QVERIFY(fmts.contains(QStringLiteral("epub_2")));
    QCOMPARE(fmts.value(QStringLiteral("epub_1")),
             QStringLiteral("https://example.test/1342.epub.images"));
    QCOMPARE(fmts.value(QStringLiteral("epub_2")),
             QStringLiteral("https://example.test/1342.epub.noimages"));
}

QTEST_GUILESS_MAIN(GutenbergIdResolutionTest)

#include "test_gutenberg_id_resolution.moc"
