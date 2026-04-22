#!/usr/bin/env python3
"""
migrate_db.py — Migrate classic_books.db from schema version 0 to version 1.

Schema v0: ISBN-keyed books table.
Schema v1: book_id-keyed books table with book_identifiers for cross-source deduplication.

All migration steps run inside a single transaction. PRAGMA user_version is set
outside the transaction (SQLite requirement). On any failure the transaction is
rolled back and the database is left untouched.
"""

import argparse
import os
import re
import sqlite3
import sys


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
    FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
)
"""

NEW_SOURCES_DDL = """
CREATE TABLE IF NOT EXISTS sources (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    format_id INTEGER NOT NULL,
    source_name TEXT NOT NULL,
    download_link TEXT NOT NULL,
    FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
)
"""

NEW_LIBRARY_ITEMS_DDL = """
CREATE TABLE IF NOT EXISTS library_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id TEXT NOT NULL,
    edition_id INTEGER,
    added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status TEXT,
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (edition_id) REFERENCES editions(id)
)
"""

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
# Migration
# ---------------------------------------------------------------------------

def migrate(db_path: str) -> None:
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

        if user_version >= 1:
            print("already at version 1, nothing to do")
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

        # ======================================================================
        # BEGIN TRANSACTION
        # ======================================================================
        con.execute("BEGIN")

        # Step 1: Disable foreign key enforcement for the duration of migration.
        # NOTE: PRAGMA foreign_keys must be set outside any transaction to take
        # effect, but SQLite silently accepts it inside one — it will apply after
        # the next transaction boundary. We disable it here so that the rename +
        # re-create sequence doesn't trip FK checks mid-migration.
        con.execute("PRAGMA foreign_keys = OFF")

        # Step 2: Rename old tables to backup names.
        con.execute("ALTER TABLE books RENAME TO _old_books")
        con.execute("ALTER TABLE editions RENAME TO _old_editions")
        con.execute("ALTER TABLE book_genres RENAME TO _old_book_genres")
        con.execute("ALTER TABLE library_items RENAME TO _old_library_items")

        # Step 3: Create new tables.
        con.execute(NEW_BOOKS_DDL)
        con.execute(NEW_BOOK_IDENTIFIERS_DDL)
        con.execute(NEW_EDITIONS_DDL)
        # genres, formats, sources schema is unchanged — recreate defensively
        # using IF NOT EXISTS so existing data is preserved.
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

        # Step 10: Drop backup tables.
        con.execute("DROP TABLE _old_library_items")
        con.execute("DROP TABLE _old_book_genres")
        con.execute("DROP TABLE _old_editions")
        con.execute("DROP TABLE _old_books")

        # Step 11: Re-enable foreign keys.
        con.execute("PRAGMA foreign_keys = ON")

        # Step 12: Commit transaction.
        con.execute("COMMIT")

        # Step 13: Set user_version OUTSIDE the transaction (SQLite requirement).
        con.execute("PRAGMA user_version = 1")

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
    parser = argparse.ArgumentParser(
        description=(
            "Migrate classic_books.db from schema version 0 (ISBN-keyed) "
            "to version 1 (book_id-keyed)."
        )
    )
    parser.add_argument(
        "db_path",
        nargs="?",
        default=os.path.join("build", "classic_books.db"),
        help="Path to the SQLite database file (default: build/classic_books.db)",
    )
    args = parser.parse_args()

    migrate(args.db_path)


if __name__ == "__main__":
    main()
