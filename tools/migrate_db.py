#!/usr/bin/env python3
"""
migrate_db.py — Migrate bookhub.db to the current schema version.

Schema v0: ISBN-keyed books table.
Schema v1: book_id-keyed books table with book_identifiers for cross-source deduplication.
Schema v2: Adds download-flow tables and library_items.status values.
Schema v3: Adds voices table (preset/custom TTS voices) and a CHECK constraint on
           library_items.status to prevent invalid state values.
Schema v4: Normalises language codes to ISO 639-1 (e.g. "English" → "en").
Schema v5: Strips _N suffixes from format_type rows (epub_1 → epub) and fixes MARC
           subfield markers in book titles ($b → ": "); remaps lccn:n* book IDs that
           point to names-authority (person) records to gutenberg:<id>.
Schema v6: Strips MIME parameters from format_type (e.g. "plain; charset=us-ascii"
           → "plain") and cleans up any double-colon artefacts in book titles.

The v1→v2 migration and all migrations from v3 onward are handled automatically by
the app at startup; launch the app once and it will upgrade the database in place.
The v0→v1 and v2→v3 migrations require this script because they involve structural
table changes that cannot be performed online while the app is running.

All script-handled migration steps run inside a single transaction. PRAGMA user_version
is set outside the transaction (SQLite requirement). On any failure the transaction is
rolled back and the database is left untouched.
"""

import argparse
import os
import re
import sqlite3
import sys

try:
    import platformdirs
    _DEFAULT_DB = os.path.join(
        platformdirs.user_data_dir("BookHub", "BookHub"), "bookhub.db"
    )
except ImportError:
    _DEFAULT_DB = None  # fall back to requiring --db-path


# ---------------------------------------------------------------------------
# New schema DDL (version 1)
# ---------------------------------------------------------------------------

NEW_BOOKS_DDL = """
CREATE TABLE IF NOT EXISTS books (
    book_id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    author TEXT,
    publish_year INTEGER,
    summary TEXT
)
"""

NEW_BOOK_IDENTIFIERS_DDL = """
CREATE TABLE IF NOT EXISTS book_identifiers (
    book_id TEXT NOT NULL REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
    type    TEXT NOT NULL,
    value   TEXT NOT NULL,
    UNIQUE(type, value)
)
"""

NEW_EDITIONS_DDL = """
CREATE TABLE IF NOT EXISTS editions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id TEXT NOT NULL,
    language TEXT NOT NULL,
    publisher TEXT,
    publish_date TEXT,
    UNIQUE (book_id, language),
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE
)
"""

NEW_GENRES_DDL = """
CREATE TABLE IF NOT EXISTS genres (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    genre_name TEXT UNIQUE NOT NULL
)
"""

NEW_BOOK_GENRES_DDL = """
CREATE TABLE IF NOT EXISTS book_genres (
    book_id TEXT,
    genre_id INTEGER,
    PRIMARY KEY (book_id, genre_id),
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (genre_id) REFERENCES genres(id) ON DELETE CASCADE
)
"""

NEW_FORMATS_DDL = """
CREATE TABLE IF NOT EXISTS formats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    edition_id INTEGER NOT NULL,
    format_type TEXT NOT NULL,
    UNIQUE(edition_id, format_type),
    FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
)
"""

NEW_SOURCES_DDL = """
CREATE TABLE IF NOT EXISTS sources (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    format_id INTEGER NOT NULL,
    source_name TEXT NOT NULL,
    download_link TEXT NOT NULL,
    UNIQUE(format_id, source_name),
    FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
)
"""

NEW_LIBRARY_ITEMS_DDL = """
CREATE TABLE IF NOT EXISTS library_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id TEXT NOT NULL UNIQUE,
    edition_id INTEGER,
    added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status TEXT CHECK(status IS NULL OR status IN ('saved', 'downloading', 'downloaded', 'converting', 'audiobook_ready', 'error')),
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (edition_id) REFERENCES editions(id)
)
"""

