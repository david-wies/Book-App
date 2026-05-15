#include "database.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

namespace bookhub::db {

static const QString kDatabaseFileName = QStringLiteral("bookhub.db");

QString databaseFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(dir);
    return QDir(dir).filePath(kDatabaseFileName);
}

bool initializeDatabase(const QString &filePath, const QString &connectionName)
{
    if (QSqlDatabase::contains(connectionName)) {
        return true;
    }

    auto db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(filePath);

    if (!db.open()) {
        qWarning() << "Failed to open SQLite database:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec("PRAGMA foreign_keys = ON;")) {
        qWarning() << "Failed to enable foreign keys:" << query.lastError().text();
    }

    // WAL mode allows the GUI thread to read while the collector holds a write
    // lock, preventing UI stalls during background discovery runs.
    if (!query.exec("PRAGMA journal_mode = WAL;")) {
        qWarning() << "Failed to enable WAL journal mode:" << query.lastError().text();
    }

    // busy_timeout makes SQLite block up to 5 s for a write lock instead of
    // failing immediately with SQLITE_BUSY.  WAL serialises writers across
    // connections (collector + GUI's library mutations both write), so brief
    // contention is expected during a large catalog ingest.
    if (!query.exec("PRAGMA busy_timeout = 5000;")) {
        qWarning() << "Failed to set busy_timeout:" << query.lastError().text();
    }

    qDebug() << "Opened SQLite database at" << filePath << "with connection:" << connectionName;
    return true;
}

bool createSchema(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList tables = {
        R"(CREATE TABLE IF NOT EXISTS books (
            book_id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            author TEXT,
            publish_year INTEGER,
            summary TEXT
        ))",
        R"(CREATE TABLE IF NOT EXISTS book_identifiers (
            book_id TEXT NOT NULL REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            type    TEXT NOT NULL,
            value   TEXT NOT NULL,
            UNIQUE(type, value)
        ))",
        R"(CREATE TABLE IF NOT EXISTS editions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id TEXT NOT NULL,
            language TEXT NOT NULL,
            publisher TEXT,
            publish_date TEXT,
            UNIQUE (book_id, language),
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS genres (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            genre_name TEXT UNIQUE NOT NULL
        ))",
        R"(CREATE TABLE IF NOT EXISTS book_genres (
            book_id TEXT,
            genre_id INTEGER,
            PRIMARY KEY (book_id, genre_id),
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (genre_id) REFERENCES genres(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS formats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            edition_id INTEGER NOT NULL,
            format_type TEXT NOT NULL,
            UNIQUE(edition_id, format_type),
            FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS sources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            format_id INTEGER NOT NULL,
            source_name TEXT NOT NULL,
            download_link TEXT NOT NULL,
            UNIQUE(format_id, source_name),
            FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS library_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id TEXT NOT NULL UNIQUE,
            edition_id INTEGER,
            added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            status TEXT CHECK(status IS NULL OR status IN ('saved', 'downloading', 'downloaded', 'converting', 'audiobook_ready', 'error')),
            FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (edition_id) REFERENCES editions(id)
        ))",
        R"(CREATE TABLE IF NOT EXISTS voices (
            id                   INTEGER PRIMARY KEY AUTOINCREMENT,
            name                 TEXT NOT NULL UNIQUE,
            type                 TEXT NOT NULL CHECK(type IN ('preset', 'custom')),
            engine               TEXT NOT NULL CHECK(engine IN ('sherpa_onnx', 'pocket_tts')),
            model_path           TEXT,
            config_path          TEXT,
            reference_audio_path TEXT,
            created_at           TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ))",
        // Per-adapter catalog sync state. See docs/design/sync-state-resume.md.
        // Resume cursors (download_*, archive_path, bytes_*, last_parsed_entry)
        // are meaningful only while status='in_progress' and are cleared on
        // successful completion.
        R"(CREATE TABLE IF NOT EXISTS sync_state (
            adapter_id        TEXT PRIMARY KEY,
            status            TEXT NOT NULL
                              CHECK(status IN ('in_progress', 'completed', 'failed')),
            phase             TEXT
                              CHECK(phase IS NULL OR phase IN ('downloading', 'parsing')),
            last_modified     TEXT,
            validator_type    TEXT
                              CHECK(validator_type IS NULL
                                    OR validator_type IN ('last_modified', 'etag')),
            started_at        TEXT,
            completed_at      TEXT,
            books_processed   INTEGER NOT NULL DEFAULT 0,
            error_message     TEXT,
            download_url      TEXT,
            download_etag     TEXT,
            archive_path      TEXT,
            bytes_downloaded  INTEGER NOT NULL DEFAULT 0,
            bytes_total       INTEGER NOT NULL DEFAULT 0,
            last_parsed_entry TEXT
        ))"
    };

    for (const QString &sql : tables) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to create table:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }

    // Seed the three preset voices. These are product baseline data (not dev
    // sample data), so they live here rather than in insertSampleData so
    // production Release builds — which skip insertSampleData — still get them.
    // UNIQUE(name) on the voices table makes this genuinely idempotent.
    if (!query.exec(QStringLiteral(
            "INSERT OR IGNORE INTO voices (name, type, engine) VALUES "
            "('Classic Storyteller', 'preset', 'sherpa_onnx'),"
            "('Warm Listener',       'preset', 'sherpa_onnx'),"
            "('Crisp Narrator',      'preset', 'sherpa_onnx')"))) {
        qWarning() << "Failed to seed preset voices:" << query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion))) {
        qWarning() << "Failed to set user_version:" << query.lastError().text();
        return false;
    }

    qDebug() << "Database schema created successfully.";
    return true;
}

