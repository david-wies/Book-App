#include "database.h"
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
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
    // Normalises language codes stored as ISO 639-1/2 ("en", "fr", "nl", …)
    // to their English full names ("English", "French", "Dutch", …).
    //
    // Some databases may already contain a mix: the same book might have both
    // an "en" edition (from the Gutenberg collector) and an "English" edition
    // (from earlier test data or sample inserts).  The UNIQUE(book_id, language)
    // constraint prevents a plain UPDATE from converting "en" → "English" when
    // an "English" row already exists for that book.
    //
    // Strategy with FK checks off:
    //   1. Redirect library_items that reference an ISO-coded edition to the
    //      matching full-name edition (for books that have both).
    //   2. Manually delete sources → formats → editions for the conflicting
    //      ISO-coded editions (cascade is disabled, so we do it in order).
    //   3. Plain UPDATE for all remaining ISO-coded editions (now conflict-free).
    //
    // Format-type suffix cleanup (_N, _NN, …) is handled at the display layer
    // (book_details_panel) and at insert time (book_discovery_service), so no
    // schema change is needed here.
    if (version == 3) {
        qDebug() << "Migrating database schema from version 3 to 4...";

        // Build the temp mapping table outside the transaction so it is
        // available regardless of whether BEGIN succeeds.
        QSqlQuery prep(db);
        const QStringList preStmts = {
            "PRAGMA foreign_keys = OFF",
            "DROP TABLE IF EXISTS temp.lang_map",
            R"(CREATE TEMP TABLE lang_map (code TEXT PRIMARY KEY, name TEXT NOT NULL))",
            R"(INSERT INTO lang_map VALUES
               ('en','English'),('fr','French'),('de','German'),('nl','Dutch'),
               ('es','Spanish'),('it','Italian'),('pt','Portuguese'),('la','Latin'),
               ('fi','Finnish'),('da','Danish'),('sv','Swedish'),('nb','Norwegian Bokmål'),
               ('no','Norwegian Bokmål'),('zh','Chinese'),('zho','Chinese'),
               ('ru','Russian'),('rus','Russian'),('ja','Japanese'),('jpn','Japanese'),
               ('ar','Arabic'),('ara','Arabic'),('grc','Ancient Greek'),('el','Greek'),
               ('he','Hebrew'),('hu','Hungarian'),('cs','Czech'),('pl','Polish'),
               ('ro','Romanian'),('uk','Ukrainian'),('sr','Serbian'),('bg','Bulgarian'),
               ('hr','Croatian'),('sk','Slovak'),('sl','Slovenian'),('ca','Catalan'),
               ('tl','Tagalog'),('eo','Esperanto'),('cy','Welsh'),('af','Afrikaans'),
               ('ga','Irish'),('gl','Galician'),('is','Icelandic'),('lt','Lithuanian'),
               ('oc','Occitan'),('yi','Yiddish'),('br','Breton'),('mi','Māori'),
               ('fy','Western Frisian'))"
        };
        for (const QString &sql : preStmts) {
            if (!prep.exec(sql)) {
                qCritical() << "Migration v3→v4 pre-step failed at:" << sql
                            << "\nError:" << prep.lastError().text();
                prep.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        // Now run the main migration inside a transaction.
        QSqlQuery mq(db);
        const QStringList migration = {
            "BEGIN",

            // Step 1: Redirect library_items from ISO-coded edition to the full-name
            // edition for books that have both (avoids dangling edition_id after deletion).
            R"(UPDATE library_items
               SET edition_id = (
                   SELECT e2.id
                     FROM editions e1
                     JOIN lang_map lm ON lm.code = e1.language
                     JOIN editions e2 ON e2.book_id = e1.book_id AND e2.language = lm.name
                    WHERE e1.id = library_items.edition_id
                    LIMIT 1
               )
               WHERE edition_id IN (
                   SELECT e.id FROM editions e
                     JOIN lang_map lm ON lm.code = e.language
                    WHERE EXISTS (
                          SELECT 1 FROM editions e2
                           WHERE e2.book_id = e.book_id AND e2.language = lm.name
                    )
               ))",

            // Step 2: Delete sources for conflicting ISO-coded editions.
            R"(DELETE FROM sources
               WHERE format_id IN (
                   SELECT f.id FROM formats f
                     JOIN editions e ON e.id = f.edition_id
                     JOIN lang_map lm ON lm.code = e.language
                    WHERE EXISTS (SELECT 1 FROM editions e2
                                   WHERE e2.book_id = e.book_id AND e2.language = lm.name)
               ))",

            // Step 3: Delete formats for conflicting ISO-coded editions.
            R"(DELETE FROM formats
               WHERE edition_id IN (
                   SELECT e.id FROM editions e
                     JOIN lang_map lm ON lm.code = e.language
                    WHERE EXISTS (SELECT 1 FROM editions e2
                                   WHERE e2.book_id = e.book_id AND e2.language = lm.name)
               ))",

            // Step 4: Delete conflicting ISO-coded editions themselves.
            R"(DELETE FROM editions
               WHERE language IN (SELECT code FROM lang_map)
                 AND EXISTS (
                       SELECT 1 FROM editions e2
                         JOIN lang_map lm ON lm.code = editions.language
                        WHERE e2.book_id = editions.book_id AND e2.language = lm.name
                 ))",

            // Step 5: Rename all remaining ISO-coded editions (no conflicts left).
            R"(UPDATE editions
               SET language = (SELECT name FROM lang_map WHERE code = language)
               WHERE language IN (SELECT code FROM lang_map))",

            "PRAGMA user_version = 4",
            "COMMIT"
        };

        for (const QString &sql : migration) {
            if (!mq.exec(sql)) {
                qCritical() << "Migration v3→v4 failed at:" << sql
                            << "\nError:" << mq.lastError().text();
                mq.exec("ROLLBACK");
                mq.exec("DROP TABLE IF EXISTS temp.lang_map");
                mq.exec("PRAGMA foreign_keys = ON");
                return false;
            }
        }

        mq.exec("DROP TABLE IF EXISTS temp.lang_map");
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
            // book whose book_id looks like a name-authority LCCN (prefix "lccn:n")
            // AND that has a gutenberg identifier we can use as the new key.
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
                titleUpdate.addBindValue(cleaned);
                titleUpdate.addBindValue(bookId);
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
                return false;
            }

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
                titleUpdate.addBindValue(cleaned);
                titleUpdate.addBindValue(bookId);
                if (!titleUpdate.exec())
                    qWarning() << "Migration v5→v6: failed to clean title for" << bookId;
            }
        }

        // ----------------------------------------------------------------
        // Part B: strip MIME parameters from format_type (e.g. "plain; charset=us_ascii" → "plain")
        // ----------------------------------------------------------------
        if (!mq.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))
                || !mq.exec(QStringLiteral("BEGIN"))) {
            qCritical() << "Migration v5→v6 (format MIME param) preamble failed:"
                        << mq.lastError().text();
            return false;
        }

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

    qCritical("Database schema version mismatch: expected %d, found %d. "
              "Run tools/migrate_db.py to upgrade the database.",
              kSchemaVersion, version);
    return false;
}

