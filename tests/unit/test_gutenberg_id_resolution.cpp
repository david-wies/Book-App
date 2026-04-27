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
    void parseSingleRdf_ignoresImagesAndStoresIdentifiers();
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

    QCOMPARE(GutenbergAdapter::resolveBookId(
                 {QStringLiteral("http://id.loc.gov/authorities/names/n78095332"),
                  QStringLiteral("oclc:1234"),
                  QStringLiteral("1234567890")},
                 QStringLiteral("1342")),
             QStringLiteral("lccn:n78095332"));

    QCOMPARE(GutenbergAdapter::resolveBookId({}, QStringLiteral("1342")),
             QStringLiteral("gutenberg:1342"));
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
    QCOMPARE(book.resolvedId, QStringLiteral("lccn:n78095332"));
    QVERIFY(book.formats.contains(QStringLiteral("epub_1")));
    QVERIFY(!book.formats.values().contains(QStringLiteral("https://www.gutenberg.org/cache/epub/1342/cover.jpg")));
    QVERIFY(hasIdentifier(book.identifiers, QStringLiteral("lccn"), QStringLiteral("n78095332")));
    QVERIFY(hasIdentifier(book.identifiers, QStringLiteral("isbn"), QStringLiteral("9781234567897")));
    QVERIFY(hasIdentifier(book.identifiers, QStringLiteral("gutenberg"), QStringLiteral("1342")));
}

QTEST_GUILESS_MAIN(GutenbergIdResolutionTest)

#include "test_gutenberg_id_resolution.moc"
