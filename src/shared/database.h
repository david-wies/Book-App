#pragma once

#include <QString>
#include <optional>

#include <QSqlDatabase>

namespace bookhub::db {

inline constexpr int kSchemaVersion = 8;

/// Returns the absolute path to the database file, located next to the executable.
QString databaseFilePath();

bool initializeDatabase(const QString &filePath, const QString &connectionName = QSqlDatabase::defaultConnection);
bool createSchema(const QString &connectionName = QSqlDatabase::defaultConnection);
bool verifySchemaVersion(const QString &connectionName = QSqlDatabase::defaultConnection);
bool insertSampleData(const QString &connectionName = QSqlDatabase::defaultConnection);

/// Per-adapter catalog sync state, persisted in the `sync_state` table.
/// See docs/design/sync-state-resume.md for the state machine.
struct SyncState {
    QString adapterId;
    QString status;            ///< 'in_progress' | 'completed' | 'failed'
    QString phase;              ///< 'downloading' | 'parsing' | empty
    QString lastModified;       ///< HTTP Last-Modified of the last completed fetch
    QString startedAt;
    QString completedAt;
    qint64  booksProcessed{0};
    QString errorMessage;
    QString downloadUrl;
    QString downloadEtag;
    QString archivePath;
    qint64  bytesDownloaded{0};
    qint64  bytesTotal{0};
    QString lastParsedEntry;
};

/// Returns the sync_state row for `adapterId`, or nullopt if none exists.
std::optional<SyncState> getSyncState(const QString &adapterId,
                                      const QString &connectionName = QSqlDatabase::defaultConnection);

/// Counts books owned by the given adapter prefix (e.g. "gutenberg" → matches
/// book_id LIKE 'gutenberg:%'). Used as the "DB-non-empty" freshness guard.
qint64 countBooksForAdapter(const QString &adapterIdPrefix,
                            const QString &connectionName = QSqlDatabase::defaultConnection);

/// Begin a fresh fetch attempt: status='in_progress', phase='downloading',
/// resets all resume fields. Clobbers any existing row for `adapterId`.
bool beginFetch(const QString &adapterId,
                const QString &downloadUrl,
                const QString &archivePath,
                const QString &connectionName = QSqlDatabase::defaultConnection);

/// Update the download-resume cursor. Cheap; safe to call every few MB.
bool recordDownloadProgress(const QString &adapterId,
                            qint64 bytesDownloaded,
                            qint64 bytesTotal,
                            const QString &downloadEtag,
                            const QString &connectionName = QSqlDatabase::defaultConnection);

/// Transition from downloading→parsing. Clears last_parsed_entry.
bool recordDownloadComplete(const QString &adapterId,
                            qint64 bytesTotal,
                            const QString &connectionName = QSqlDatabase::defaultConnection);

/// After a book batch has committed, advance the parse cursor and counter.
bool recordBatchCommit(const QString &adapterId,
                       const QString &lastParsedEntry,
                       qint64 booksProcessedDelta,
                       const QString &connectionName = QSqlDatabase::defaultConnection);

/// Mark fetch as successfully completed; clears all resume state.
bool completeFetch(const QString &adapterId,
                   const QString &lastModified,
                   const QString &connectionName = QSqlDatabase::defaultConnection);

/// Mark fetch as failed. Leaves resume state intact so the next attempt can pick up.
bool failFetch(const QString &adapterId,
               const QString &errorMessage,
               const QString &connectionName = QSqlDatabase::defaultConnection);

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