# ---------------------------------------------------------------------------
# New schema DDL (version 3)
# ---------------------------------------------------------------------------

NEW_VOICES_DDL = """
CREATE TABLE IF NOT EXISTS voices (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    name                 TEXT NOT NULL UNIQUE,
    type                 TEXT NOT NULL CHECK(type IN ('preset', 'custom')),
    engine               TEXT NOT NULL CHECK(engine IN ('sherpa_onnx', 'pocket_tts')),
    model_path           TEXT,
    config_path          TEXT,
    reference_audio_path TEXT,
    created_at           TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
"""

_PRESET_VOICES = [
    ("Classic Storyteller", "preset", "sherpa_onnx"),
    ("Warm Listener",       "preset", "sherpa_onnx"),
    ("Crisp Narrator",      "preset", "sherpa_onnx"),
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_GUTENBERG_RE = re.compile(r'^gutenberg(\d+)$')


def derive_book_id(isbn: str) -> str:
    """Map an old ISBN/identifier string to a new book_id.

    Rules:
      - "gutenberg<digits>"  →  "gutenberg:<digits>"
      - anything else        →  "legacy:<isbn>"
    """
    m = _GUTENBERG_RE.match(isbn)
    if m:
        return f"gutenberg:{m.group(1)}"
    return f"legacy:{isbn}"


def table_exists(cur: sqlite3.Cursor, name: str) -> bool:
    cur.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)
    )
    return cur.fetchone() is not None


def column_exists(cur: sqlite3.Cursor, table: str, column: str) -> bool:
    cur.execute(f"PRAGMA table_info({table})")
    return any(row[1] == column for row in cur.fetchall())


# ---------------------------------------------------------------------------
# v2 → v3 migration
# ---------------------------------------------------------------------------

def _migrate_v2_to_v3(con: sqlite3.Connection, cur: sqlite3.Cursor) -> None:
    """Migrate an open v2 database to v3 in-place.

    Changes:
      - Creates the ``voices`` table for preset and custom TTS voices.
      - Seeds the three built-in preset voices (idempotent via INSERT OR IGNORE).
      - Recreates ``library_items`` with a CHECK constraint on ``status`` so
        invalid values are rejected at the DB level.  SQLite does not support
        ALTER TABLE ADD CONSTRAINT, so the rename-and-copy pattern is used.
    """
    print("Migrating schema v2 → v3 ...")

    # PRAGMA must run outside a transaction.
    con.execute("PRAGMA foreign_keys = OFF")
    con.execute("BEGIN")

    # Create voices table (safe to re-run; IF NOT EXISTS).
    con.execute(NEW_VOICES_DDL)

    # Seed preset voices. UNIQUE(name) makes this idempotent.
    con.executemany(
        "INSERT OR IGNORE INTO voices (name, type, engine) VALUES (?, ?, ?)",
        _PRESET_VOICES,
    )
    print(f"Seeded {len(_PRESET_VOICES)} preset voices")

    # Recreate library_items with CHECK constraint on status.
    # INSERT ... SELECT fails loudly if any existing row violates the new
    # constraint — better than silently discarding data.
    con.execute("""
        CREATE TABLE library_items_new (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id    TEXT NOT NULL UNIQUE,
            edition_id INTEGER,
            added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            status TEXT CHECK(status IS NULL OR status IN
                ('saved', 'downloading', 'downloaded',
                 'converting', 'audiobook_ready', 'error')),
            FOREIGN KEY (book_id)    REFERENCES books(book_id)
                ON DELETE CASCADE ON UPDATE CASCADE,
            FOREIGN KEY (edition_id) REFERENCES editions(id)
        )
    """)
    cur.execute("SELECT COUNT(*) FROM library_items")
    item_count = cur.fetchone()[0]
    con.execute("INSERT INTO library_items_new SELECT * FROM library_items")
    con.execute("DROP TABLE library_items")
    con.execute("ALTER TABLE library_items_new RENAME TO library_items")
    print(f"Migrated {item_count} library_items")

    con.execute("COMMIT")
    con.execute("PRAGMA foreign_keys = ON")

    try:
        con.execute("PRAGMA user_version = 3")
    except Exception as exc:
        print(
            f"Error: migration committed but version stamp failed: {exc}\n"
            "The schema is correct. Run the following to fix the version:\n"
            "  sqlite3 bookhub.db 'PRAGMA user_version = 3'",
            file=sys.stderr,
        )
        sys.exit(1)

    print("Migration complete. user_version set to 3.")