QString languageNameForCode(const QString &isoCode)
{
    // ISO 639-1 (2-letter) and ISO 639-3 (3-letter) → ISO 639-3 Ref_Name.
    // Source: https://iso639-3.sil.org/sites/iso639-3/files/downloads/iso-639-3.tab
    // Both "no" (Norwegian macrolanguage, nor) and "nb" (Norwegian Bokmål, nob) are
    // mapped to their own distinct ISO 639-3 reference names to avoid UNIQUE conflicts
    // in the editions table — they resolve to "Norwegian" and "Norwegian Bokmål"
    // respectively, not the same string as QLocale::languageToString() would produce.
    static const QHash<QString, QString> kMap = {
        // 2-letter ISO 639-1 codes
        {QStringLiteral("af"), QStringLiteral("Afrikaans")},
        {QStringLiteral("ar"), QStringLiteral("Arabic")},
        {QStringLiteral("bg"), QStringLiteral("Bulgarian")},
        {QStringLiteral("br"), QStringLiteral("Breton")},
        {QStringLiteral("ca"), QStringLiteral("Catalan")},
        {QStringLiteral("cs"), QStringLiteral("Czech")},
        {QStringLiteral("cy"), QStringLiteral("Welsh")},
        {QStringLiteral("da"), QStringLiteral("Danish")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("el"), QStringLiteral("Modern Greek")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("eo"), QStringLiteral("Esperanto")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("fi"), QStringLiteral("Finnish")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("fy"), QStringLiteral("Western Frisian")},
        {QStringLiteral("ga"), QStringLiteral("Irish")},
        {QStringLiteral("gl"), QStringLiteral("Galician")},
        {QStringLiteral("he"), QStringLiteral("Hebrew")},
        {QStringLiteral("hr"), QStringLiteral("Croatian")},
        {QStringLiteral("hu"), QStringLiteral("Hungarian")},
        {QStringLiteral("is"), QStringLiteral("Icelandic")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("la"), QStringLiteral("Latin")},
        {QStringLiteral("lt"), QStringLiteral("Lithuanian")},
        {QStringLiteral("mi"), QStringLiteral("Māori")},
        {QStringLiteral("nb"), QStringLiteral("Norwegian Bokmål")},
        {QStringLiteral("nl"), QStringLiteral("Dutch")},
        {QStringLiteral("nn"), QStringLiteral("Norwegian Nynorsk")},
        {QStringLiteral("no"), QStringLiteral("Norwegian")},
        {QStringLiteral("oc"), QStringLiteral("Occitan")},
        {QStringLiteral("pl"), QStringLiteral("Polish")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese")},
        {QStringLiteral("ro"), QStringLiteral("Romanian")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("sk"), QStringLiteral("Slovak")},
        {QStringLiteral("sl"), QStringLiteral("Slovenian")},
        {QStringLiteral("sr"), QStringLiteral("Serbian")},
        {QStringLiteral("sv"), QStringLiteral("Swedish")},
        {QStringLiteral("tl"), QStringLiteral("Tagalog")},
        {QStringLiteral("uk"), QStringLiteral("Ukrainian")},
        {QStringLiteral("yi"), QStringLiteral("Yiddish")},
        {QStringLiteral("zh"), QStringLiteral("Chinese")},
        // 3-letter ISO 639-3 codes
        {QStringLiteral("afr"), QStringLiteral("Afrikaans")},
        {QStringLiteral("ara"), QStringLiteral("Arabic")},
        {QStringLiteral("bre"), QStringLiteral("Breton")},
        {QStringLiteral("bul"), QStringLiteral("Bulgarian")},
        {QStringLiteral("cat"), QStringLiteral("Catalan")},
        {QStringLiteral("ces"), QStringLiteral("Czech")},
        {QStringLiteral("cym"), QStringLiteral("Welsh")},
        {QStringLiteral("dan"), QStringLiteral("Danish")},
        {QStringLiteral("deu"), QStringLiteral("German")},
        {QStringLiteral("ell"), QStringLiteral("Modern Greek")},
        {QStringLiteral("eng"), QStringLiteral("English")},
        {QStringLiteral("epo"), QStringLiteral("Esperanto")},
        {QStringLiteral("fin"), QStringLiteral("Finnish")},
        {QStringLiteral("fra"), QStringLiteral("French")},
        {QStringLiteral("fry"), QStringLiteral("Western Frisian")},
        {QStringLiteral("gle"), QStringLiteral("Irish")},
        {QStringLiteral("glg"), QStringLiteral("Galician")},
        {QStringLiteral("grc"), QStringLiteral("Ancient Greek")},
        {QStringLiteral("heb"), QStringLiteral("Hebrew")},
        {QStringLiteral("hrv"), QStringLiteral("Croatian")},
        {QStringLiteral("hun"), QStringLiteral("Hungarian")},
        {QStringLiteral("isl"), QStringLiteral("Icelandic")},
        {QStringLiteral("ita"), QStringLiteral("Italian")},
        {QStringLiteral("jpn"), QStringLiteral("Japanese")},
        {QStringLiteral("lat"), QStringLiteral("Latin")},
        {QStringLiteral("lit"), QStringLiteral("Lithuanian")},
        {QStringLiteral("mri"), QStringLiteral("Māori")},
        {QStringLiteral("nld"), QStringLiteral("Dutch")},
        {QStringLiteral("nno"), QStringLiteral("Norwegian Nynorsk")},
        {QStringLiteral("nob"), QStringLiteral("Norwegian Bokmål")},
        {QStringLiteral("nor"), QStringLiteral("Norwegian")},
        {QStringLiteral("oci"), QStringLiteral("Occitan")},
        {QStringLiteral("pol"), QStringLiteral("Polish")},
        {QStringLiteral("por"), QStringLiteral("Portuguese")},
        {QStringLiteral("ron"), QStringLiteral("Romanian")},
        {QStringLiteral("rus"), QStringLiteral("Russian")},
        {QStringLiteral("slk"), QStringLiteral("Slovak")},
        {QStringLiteral("slv"), QStringLiteral("Slovenian")},
        {QStringLiteral("spa"), QStringLiteral("Spanish")},
        {QStringLiteral("srp"), QStringLiteral("Serbian")},
        {QStringLiteral("swe"), QStringLiteral("Swedish")},
        {QStringLiteral("tgl"), QStringLiteral("Tagalog")},
        {QStringLiteral("ukr"), QStringLiteral("Ukrainian")},
        {QStringLiteral("yid"), QStringLiteral("Yiddish")},
        {QStringLiteral("zho"), QStringLiteral("Chinese")},
    };
    const QString lower = isoCode.toLower();
    const auto it = kMap.constFind(lower);
    if (it == kMap.constEnd()) {
        // One-shot warning per unmapped code.  This function is on the hot path
        // for search-filter population and book-details rendering, so logging
        // every call would spam the console when the database holds rows in a
        // language we haven't mapped yet.
        static QSet<QString> warned;
        static QMutex warnMutex;
        QMutexLocker lock(&warnMutex);
        if (!warned.contains(lower)) {
            warned.insert(lower);
            qWarning() << "languageNameForCode: unmapped ISO code" << isoCode << "— extend kMap";
        }
    }
    return (it != kMap.constEnd()) ? *it : isoCode;
}

QString stripFormatTypeSuffix(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("_\\d+$"));
    QString result = key;
    result.remove(re);
    return result;
}

