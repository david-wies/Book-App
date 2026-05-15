// Unit tests for the pure-function helpers in shared/database.h that have no
// database dependency: languageNameForCode() and stripFormatTypeSuffix().
//
// These are exercised indirectly by the migration and search-service tests,
// but a direct, side-effect-free suite makes regressions easier to localise
// when the helpers are touched (e.g. when adding ISO language codes).

#include "shared/database.h"

#include <QtTest>

#include <atomic>

using bookhub::db::languageNameForCode;
using bookhub::db::stripFormatTypeSuffix;

class DatabaseHelpersTest : public QObject
{
    Q_OBJECT

private slots:
    void languageNameForCode_mapsTwoLetterCodes();
    void languageNameForCode_mapsThreeLetterCodes();
    void languageNameForCode_isCaseInsensitive();
    void languageNameForCode_distinguishesNorwegianMacroFromBokmal();
    void languageNameForCode_returnsInputForUnknownCode();
    void languageNameForCode_warnsAtMostOncePerUnknownCode();

    void stripFormatTypeSuffix_removesSingleDigitSuffix();
    void stripFormatTypeSuffix_removesMultiDigitSuffix();
    void stripFormatTypeSuffix_leavesBareTypeUnchanged();
    void stripFormatTypeSuffix_leavesNonNumericTailUnchanged();
    void stripFormatTypeSuffix_leavesEmbeddedDigitsUnchanged();
};

void DatabaseHelpersTest::languageNameForCode_mapsTwoLetterCodes()
{
    QCOMPARE(languageNameForCode(QStringLiteral("en")), QStringLiteral("English"));
    QCOMPARE(languageNameForCode(QStringLiteral("fr")), QStringLiteral("French"));
    QCOMPARE(languageNameForCode(QStringLiteral("de")), QStringLiteral("German"));
}

void DatabaseHelpersTest::languageNameForCode_mapsThreeLetterCodes()
{
    QCOMPARE(languageNameForCode(QStringLiteral("eng")), QStringLiteral("English"));
    QCOMPARE(languageNameForCode(QStringLiteral("fra")), QStringLiteral("French"));
    QCOMPARE(languageNameForCode(QStringLiteral("grc")), QStringLiteral("Ancient Greek"));
}

void DatabaseHelpersTest::languageNameForCode_isCaseInsensitive()
{
    QCOMPARE(languageNameForCode(QStringLiteral("EN")),  QStringLiteral("English"));
    QCOMPARE(languageNameForCode(QStringLiteral("Eng")), QStringLiteral("English"));
    QCOMPARE(languageNameForCode(QStringLiteral("DEU")), QStringLiteral("German"));
}

void DatabaseHelpersTest::languageNameForCode_distinguishesNorwegianMacroFromBokmal()
{
    // The macrolanguage code "no"/"nor" must not collapse into the Bokmål
    // variant "nb"/"nob": both rows can coexist in editions and they must
    // surface as distinct strings to avoid UNIQUE-constraint conflicts.
    QCOMPARE(languageNameForCode(QStringLiteral("no")),  QStringLiteral("Norwegian"));
    QCOMPARE(languageNameForCode(QStringLiteral("nor")), QStringLiteral("Norwegian"));
    QCOMPARE(languageNameForCode(QStringLiteral("nb")),  QStringLiteral("Norwegian Bokmål"));
    QCOMPARE(languageNameForCode(QStringLiteral("nob")), QStringLiteral("Norwegian Bokmål"));
    QVERIFY(languageNameForCode(QStringLiteral("no"))
            != languageNameForCode(QStringLiteral("nb")));
}

void DatabaseHelpersTest::languageNameForCode_returnsInputForUnknownCode()
{
    // Unknown codes are echoed back unchanged so the editions row is still
    // displayable; the function also logs a one-shot warning, but that side
    // effect is not asserted here.
    QCOMPARE(languageNameForCode(QStringLiteral("xx")),    QStringLiteral("xx"));
    QCOMPARE(languageNameForCode(QStringLiteral("zzz")),   QStringLiteral("zzz"));
    QCOMPARE(languageNameForCode(QString()),               QString());
}

namespace {
// Test-scoped counter populated by a temporary Qt message handler.  Tracks
// only warnings whose text contains the unique sentinel below, so messages
// from other test code (e.g. QtTest framework warnings) don't pollute the
// count.
std::atomic<int> g_unmappedWarningCount{0};
constexpr const char *kUnmappedSentinel = "languageNameForCode: unmapped ISO code";

void countingHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type == QtWarningMsg && msg.contains(QLatin1String(kUnmappedSentinel)))
        g_unmappedWarningCount.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void DatabaseHelpersTest::languageNameForCode_warnsAtMostOncePerUnknownCode()
{
    // The throttling state lives in a function-local static QSet shared across
    // calls within the process.  Use a code that no other test in this suite
    // exercises so the first call here is guaranteed to be its first hit.
    const QString uniqueCode = QStringLiteral("zq");

    g_unmappedWarningCount.store(0, std::memory_order_relaxed);
    QtMessageHandler previous = qInstallMessageHandler(&countingHandler);

    // First call must warn; subsequent calls (including the case-insensitive
    // variant, which canonicalises to the same throttle key) must be silent.
    // For unknown codes the function echoes the *original* case back, so each
    // call's return value matches its own input.
    QCOMPARE(languageNameForCode(uniqueCode),           uniqueCode);
    QCOMPARE(languageNameForCode(uniqueCode),           uniqueCode);
    QCOMPARE(languageNameForCode(QStringLiteral("ZQ")), QStringLiteral("ZQ"));

    qInstallMessageHandler(previous);
    QCOMPARE(g_unmappedWarningCount.load(std::memory_order_relaxed), 1);
}

void DatabaseHelpersTest::stripFormatTypeSuffix_removesSingleDigitSuffix()
{
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub_1")), QStringLiteral("epub"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("pdf_2")),  QStringLiteral("pdf"));
}

void DatabaseHelpersTest::stripFormatTypeSuffix_removesMultiDigitSuffix()
{
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub_15")),  QStringLiteral("epub"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("plain_999")), QStringLiteral("plain"));
}

void DatabaseHelpersTest::stripFormatTypeSuffix_leavesBareTypeUnchanged()
{
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub")),  QStringLiteral("epub"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("pdf")),   QStringLiteral("pdf"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("plain")), QStringLiteral("plain"));
    QCOMPARE(stripFormatTypeSuffix(QString()),               QString());
}

void DatabaseHelpersTest::stripFormatTypeSuffix_leavesNonNumericTailUnchanged()
{
    // The suffix pattern is anchored to "_<digits>$"; anything else after the
    // underscore must survive unchanged.
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub_x")),    QStringLiteral("epub_x"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("text_plain")), QStringLiteral("text_plain"));
}

void DatabaseHelpersTest::stripFormatTypeSuffix_leavesEmbeddedDigitsUnchanged()
{
    // Only the trailing "_<digits>" run is removed, never digits embedded in
    // the type name itself.
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub3")),    QStringLiteral("epub3"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("html5")),    QStringLiteral("html5"));
    QCOMPARE(stripFormatTypeSuffix(QStringLiteral("epub3_2")),  QStringLiteral("epub3"));
}

QTEST_APPLESS_MAIN(DatabaseHelpersTest)
#include "test_database_helpers.moc"
