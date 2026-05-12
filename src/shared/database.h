#pragma once

#include <QString>

#include <QSqlDatabase>

namespace bookhub::db {

inline constexpr int kSchemaVersion = 7;

/// Returns the absolute path to the database file, located next to the executable.
QString databaseFilePath();

bool initializeDatabase(const QString &filePath, const QString &connectionName = QSqlDatabase::defaultConnection);
bool createSchema(const QString &connectionName = QSqlDatabase::defaultConnection);
bool verifySchemaVersion(const QString &connectionName = QSqlDatabase::defaultConnection);
bool insertSampleData(const QString &connectionName = QSqlDatabase::defaultConnection);

/// Maps an ISO 639-1 (2-letter) or ISO 639-3 (3-letter) language code to a
/// display-friendly name derived from the ISO 639-3 reference names
/// (e.g. "en" → "English", "nob" → "Norwegian Bokmål", "grc" → "Ancient Greek").
/// Year qualifiers from ISO 639-3 reference names are omitted for readability.
/// Returns the input unchanged if the code is not in the table.
/// Source: https://iso639-3.sil.org/sites/iso639-3/files/downloads/iso-639-3.tab
QString languageNameForCode(const QString &isoCode);

/// Strips the _N de-collision suffix that the Gutenberg adapter appends to
/// format_type keys when a book has multiple files of the same MIME type
/// (e.g. "epub_1" → "epub", "epub_2" → "epub"). Used both at insertion time
/// (BookDiscoveryService) and at display time (BookDetailsPanel).
QString stripFormatTypeSuffix(const QString &key);

} // namespace bookhub::db