bool verifySchemaVersion(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.exec("PRAGMA user_version");
    if (!q.next()) {
        qCritical("Failed to read PRAGMA user_version");
        return false;
    }
    int version = q.value(0).toInt();
    if (version == kSchemaVersion)
        return true;

    // version 0 covers two cases:
    // 1. Fresh DB with no tables — proceed; createSchema() sets the version next.
    // 2. Legacy pre-versioned schema with a books table present — fall through to
    //    mismatch handling below so the user is prompted to migrate.
    if (version == 0) {
        QSqlQuery tableCheck(db);
        tableCheck.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='books'");
        if (!tableCheck.next())
            return true; // no tables yet — fresh DB
    }

    // Migration: version 1 → 2
    // Adds UNIQUE(edition_id, format_type) on formats,
    // UNIQUE(format_id, source_name) on sources, and UNIQUE(book_id) on
    // library_items. SQLite does not support ADD CONSTRAINT, so we recreate
    // each table using the standard rename-insert-drop pattern.
    if (version == 1) {
        qDebug() << "Migrating database schema from version 1 to 2...";
        QStringList migration = {
            // Must be outside a transaction; disables FK checks during table recreation.
            "PRAGMA foreign_keys = OFF",
            "BEGIN",
            R"(CREATE TABLE formats_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                edition_id INTEGER NOT NULL,
                format_type TEXT NOT NULL,
                UNIQUE(edition_id, format_type),
                FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
            ))",
            "INSERT OR IGNORE INTO formats_new SELECT * FROM formats",
            "DROP TABLE formats",
            "ALTER TABLE formats_new RENAME TO formats",
            R"(CREATE TABLE sources_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                format_id INTEGER NOT NULL,
                source_name TEXT NOT NULL,
                download_link TEXT NOT NULL,
                UNIQUE(format_id, source_name),
                FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
            ))",
            "INSERT OR IGNORE INTO sources_new SELECT * FROM sources",
            "DROP TABLE sources",
            "ALTER TABLE sources_new RENAME TO sources",
            R"(CREATE TABLE library_items_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                book_id TEXT NOT NULL UNIQUE,
                edition_id INTEGER,
                added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                status TEXT,
                FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
                FOREIGN KEY (edition_id) REFERENCES editions(id)
            ))",
            "INSERT OR IGNORE INTO library_items_new SELECT * FROM library_items",
            "DROP TABLE library_items",
            "ALTER TABLE library_items_new RENAME TO library_items",
            // Stamp the version this migration produces, not kSchemaVersion —
            // otherwise bumping the schema later (e.g. v3, v4, …) would cause a
            // v1 DB to skip every intermediate migration step.
            QStringLiteral("PRAGMA user_version = 2"),
            "COMMIT",
            "PRAGMA foreign_keys = ON"
        };

        QSqlQuery mq(db);
        for (const QString &sql : migration) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v1→v2 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }
        qDebug() << "Migration to schema version 2 complete.";
        // Chain into the next migration so a v1 DB lands at the current
        // schema version in a single startup.
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 2 → 3
    // Adds voices table for preset and custom voice storage, and a CHECK
    // constraint on library_items.status so typos cannot silently land bad data.
    if (version == 2) {
        qDebug() << "Migrating database schema from version 2 to 3...";
        QStringList migration = {
            // PRAGMA must run outside a transaction; FK checks are disabled
            // during the table-recreation dance for library_items.
            "PRAGMA foreign_keys = OFF",
            "BEGIN",
            R"(CREATE TABLE IF NOT EXISTS voices (
                id                   INTEGER PRIMARY KEY AUTOINCREMENT,
                name                 TEXT NOT NULL UNIQUE,
                type                 TEXT NOT NULL CHECK(type IN ('preset', 'custom')),
                engine               TEXT NOT NULL CHECK(engine IN ('sherpa_onnx', 'pocket_tts')),
                model_path           TEXT,
                config_path          TEXT,
                reference_audio_path TEXT,
                created_at           TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            ))",
            // Seed preset voices here so users upgrading from v2 get them
            // immediately, without waiting for another createSchema() call.
            QStringLiteral("INSERT OR IGNORE INTO voices (name, type, engine) VALUES "
                           "('Classic Storyteller', 'preset', 'sherpa_onnx'),"
                           "('Warm Listener',       'preset', 'sherpa_onnx'),"
                           "('Crisp Narrator',      'preset', 'sherpa_onnx')"),
            // Recreate library_items with CHECK constraint on status.
            // SQLite does not support ALTER TABLE ADD CONSTRAINT.
            R"(CREATE TABLE library_items_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                book_id TEXT NOT NULL UNIQUE,
                edition_id INTEGER,
                added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                status TEXT CHECK(status IS NULL OR status IN ('saved', 'downloading', 'downloaded', 'converting', 'audiobook_ready', 'error')),
                FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
                FOREIGN KEY (edition_id) REFERENCES editions(id)
            ))",
            // Fail loudly if any existing status value violates the new constraint
            // rather than silently discarding rows.
            "INSERT INTO library_items_new SELECT * FROM library_items",
            "DROP TABLE library_items",
            "ALTER TABLE library_items_new RENAME TO library_items",
            // Hard-code the version this migration produces so a future v3→v4
            // migration is not silently skipped by a stale kSchemaVersion stamp.
            QStringLiteral("PRAGMA user_version = 3"),
            "COMMIT",
            "PRAGMA foreign_keys = ON"
        };

        QSqlQuery mq(db);
        for (const QString &sql : migration) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v2→v3 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }
        qDebug() << "Migration to schema version 3 complete.";
        // Chain into the next migration so a single startup lands at kSchemaVersion.
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 3 → 4
    // Normalises language codes stored as ISO 639-1/2/3 ("en", "fr", "nob", …)
    // to ISO 639-3 reference names ("English", "French", "Norwegian Bokmål", …).
    //
    // Implemented as a C++ loop using languageNameForCode() so the migration always
    // produces the same strings as the runtime normalisation functions, with no
    // risk of hardcoded-name vs QLocale drift.  Per-edition processing also handles
    // the UNIQUE(book_id, language) constraint correctly:
    //   - If a full-name edition already exists (or another ISO code in this batch
    //     already claimed the slot), library_items are redirected to the surviving
    //     edition and the duplicate is deleted along with its formats and sources.
    //   - UPDATE OR IGNORE is used so FK-off + transactional isolation prevent any
    //     constraint failure from aborting the migration.
    if (version == 3) {
        qDebug() << "Migrating database schema from version 3 to 4...";

        QSqlQuery mq(db);
        if (!mq.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))
                || !mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v3→v4 preamble failed:" << mq.lastError().text();
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        // Collect all editions whose language looks like a short ISO code.
        QSqlQuery edSelect(db);
        if (!edSelect.exec(QStringLiteral(
                "SELECT id, book_id, language FROM editions "
                "WHERE length(language) <= 3 AND language NOT LIKE '% %'"))) {
            qCritical() << "Migration v3→v4: failed to read editions:"
                        << edSelect.lastError().text();
            mq.exec("ROLLBACK");
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        struct EdRemap { int id; QString bookId; QString newName; };
        QList<EdRemap> remaps;
        while (edSelect.next()) {
            const QString code = edSelect.value(2).toString();
            const QString name = languageNameForCode(code);
            if (name == code) continue;
            remaps.append({edSelect.value(0).toInt(),
                           edSelect.value(1).toString(),
                           name});
        }

        for (const auto &r : remaps) {
            // Helper lambda: redirect library_items to targetId, then delete
            // sources/formats/edition for the ISO-coded edition row r.id.
            auto mergeInto = [&](int targetId) -> bool {
                QSqlQuery q(db);
                // Remove any old-edition rows whose book_id already has a row under
                // targetId — a plain UPDATE would produce a duplicate without this.
                q.prepare(QStringLiteral(
                    "DELETE FROM library_items WHERE edition_id = ? "
                    "AND book_id IN (SELECT book_id FROM library_items WHERE edition_id = ?)"));
                q.addBindValue(r.id);
                q.addBindValue(targetId);
                q.exec();
                q.prepare(QStringLiteral(
                    "UPDATE library_items SET edition_id = ? WHERE edition_id = ?"));
                q.addBindValue(targetId);
                q.addBindValue(r.id);
                if (!q.exec()) {
                    qWarning() << "Migration v3→v4: library_items redirect failed:"
                               << q.lastError().text();
                }
                q.prepare(QStringLiteral(
                    "DELETE FROM sources WHERE format_id IN "
                    "(SELECT id FROM formats WHERE edition_id = ?)"));
                q.addBindValue(r.id);
                q.exec();
                q.prepare(QStringLiteral("DELETE FROM formats WHERE edition_id = ?"));
                q.addBindValue(r.id);
                q.exec();
                q.prepare(QStringLiteral("DELETE FROM editions WHERE id = ?"));
                q.addBindValue(r.id);
                if (!q.exec()) {
                    qCritical() << "Migration v3→v4: edition delete failed:"
                                << q.lastError().text();
                    return false;
                }
                return true;
            };

            // Check for an existing full-name edition for this book.
            QSqlQuery existsQ(db);
            existsQ.prepare(QStringLiteral(
                "SELECT id FROM editions WHERE book_id = ? AND language = ?"));
            existsQ.addBindValue(r.bookId);
            existsQ.addBindValue(r.newName);
            if (!existsQ.exec()) {
                qCritical() << "Migration v3→v4: exists check failed:"
                            << existsQ.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }

            if (existsQ.next()) {
                // Full-name edition already present — merge this ISO row into it.
                if (!mergeInto(existsQ.value(0).toInt())) {
                    mq.exec("ROLLBACK");
                    mq.exec("PRAGMA foreign_keys = ON");
                    return false;
                }
            } else {
                // Try to rename in place.
                QSqlQuery upd(db);
                upd.prepare(QStringLiteral(
                    "UPDATE OR IGNORE editions SET language = ? WHERE id = ?"));
                upd.addBindValue(r.newName);
                upd.addBindValue(r.id);
                if (!upd.exec()) {
                    qCritical() << "Migration v3→v4: update failed:"
                                << upd.lastError().text();
                    mq.exec("ROLLBACK");
                    mq.exec("PRAGMA foreign_keys = ON");
                    return false;
                }
                if (upd.numRowsAffected() == 0) {
                    // OR IGNORE absorbed a UNIQUE conflict: another remap in this
                    // batch already claimed (bookId, newName).  Merge into that row.
                    QSqlQuery findQ(db);
                    findQ.prepare(QStringLiteral(
                        "SELECT id FROM editions WHERE book_id = ? AND language = ?"));
                    findQ.addBindValue(r.bookId);
                    findQ.addBindValue(r.newName);
                    findQ.exec();
                    if (findQ.next()) {
                        if (!mergeInto(findQ.value(0).toInt())) {
                            mq.exec("ROLLBACK");
                            mq.exec("PRAGMA foreign_keys = ON");
                            return false;
                        }
                    }
                }
            }
        }

        if (!mq.exec(QStringLiteral("PRAGMA user_version = 4"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v3→v4: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        mq.exec("PRAGMA foreign_keys = ON");
        qDebug() << "Migration to schema version 4 complete.";
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 4 → 5
    // Three concerns:
    //
    // (a) Rename book_ids that were assigned a LOC names-authority LCCN (e.g.
    //     lccn:n78095332, which identifies Jane Austen the person, not the work).
    //     For each such book that has a gutenberg identifier, the book_id is
    //     replaced with gutenberg:<value>.  All FK-constrained tables are updated
    //     inside a single transaction with FK checks disabled.
    //
    // (b) Strip the _N de-collision suffix from format_type values introduced by
    //     earlier versions of the Gutenberg adapter ("epub_1" → "epub", etc.).
    //     _2+ rows whose bare name already exists are deleted as true duplicates.
    //
    // (c) Strip MARC 21 subfield markers (e.g. " $b ") from book titles.
    //     SQLite lacks native regex, so this is done in a C++ loop.
    if (version == 4) {
        qDebug() << "Migrating database schema from version 4 to 5...";

        QSqlQuery mq(db);

        // ----------------------------------------------------------------
        // Part A: remap lccn:n... book_ids to gutenberg:<id> fallback
        // ----------------------------------------------------------------
        const QStringList preA = {
            "PRAGMA foreign_keys = OFF",
            // Build a temp mapping from old book_id to new book_id for every
            // book whose book_id looks like a LOC names-authority LCCN AND that
            // has a gutenberg identifier we can use as the new key.
            //
            // The "n" prefix on an LCCN identifies the Library of Congress
            // Name Authority File (e.g. "n78095332" = Jane Austen the person).
            // Work-level (bibliographic) LCCNs are usually bare numeric, often
            // with a year prefix (e.g. "82-12345" or "2001012345") and never
            // start with "n"; other alphabetic prefixes such as "sh" / "sn"
            // belong to *subject*-authority records (LCSH and Name-Subject) and
            // are also not work-level.  See
            // https://www.loc.gov/marc/lccn-namespace.html for the full
            // prefix list.  Older versions of resolveBookId() promoted
            // names-authority IDs to primary keys; we now remap them back.
            "DROP TABLE IF EXISTS temp.bookid_remap",
            R"(CREATE TEMP TABLE bookid_remap (old_id TEXT PRIMARY KEY, new_id TEXT NOT NULL))",
            R"(INSERT INTO bookid_remap (old_id, new_id)
               SELECT b.book_id,
                      'gutenberg:' || bi.value
                 FROM books b
                 JOIN book_identifiers bi
                   ON bi.book_id = b.book_id AND bi.type = 'gutenberg'
                WHERE b.book_id LIKE 'lccn:n%')",
            "BEGIN",
            // Apply remapping to all FK-constrained tables then books itself.
            R"(UPDATE OR IGNORE book_identifiers
               SET book_id = (SELECT new_id FROM bookid_remap WHERE old_id = book_id)
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(DELETE FROM book_identifiers
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(UPDATE OR IGNORE editions
               SET book_id = (SELECT new_id FROM bookid_remap WHERE old_id = book_id)
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(DELETE FROM editions
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(UPDATE OR IGNORE book_genres
               SET book_id = (SELECT new_id FROM bookid_remap WHERE old_id = book_id)
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(DELETE FROM book_genres
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(UPDATE OR IGNORE library_items
               SET book_id = (SELECT new_id FROM bookid_remap WHERE old_id = book_id)
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(DELETE FROM library_items
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(UPDATE OR IGNORE books
               SET book_id = (SELECT new_id FROM bookid_remap WHERE old_id = book_id)
               WHERE book_id IN (SELECT old_id FROM bookid_remap))",
            R"(DELETE FROM books WHERE book_id IN (SELECT old_id FROM bookid_remap))",
        };

        for (const QString &sql : preA) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v4→v5 (book_id remap) failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("DROP TABLE IF EXISTS temp.bookid_remap");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        // ----------------------------------------------------------------
        // Part B: strip _N suffix from format_type (inside same transaction)
        // ----------------------------------------------------------------
        const QStringList partB = {
            // Delete sources for _2+ format rows first (FK is off, so manual).
            R"(DELETE FROM sources
               WHERE format_id IN (
                   SELECT id FROM formats
                    WHERE format_type GLOB '*_[2-9]'
                       OR format_type GLOB '*_[1-9][0-9]'
               ))",
            // Delete _2+ format rows.
            R"(DELETE FROM formats
               WHERE format_type GLOB '*_[2-9]'
                  OR format_type GLOB '*_[1-9][0-9]')",
            // Rename _1 rows to bare name; OR IGNORE skips conflicts silently.
            R"(UPDATE OR IGNORE formats
               SET format_type = SUBSTR(format_type, 1, LENGTH(format_type) - 2)
               WHERE format_type GLOB '*_1')",
            // Any _1 row still present is a duplicate of a bare row — drop it.
            R"(DELETE FROM formats WHERE format_type GLOB '*_1')",
        };

        for (const QString &sql : partB) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v4→v5 (format suffix) failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("DROP TABLE IF EXISTS temp.bookid_remap");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        // ----------------------------------------------------------------
        // Part C: strip MARC subfield markers from titles (C++ loop)
        // ----------------------------------------------------------------
        {
            QSqlQuery titleSelect(db);
            if (!titleSelect.exec(
                    QStringLiteral("SELECT book_id, title FROM books WHERE title LIKE '%$%'"))) {
                qCritical() << "Migration v4→v5 (MARC title select) failed:"
                            << titleSelect.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("DROP TABLE IF EXISTS temp.bookid_remap");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }

            // Same MARC subfield regex as gutenberg_adapter.cpp parseSingleRdf().
            // The subfield class is intentionally [a-z] (alphabetic MARC subfields
            // only) — see the comment there for the rationale on excluding [0-9].
            static const QRegularExpression reMarc(QStringLiteral("[\\s:;/,]*\\$[a-z]\\s*"));
            QSqlQuery titleUpdate(db);
            titleUpdate.prepare(
                QStringLiteral("UPDATE books SET title = ? WHERE book_id = ?"));

            while (titleSelect.next()) {
                const QString bookId   = titleSelect.value(0).toString();
                const QString rawTitle = titleSelect.value(1).toString();
                QString cleaned = rawTitle;
                cleaned.replace(reMarc, QStringLiteral(": "));
                cleaned = cleaned.simplified();
                if (cleaned == rawTitle)
                    continue;
                // Use positional bindValue so repeated calls on the prepared
                // statement always overwrite slot 0 and 1, rather than appending
                // to the addBindValue stack (which the SQLite driver tolerates
                // but is brittle across drivers).
                titleUpdate.bindValue(0, cleaned);
                titleUpdate.bindValue(1, bookId);
                if (!titleUpdate.exec()) {
                    qWarning() << "Migration v4→v5: failed to clean title for" << bookId
                               << ":" << titleUpdate.lastError().text();
                    // Non-fatal: a bad title is better than an aborted migration.
                }
            }
        }

        // ----------------------------------------------------------------
        // Commit
        // ----------------------------------------------------------------
        if (!mq.exec(QStringLiteral("PRAGMA user_version = 5"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v4→v5: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            mq.exec("DROP TABLE IF EXISTS temp.bookid_remap");
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        mq.exec("DROP TABLE IF EXISTS temp.bookid_remap");
        mq.exec("PRAGMA foreign_keys = ON");
        qDebug() << "Migration to schema version 5 complete.";
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 5 → 6
    // Two fixes for data-quality issues discovered by running the app:
    //
    // (a) Re-clean MARC-contaminated titles using the corrected regex that also
    //     strips preceding MARC punctuation (e.g. " : $b " → ": " rather than
    //     leaving " : " untouched and producing double-colon output like "Foo :: Bar").
    //
    // (b) Normalise format_type values that contain MIME parameters — the collector
    //     was storing "plain; charset=us_ascii" instead of "plain" because
    //     normalizeFormatName() did not strip "; charset=…" before matching.
    //     Strip the parameter suffix and deduplicate rows where stripping would
    //     create a UNIQUE(edition_id, format_type) conflict.
    if (version == 5) {
        qDebug() << "Migrating database schema from version 5 to 6...";

        QSqlQuery mq(db);

        // Open a single transaction covering both Part A and Part B so that
        // title re-cleaning and format-type stripping are committed atomically.
        if (!mq.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))
                || !mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v5→v6 preamble failed:" << mq.lastError().text();
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        // ----------------------------------------------------------------
        // Part A: re-clean titles (same C++ loop, corrected regex)
        // ----------------------------------------------------------------
        {
            QSqlQuery titleSelect(db);
            if (!titleSelect.exec(
                    QStringLiteral("SELECT book_id, title FROM books WHERE title LIKE '%$%'"
                                   " OR title LIKE '%: :%'"))) {
                qCritical() << "Migration v5→v6 (title select) failed:"
                            << titleSelect.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }

            // [a-z] only — see gutenberg_adapter.cpp parseSingleRdf() for why
            // numeric MARC subfields are intentionally excluded.
            static const QRegularExpression reMarc(QStringLiteral("[\\s:;/,]*\\$[a-z]\\s*"));
            // Collapses double-colon artefacts left by the old MARC regex (" : :") → ": ".
            // The leading \s* also consumes the space before the first colon.
            static const QRegularExpression reDoubleColon(QStringLiteral("\\s*:\\s*:\\s*"));
            QSqlQuery titleUpdate(db);
            titleUpdate.prepare(
                QStringLiteral("UPDATE books SET title = ? WHERE book_id = ?"));

            while (titleSelect.next()) {
                const QString bookId   = titleSelect.value(0).toString();
                const QString rawTitle = titleSelect.value(1).toString();
                QString cleaned = rawTitle;
                cleaned.replace(reMarc, QStringLiteral(": "));
                cleaned.replace(reDoubleColon, QStringLiteral(": "));
                cleaned = cleaned.simplified();
                if (cleaned == rawTitle)
                    continue;
                // Positional bindValue — see the equivalent loop in v4→v5
                // for the rationale.
                titleUpdate.bindValue(0, cleaned);
                titleUpdate.bindValue(1, bookId);
                if (!titleUpdate.exec())
                    qWarning() << "Migration v5→v6: failed to clean title for" << bookId;
            }
        }

        // ----------------------------------------------------------------
        // Part B: strip MIME parameters from format_type (e.g. "plain; charset=us_ascii" → "plain")
        // ----------------------------------------------------------------

        const QStringList partB = {
            // Delete sources for format rows whose type would collide after stripping.
            R"(DELETE FROM sources
               WHERE format_id IN (
                   SELECT f.id FROM formats f
                    WHERE f.format_type LIKE '%;%'
                      AND EXISTS (
                          SELECT 1 FROM formats f2
                           WHERE f2.edition_id = f.edition_id
                             AND f2.format_type = SUBSTR(f.format_type, 1, INSTR(f.format_type, ';') - 1)
                      )
               ))",
            // Delete the colliding format rows themselves.
            R"(DELETE FROM formats
               WHERE format_type LIKE '%;%'
                 AND EXISTS (
                     SELECT 1 FROM formats f2
                      WHERE f2.edition_id = formats.edition_id
                        AND f2.format_type = SUBSTR(formats.format_type, 1, INSTR(formats.format_type, ';') - 1)
                 ))",
            // Rename remaining parameterised rows to the bare type.
            R"(UPDATE OR IGNORE formats
               SET format_type = SUBSTR(format_type, 1, INSTR(format_type, ';') - 1)
               WHERE format_type LIKE '%;%')",
            // Any rows still containing ';' failed OR IGNORE — delete them as duplicates.
            R"(DELETE FROM formats WHERE format_type LIKE '%;%')",
        };

        for (const QString &sql : partB) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v5→v6 (format MIME param) failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        if (!mq.exec(QStringLiteral("PRAGMA user_version = 6"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v5→v6: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        mq.exec("PRAGMA foreign_keys = ON");
        qDebug() << "Migration to schema version 6 complete.";
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 6 → 7
    // Renames format_type 'text_plain' → 'plain'.  The slash in the MIME type
    // "text/plain" was previously encoded as '_' to produce "text_plain", but
    // normalizeFormatName() in gutenberg_adapter.cpp was updated to return the
    // cleaner "plain" (matching epub, pdf, etc.).  This migration brings existing
    // rows in line with new inserts so UI display and dedup are consistent.
    // Rows that would collide with an existing 'plain' row are deleted along
    // with their sources; the remaining rows are renamed with UPDATE OR IGNORE
    // and any still-unconverted duplicates are removed.
    if (version == 6) {
        qDebug() << "Migrating database schema from version 6 to 7...";

        QSqlQuery mq(db);
        if (!mq.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))
                || !mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v6→v7 preamble failed:" << mq.lastError().text();
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        const QStringList stmts = {
            // Delete sources for text_plain rows that would collide with an existing 'plain' row.
            R"(DELETE FROM sources
               WHERE format_id IN (
                   SELECT f.id FROM formats f
                    WHERE f.format_type = 'text_plain'
                      AND EXISTS (SELECT 1 FROM formats f2
                                   WHERE f2.edition_id = f.edition_id
                                     AND f2.format_type = 'plain')
               ))",
            // Delete the colliding text_plain format rows.
            R"(DELETE FROM formats
               WHERE format_type = 'text_plain'
                 AND EXISTS (SELECT 1 FROM formats f2
                              WHERE f2.edition_id = formats.edition_id
                                AND f2.format_type = 'plain'))",
            // Rename remaining text_plain rows to plain.
            "UPDATE OR IGNORE formats SET format_type = 'plain' WHERE format_type = 'text_plain'",
            // Delete any text_plain rows that OR IGNORE could not rename.
            "DELETE FROM formats WHERE format_type = 'text_plain'",
        };

        for (const QString &sql : stmts) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v6→v7 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        if (!mq.exec(QStringLiteral("PRAGMA user_version = 7"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v6→v7: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            mq.exec("PRAGMA foreign_keys = ON");
            return false;
        }

        mq.exec("PRAGMA foreign_keys = ON");
        qDebug() << "Migration to schema version 7 complete.";
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 7 → 8
    // Adds the sync_state table.  Catalog freshness used to live in QSettings
    // ("gutenberg_last_modified"), which made the cache decouple from the DB —
    // a deleted DB plus a stale cache gave the user an empty library with a 304
    // response.  See docs/design/sync-state-resume.md for the full state machine.
    //
    // The QSettings → sync_state value copy is performed by the app at startup
    // (main.cpp), not here, because QSettings depends on QCoreApplication which
    // may not be initialised when this code runs in tests or migration tools.
    if (version == 7) {
        qDebug() << "Migrating database schema from version 7 to 8...";

        QSqlQuery mq(db);
        if (!mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v7→v8 preamble failed:" << mq.lastError().text();
            return false;
        }

        const QString createSyncState = R"(CREATE TABLE IF NOT EXISTS sync_state (
            adapter_id        TEXT PRIMARY KEY,
            status            TEXT NOT NULL
                              CHECK(status IN ('in_progress', 'completed', 'failed')),
            phase             TEXT
                              CHECK(phase IS NULL OR phase IN ('downloading', 'parsing')),
            last_modified     TEXT,
            started_at        TEXT,
            completed_at      TEXT,
            books_processed   INTEGER NOT NULL DEFAULT 0,
            error_message     TEXT,
            download_url      TEXT,
            download_etag     TEXT,
            archive_path      TEXT,
            bytes_downloaded  INTEGER NOT NULL DEFAULT 0,
            bytes_total       INTEGER NOT NULL DEFAULT 0,
            last_parsed_entry TEXT
        ))";

        if (!mq.exec(createSyncState)) {
            qCritical() << "Migration v7→v8 failed creating sync_state:"
                        << mq.lastError().text();
            mq.exec("ROLLBACK");
            return false;
        }

        if (!mq.exec(QStringLiteral("PRAGMA user_version = 8"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v7→v8: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            return false;
        }

        qDebug() << "Migration to schema version 8 complete.";
        return verifySchemaVersion(connectionName);
    }

    // Migration: version 8 → 9
    // Adds the `validator_type` column to sync_state so adapters can record
    // whether `last_modified` / `download_etag` came from an HTTP Last-Modified
    // header (use If-Modified-Since on conditional GETs) or an ETag header (use
    // If-None-Match).  Gutenberg always sends Last-Modified, but Ben-Yehuda /
    // Archive.org adapters may rely on ETags; conflating the two would generate
    // an invalid If-Modified-Since: <etag-bytes> request.
    //
    // NULL values are treated as 'last_modified' by adapter code for backward
    // compatibility with v8 rows written before this column existed.
    if (version == 8) {
        qDebug() << "Migrating database schema from version 8 to 9...";

        QSqlQuery mq(db);
        if (!mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v8→v9 preamble failed:" << mq.lastError().text();
            return false;
        }

        // Idempotency: tests sometimes set PRAGMA user_version backwards without
        // reshaping the table, so the v9 column may already be present.  Probe
        // before ALTER to keep the migration replayable.  ALTER TABLE ADD COLUMN
        // also cannot add a CHECK constraint; matching CHECK semantics on
        // upgraded DBs would require a table rebuild, so we accept the looser
        // invariant — the createSchema path on fresh DBs still carries the full
        // CHECK and adapter code only ever writes the two valid values.
        bool columnExists = false;
        QSqlQuery pq(db);
        if (pq.exec(QStringLiteral("PRAGMA table_info(sync_state)"))) {
            while (pq.next()) {
                if (pq.value(1).toString() == QLatin1String("validator_type")) {
                    columnExists = true;
                    break;
                }
            }
        }

        if (!columnExists && !mq.exec(QStringLiteral(
                "ALTER TABLE sync_state ADD COLUMN validator_type TEXT"))) {
            qCritical() << "Migration v8→v9 failed adding validator_type:"
                        << mq.lastError().text();
            mq.exec("ROLLBACK");
            return false;
        }

        if (!mq.exec(QStringLiteral("PRAGMA user_version = 9"))
                || !mq.exec(QStringLiteral("COMMIT"))) {
            qCritical() << "Migration v8→v9: commit failed:" << mq.lastError().text();
            mq.exec("ROLLBACK");
            return false;
        }

        qDebug() << "Migration to schema version 9 complete.";
        return verifySchemaVersion(connectionName);
    }

    qCritical("Database schema version mismatch: expected %d, found %d. "
              "Run tools/migrate_db.py to upgrade the database.",
              kSchemaVersion, version);
    return false;
}

// ---------------------------------------------------------------------------
// sync_state helpers — see docs/design/sync-state-resume.md
// ---------------------------------------------------------------------------

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}
} // namespace

std::optional<SyncState> getSyncState(const QString &adapterId,
                                      const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT adapter_id, status, phase, last_modified, validator_type, "
        "started_at, completed_at, books_processed, error_message, "
        "download_url, download_etag, archive_path, "
        "bytes_downloaded, bytes_total, last_parsed_entry "
        "FROM sync_state WHERE adapter_id = ?"));
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "getSyncState: query failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;

    SyncState s;
    s.adapterId       = q.value(0).toString();
    s.status          = q.value(1).toString();
    s.phase           = q.value(2).toString();
    s.lastModified    = q.value(3).toString();
    s.validatorType   = q.value(4).toString();
    s.startedAt       = q.value(5).toString();
    s.completedAt     = q.value(6).toString();
    s.booksProcessed  = q.value(7).toLongLong();
    s.errorMessage    = q.value(8).toString();
    s.downloadUrl     = q.value(9).toString();
    s.downloadEtag    = q.value(10).toString();
    s.archivePath     = q.value(11).toString();
    s.bytesDownloaded = q.value(12).toLongLong();
    s.bytesTotal      = q.value(13).toLongLong();
    s.lastParsedEntry = q.value(14).toString();
    return s;
}

qint64 countBooksForAdapter(const QString &adapterIdPrefix, const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM books WHERE book_id LIKE ?"));
    q.addBindValue(adapterIdPrefix + QStringLiteral(":%"));
    if (!q.exec() || !q.next()) {
        qWarning() << "countBooksForAdapter: query failed:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toLongLong();
}

bool beginFetch(const QString &adapterId, const QString &downloadUrl,
                const QString &archivePath, const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    // INSERT OR REPLACE so we clobber any prior row (failed/completed) and reset all
    // resume cursors at once.  PRIMARY KEY on adapter_id keeps us idempotent.
    // The validator_type sub-SELECT mirrors last_modified for consistency.
    // In practice the preserved value lives only between this INSERT and the
    // first recordDownloadProgress() call (which overwrites it from the new
    // response headers), but mirroring the same preservation contract keeps
    // the two fields in lock-step and avoids a half-initialised row if the
    // network stack fails before any header arrives.
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sync_state ("
        "  adapter_id, status, phase, last_modified, validator_type,"
        "  started_at, completed_at, books_processed, error_message,"
        "  download_url, download_etag, archive_path,"
        "  bytes_downloaded, bytes_total, last_parsed_entry"
        ") VALUES (?, 'in_progress', 'downloading', "
        "  (SELECT last_modified  FROM sync_state WHERE adapter_id = ?),"
        "  (SELECT validator_type FROM sync_state WHERE adapter_id = ?),"
        "  ?, NULL, 0, NULL, ?, NULL, ?, 0, 0, NULL)"));
    q.addBindValue(adapterId);
    q.addBindValue(adapterId);          // preserves last completed last_modified
    q.addBindValue(adapterId);          // preserves last completed validator_type
    q.addBindValue(nowIso());
    q.addBindValue(downloadUrl);
    q.addBindValue(archivePath);
    if (!q.exec()) {
        qWarning() << "beginFetch: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool recordDownloadProgress(const QString &adapterId, qint64 bytesDownloaded,
                            qint64 bytesTotal, const QString &downloadEtag,
                            const QString &validatorType,
                            const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_state SET bytes_downloaded = ?, bytes_total = ?, "
        "download_etag  = COALESCE(NULLIF(?, ''), download_etag), "
        "validator_type = COALESCE(NULLIF(?, ''), validator_type) "
        "WHERE adapter_id = ?"));
    q.addBindValue(bytesDownloaded);
    q.addBindValue(bytesTotal);
    q.addBindValue(downloadEtag);
    q.addBindValue(validatorType);
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "recordDownloadProgress: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool recordDownloadComplete(const QString &adapterId, qint64 bytesTotal,
                            const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_state SET phase = 'parsing', bytes_downloaded = ?, "
        "bytes_total = ?, last_parsed_entry = NULL, books_processed = 0 "
        "WHERE adapter_id = ?"));
    q.addBindValue(bytesTotal);
    q.addBindValue(bytesTotal);
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "recordDownloadComplete: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool recordBatchCommit(const QString &adapterId, const QString &lastParsedEntry,
                       qint64 booksProcessedDelta, const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_state SET last_parsed_entry = ?, "
        "books_processed = books_processed + ? "
        "WHERE adapter_id = ?"));
    q.addBindValue(lastParsedEntry);
    q.addBindValue(booksProcessedDelta);
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "recordBatchCommit: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool completeFetch(const QString &adapterId, const QString &lastModified,
                   const QString &validatorType,
                   const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_state SET status = 'completed', phase = NULL, "
        "completed_at = ?, "
        "last_modified  = COALESCE(NULLIF(?, ''), last_modified), "
        "validator_type = COALESCE(NULLIF(?, ''), validator_type), "
        "download_url = NULL, download_etag = NULL, archive_path = NULL, "
        "bytes_downloaded = 0, bytes_total = 0, last_parsed_entry = NULL, "
        "error_message = NULL "
        "WHERE adapter_id = ?"));
    q.addBindValue(nowIso());
    q.addBindValue(lastModified);
    q.addBindValue(validatorType);
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "completeFetch: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool failFetch(const QString &adapterId, const QString &errorMessage,
               const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery q(db);
    // Leave the resume cursors in place — next attempt may pick up from them.
    q.prepare(QStringLiteral(
        "UPDATE sync_state SET status = 'failed', error_message = ? "
        "WHERE adapter_id = ?"));
    q.addBindValue(errorMessage);
    q.addBindValue(adapterId);
    if (!q.exec()) {
        qWarning() << "failFetch: failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool insertSampleData(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList inserts = {
        R"(INSERT OR IGNORE INTO books (book_id, title, author, publish_year) VALUES
            ('gutenberg:1342', 'Pride and Prejudice',                'Jane Austen',     1813),
            ('gutenberg:76',   'The Adventures of Huckleberry Finn', 'Mark Twain',      1884),
            ('gutenberg:1184', 'The Count of Monte Cristo',          'Alexandre Dumas', 1844)
        )",
        // n78095332 and n79025140 are LOC names-authority IDs (Jane Austen and Mark Twain
        // the persons), not work-level LCCNs — they are not stored as identifiers.
        R"(INSERT OR IGNORE INTO book_identifiers (book_id, type, value) VALUES
            ('gutenberg:1342', 'gutenberg', '1342'),
            ('gutenberg:76',   'gutenberg', '76'),
            ('gutenberg:1184', 'gutenberg', '1184')
        )",
        // French editions are intentionally omitted: Gutenberg carries only English
        // content for these works, so French stubs would be permanently empty.
        R"(INSERT OR IGNORE INTO editions (id, book_id, language) VALUES
            (1, 'gutenberg:1342', 'English'),
            (3, 'gutenberg:76',   'English'),
            (4, 'gutenberg:1184', 'English')
        )",
        R"(INSERT OR IGNORE INTO genres (id, genre_name) VALUES
            (1, 'Romance'),
            (2, 'Classic'),
            (3, 'Adventure'),
            (4, 'Historical Fiction')
        )",
        R"(INSERT OR IGNORE INTO book_genres (book_id, genre_id) VALUES
            ('gutenberg:1342', 1), ('gutenberg:1342', 2),
            ('gutenberg:76',   3), ('gutenberg:76',   2),
            ('gutenberg:1184', 3), ('gutenberg:1184', 4), ('gutenberg:1184', 2)
        )",
        R"(INSERT OR IGNORE INTO formats (id, edition_id, format_type) VALUES
            (1, 1, 'epub'), (2, 1, 'pdf'), (3, 3, 'epub'), (4, 4, 'epub')
        )",
        R"(INSERT OR IGNORE INTO sources (id, format_id, source_name, download_link) VALUES
            (1, 1, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.epub.images'),
            (2, 2, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1342.pdf.images'),
            (3, 3, 'Gutenberg', 'https://www.gutenberg.org/ebooks/76.epub.images'),
            (4, 4, 'Gutenberg', 'https://www.gutenberg.org/ebooks/1184.epub.images')
        )",
        R"(INSERT OR IGNORE INTO library_items (book_id, edition_id, status) VALUES
            ('gutenberg:1342', 1, 'saved'),
            ('gutenberg:76',   3, 'downloaded')
        )"
        // NOTE: preset voices are seeded by createSchema() so they exist in
        // production builds (which skip insertSampleData under !QT_DEBUG).
    };

    for (const QString &sql : inserts) {
        if (!query.exec(sql)) {
            qWarning() << "Failed to insert sample data:" << query.lastError().text() << "\nQuery:" << sql;
            return false;
        }
    }

    qDebug() << "Sample data inserted successfully.";
    return true;
}

} // namespace bookhub::db