# ---------------------------------------------------------------------------
# Migration
# ---------------------------------------------------------------------------

def migrate(db_path: str) -> None:
    print(f"Using database: {db_path}")
    if not os.path.exists(db_path):
        print(f"Error: database file not found: {db_path}", file=sys.stderr)
        sys.exit(1)

    con = sqlite3.connect(db_path)
    con.row_factory = sqlite3.Row

    try:
        cur = con.cursor()

        # --- Check current version -------------------------------------------
        cur.execute("PRAGMA user_version")
        user_version = cur.fetchone()[0]

        if user_version == 1:
            print(
                "Database is at v1. Launch the app once to auto-migrate to v2."
            )
            con.close()
            return

        if user_version == 2:
            _migrate_v2_to_v3(con, cur)
            con.close()
            return

        if user_version in (3, 4, 5):
            print(
                f"Database is at v{user_version}. "
                "Launch the app once to auto-migrate to v6 (current)."
            )
            con.close()
            return

        if user_version >= 6:
            print("Already at version 6 (current), nothing to do.")
            con.close()
            return

        # --- Guard against partial previous runs ------------------------------
        if table_exists(cur, "_old_books"):
            print(
                "Error: table '_old_books' already exists. The database may be "
                "in an unknown partial-migration state. Restore from a backup "
                "before retrying.",
                file=sys.stderr,
            )
            con.close()
            sys.exit(1)

        for sentinel in ("_old_formats", "_old_sources"):
            if table_exists(cur, sentinel):
                print(
                    f"Error: table '{sentinel}' already exists. The database may be "
                    "in an unknown partial-migration state. Restore from a backup "
                    "before retrying.",
                    file=sys.stderr,
                )
                con.close()
                sys.exit(1)

        # --- Verify the old 'books' table has the expected 'isbn' column ------
        if table_exists(cur, "books") and not column_exists(cur, "books", "isbn"):
            print(
                "Error: 'books' table exists but has no 'isbn' column. "
                "The database may have been partially migrated. "
                "Restore from a backup before retrying.",
                file=sys.stderr,
            )
            con.close()
            sys.exit(1)

        # Step 1: Disable FK enforcement — must be outside any active transaction.
        con.execute("PRAGMA foreign_keys = OFF")

        # ======================================================================
        # BEGIN TRANSACTION
        # ======================================================================
        con.execute("BEGIN")

        # Step 2: Rename old tables to backup names.
        # formats and sources are renamed alongside editions: SQLite's
        # ALTER TABLE RENAME rewrites child FK definitions to point at the new
        # parent name (_old_editions), so leaving formats in place would give it
        # a broken FK after _old_editions is dropped.
        con.execute("ALTER TABLE books RENAME TO _old_books")
        con.execute("ALTER TABLE editions RENAME TO _old_editions")
        if table_exists(cur, "formats"):
            con.execute("ALTER TABLE formats RENAME TO _old_formats")
        if table_exists(cur, "sources"):
            con.execute("ALTER TABLE sources RENAME TO _old_sources")
        con.execute("ALTER TABLE book_genres RENAME TO _old_book_genres")
        con.execute("ALTER TABLE library_items RENAME TO _old_library_items")

        # Step 3: Create new tables.
        con.execute(NEW_BOOKS_DDL)
        con.execute(NEW_BOOK_IDENTIFIERS_DDL)
        con.execute(NEW_EDITIONS_DDL)
        con.execute(NEW_GENRES_DDL)
        con.execute(NEW_BOOK_GENRES_DDL)
        con.execute(NEW_FORMATS_DDL)
        con.execute(NEW_SOURCES_DDL)
        con.execute(NEW_LIBRARY_ITEMS_DDL)

        # Step 4: Derive book_id mapping from old isbn values.
        cur.execute(
            "SELECT isbn, title, author, publish_year, summary FROM _old_books"
        )
        old_books = cur.fetchall()

        isbn_to_book_id: dict[str, str] = {}
        for row in old_books:
            isbn_to_book_id[row["isbn"]] = derive_book_id(row["isbn"])

        # Step 5: Insert into new books table (drop publication_date).
        books_rows = [
            (
                isbn_to_book_id[row["isbn"]],
                row["title"],
                row["author"],
                row["publish_year"],
                row["summary"],
            )
            for row in old_books
        ]
        con.executemany(
            "INSERT INTO books (book_id, title, author, publish_year, summary) "
            "VALUES (?, ?, ?, ?, ?)",
            books_rows,
        )
        print(f"Migrated {len(books_rows)} books")

        # Step 6: Insert into book_identifiers (one row per book, type='legacy_isbn').
        identifier_rows = [
            (isbn_to_book_id[row["isbn"]], "legacy_isbn", row["isbn"])
            for row in old_books
        ]
        con.executemany(
            "INSERT INTO book_identifiers (book_id, type, value) VALUES (?, ?, ?)",
            identifier_rows,
        )
        print(f"Migrated {len(identifier_rows)} book_identifiers")

        # Step 7: Migrate editions — replace book_isbn with derived book_id.
        cur.execute(
            "SELECT id, book_isbn, language, publisher, publish_date FROM _old_editions"
        )
        old_editions = cur.fetchall()
        edition_rows = [
            (
                row["id"],
                isbn_to_book_id[row["book_isbn"]],
                row["language"],
                row["publisher"],
                row["publish_date"],
            )
            for row in old_editions
        ]
        con.executemany(
            "INSERT INTO editions (id, book_id, language, publisher, publish_date) "
            "VALUES (?, ?, ?, ?, ?)",
            edition_rows,
        )
        print(f"Migrated {len(edition_rows)} editions")

        # Step 8: Migrate book_genres — replace book_isbn with derived book_id.
        cur.execute("SELECT book_isbn, genre_id FROM _old_book_genres")
        old_book_genres = cur.fetchall()
        book_genre_rows = [
            (isbn_to_book_id[row["book_isbn"]], row["genre_id"])
            for row in old_book_genres
        ]
        con.executemany(
            "INSERT INTO book_genres (book_id, genre_id) VALUES (?, ?)",
            book_genre_rows,
        )
        print(f"Migrated {len(book_genre_rows)} book_genres")

        # Step 9: Migrate library_items — replace book_isbn with derived book_id.
        cur.execute(
            "SELECT id, book_isbn, edition_id, added_date, status FROM _old_library_items"
        )
        old_library_items = cur.fetchall()
        library_rows = [
            (
                row["id"],
                isbn_to_book_id[row["book_isbn"]],
                row["edition_id"],
                row["added_date"],
                row["status"],
            )
            for row in old_library_items
        ]
        con.executemany(
            "INSERT INTO library_items (id, book_id, edition_id, added_date, status) "
            "VALUES (?, ?, ?, ?, ?)",
            library_rows,
        )
        print(f"Migrated {len(library_rows)} library_items")

        # Step 10: Migrate formats. MIN(id) picks a stable representative row when
        # the old schema allowed duplicate (edition_id, format_type) pairs that the
        # restored UNIQUE constraint now forbids.
        if table_exists(cur, "_old_formats"):
            cur.execute("""
                SELECT MIN(id) AS id, edition_id, format_type
                FROM _old_formats
                GROUP BY edition_id, format_type
            """)
            old_formats = cur.fetchall()
            format_rows = [
                (row["id"], row["edition_id"], row["format_type"])
                for row in old_formats
            ]
            con.executemany(
                "INSERT INTO formats (id, edition_id, format_type) VALUES (?, ?, ?)",
                format_rows,
            )
            print(f"Migrated {len(format_rows)} formats")

        # Step 11: Migrate sources. MIN(id) picks a stable representative row when
        # the old schema allowed duplicate (format_id, source_name) pairs that the
        # restored UNIQUE constraint now forbids. download_link from the earliest row
        # is kept; for true duplicates it will be identical.
        if table_exists(cur, "_old_sources"):
            cur.execute("""
                SELECT MIN(id) AS id, format_id, source_name,
                       MIN(download_link) AS download_link
                FROM _old_sources
                GROUP BY format_id, source_name
            """)
            old_sources = cur.fetchall()
            source_rows = [
                (row["id"], row["format_id"], row["source_name"], row["download_link"])
                for row in old_sources
            ]
            con.executemany(
                "INSERT INTO sources (id, format_id, source_name, download_link) "
                "VALUES (?, ?, ?, ?)",
                source_rows,
            )
            print(f"Migrated {len(source_rows)} sources")

        # Step 12: Drop backup tables.
        if table_exists(cur, "_old_sources"):
            con.execute("DROP TABLE _old_sources")
        if table_exists(cur, "_old_formats"):
            con.execute("DROP TABLE _old_formats")
        con.execute("DROP TABLE _old_library_items")
        con.execute("DROP TABLE _old_book_genres")
        con.execute("DROP TABLE _old_editions")
        con.execute("DROP TABLE _old_books")

        # Step 13: Commit transaction.
        con.execute("COMMIT")

        # Steps 14-15: Re-enable FK enforcement and stamp the version — both
        # must be outside an active transaction (SQLite requirement).
        con.execute("PRAGMA foreign_keys = ON")

        # Stamp the version in a separate try block: if COMMIT succeeded but this
        # fails, the schema is correct but user_version is still 0, so the migration
        # would re-run on next launch. Surface a clear recovery instruction rather
        # than silently leaving the DB in an ambiguous state.
        try:
            con.execute("PRAGMA user_version = 1")
        except Exception as exc:
            print(
                f"Error: migration committed but version stamp failed: {exc}\n"
                "The schema is correct. Run the following to fix the version:\n"
                "  sqlite3 bookhub.db 'PRAGMA user_version = 1'",
                file=sys.stderr,
            )
            con.close()
            sys.exit(1)

        print("Migration complete. user_version set to 1.")

    except Exception as exc:
        # Roll back any partial changes and report the error.
        try:
            con.execute("ROLLBACK")
        except Exception:
            pass
        print(f"Error: migration failed: {exc}", file=sys.stderr)
        con.close()
        sys.exit(1)

    con.close()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if _DEFAULT_DB is not None:
        default_help = f"Path to the SQLite database file (default: {_DEFAULT_DB})"
    else:
        default_help = (
            "Path to the SQLite database file. "
            "Install 'platformdirs' (pip install platformdirs) to use the platform default, "
            "or supply --db-path explicitly."
        )
    parser = argparse.ArgumentParser(
        description=(
            "Migrate bookhub.db to the current schema version (v6). "
            "Handles v0→v1 (ISBN-keyed to book_id-keyed) and v2→v3 "
            "(voices table + library_items CHECK constraint) directly. "
            "For v3→v6 (language normalisation, format-suffix cleanup, MARC title "
            "fixes, MIME parameter stripping), launch the app — it auto-migrates "
            "at startup.\n\n"
            "Platform default paths:\n"
            "  Linux:   ~/.local/share/BookHub/BookHub/bookhub.db\n"
            "  macOS:   ~/Library/Application Support/BookHub/BookHub/bookhub.db\n"
            "  Windows: %APPDATA%\\BookHub\\BookHub\\bookhub.db"
        )
    )
    parser.add_argument(
        "--db-path",
        default=_DEFAULT_DB,
        required=(_DEFAULT_DB is None),
        help=default_help,
    )
    args = parser.parse_args()

    migrate(args.db_path)


if __name__ == "__main__":
    main()