bool insertSampleData(const QString &connectionName)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    QSqlQuery query(db);

    QStringList inserts = {
        R"(INSERT OR IGNORE INTO books (book_id, title, author, publish_year) VALUES
            ('gutenberg:1342', 'Pride and Prejudice', 'Jane Austen', 1813),
            ('lccn:n79025140', 'The Adventures of Huckleberry Finn', 'Mark Twain', 1884),
            ('gutenberg:1184', 'The Count of Monte Cristo', 'Alexandre Dumas', 1844)
        )",
        R"(INSERT OR IGNORE INTO book_identifiers (book_id, type, value) VALUES
            ('gutenberg:1342', 'gutenberg', '1342'),
            ('lccn:n79025140', 'lccn', 'n79025140'),
            ('lccn:n79025140', 'gutenberg', '76'),
            ('gutenberg:1184', 'gutenberg', '1184')
        )",
        // French editions for books 1342 and 1184 are intentionally omitted:
        // Gutenberg has only English content for these works, so French editions
        // would be permanently empty and mislead the UI.
        R"(INSERT OR IGNORE INTO editions (id, book_id, language) VALUES
            (1, 'gutenberg:1342', 'English'),
            (3, 'lccn:n79025140', 'English'),
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
            ('lccn:n79025140', 3), ('lccn:n79025140', 2),
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
            ('lccn:n79025140', 3, 'downloaded')
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
