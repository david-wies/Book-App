# Source-Adapter Sync State & Resume

## Problem

The Gutenberg adapter caches `Last-Modified` in `QSettings`, decoupled from
the database. Two failure modes followed:

1. **Stale-cache + empty DB.** A user who deletes (or never had) `bookhub.db`
   keeps the cached timestamp. The next fetch sends `If-Modified-Since`, the
   server returns `304 Not Modified`, and the adapter never populates the DB.
   The user sees only the dev-only sample data.

2. **Force-close mid-fetch.** A successful 200 OK saves `Last-Modified` *before*
   the ~70K-entry archive finishes parsing. If the process is killed during
   extract/parse, some books are committed (each 500-book batch commits in
   `BookDiscoveryService`) and the cache is fully advanced. The next fetch sees
   304, leaving a permanently partial catalog until Gutenberg republishes.

## Goals

* Catalog freshness is tracked in the same store as the catalog itself.
* A non-completed fetch (crash, kill, network drop) is detected on next launch
  and the run does *not* assume freshness.
* In-progress fetches resume rather than restart from zero — both the network
  download (HTTP `Range`) and the archive parse (skip already-processed entries).
* Per-adapter — Ben-Yehuda and Archive adapters will plug into the same state
  machine.

## Non-goals

* True streaming resume during a single process run. If the process is alive,
  the existing logic continues; this design is about *cross-run* resumption.
* Adaptive chunking, multi-connection downloads, or partial-byte verification
  beyond what `If-Range` provides.

## Data model

New table, owned by `bookhub::db` and added in schema version 8:

```sql
CREATE TABLE IF NOT EXISTS sync_state (
    adapter_id        TEXT PRIMARY KEY,         -- 'gutenberg', 'benyehuda', ...
    status            TEXT NOT NULL
                      CHECK(status IN ('in_progress', 'completed', 'failed')),
    phase             TEXT
                      CHECK(phase IS NULL OR phase IN ('downloading', 'parsing')),
    last_modified     TEXT,                      -- Server validator from the last completed fetch (Last-Modified value or ETag)
    validator_type    TEXT                       -- 'last_modified' | 'etag' | NULL (legacy = treat as 'last_modified').
                      CHECK(validator_type IS NULL
                            OR validator_type IN ('last_modified', 'etag')),
                                                 -- Selects which conditional-request header the next fetch sends
                                                 -- (If-Modified-Since vs If-None-Match).
    started_at        TEXT,                      -- ISO 8601 of current/last attempt
    completed_at      TEXT,                      -- ISO 8601 of last successful completion
    books_processed   INTEGER NOT NULL DEFAULT 0,
    error_message     TEXT,
    -- Resume state (meaningful only while status='in_progress'):
    download_url      TEXT,                      -- URL being downloaded
    download_etag     TEXT,                      -- Server validator (Last-Modified or ETag) at download start; same kind as validator_type while in progress
    archive_path      TEXT,                      -- Cache file path
    bytes_downloaded  INTEGER NOT NULL DEFAULT 0,
    bytes_total       INTEGER NOT NULL DEFAULT 0,
    last_parsed_entry TEXT                       -- Last successfully committed tar entry name
);
```

`books_processed` is reset to 0 on every fresh fetch and counts books written
to the DB during the *current* attempt. It is monotonically increasing within
an attempt — a kill leaves it pointing at the last successful batch commit.

`last_parsed_entry` is the tar entry name (e.g. `cache/epub/1342/pg1342.rdf`)
of the last entry whose books were committed. It is updated *after* the DB
transaction commits, so it never points past a non-committed batch.

## State machine

```
                  ┌─────────────┐
                  │   <no row>  │
                  └──────┬──────┘
                         │ adapter starts first fetch
                         ▼
              ┌──────────────────────┐
              │ in_progress (download)│
              └──────────┬───────────┘
                         │ download complete (200/206/304-cache-hit)
                         ▼
              ┌──────────────────────┐
              │ in_progress (parse)  │
              └──────────┬───────────┘
                ┌────────┴────────┐
       success  │                 │ unrecoverable error
                ▼                 ▼
        ┌──────────────┐    ┌──────────────┐
        │  completed   │    │   failed     │
        └──────┬───────┘    └──────┬───────┘
               │ next fetch tick   │ next fetch tick
               ▼                   ▼
       (if last_modified           (re-fetch with no
        non-empty and DB has        If-Modified-Since)
        adapter books: send
        If-Modified-Since)
```

Force-close during either in_progress phase leaves the row at `in_progress`.
Next launch routes through the resume logic.

## Decision logic at `fetchBooks()`

1. Read `sync_state` for this adapter.
2. Count adapter-owned books (`SELECT COUNT(*) FROM books WHERE book_id LIKE 'gutenberg:%'`).
3. Branch:
   * **No row OR books == 0 OR status == 'failed':** start a fresh fetch
     (omit conditional headers). Write `status='in_progress'`, `phase='downloading'`,
     reset all resume fields, `started_at=now`, `books_processed=0`. **Clear
     `last_modified` and `validator_type`** — the prior fetch's validator is
     no longer authoritative for the new download.  If the server's 200
     response omits Last-Modified/ETag, those columns stay NULL and the next
     fetch will go unconditional rather than stamp new data with the old
     validator (which would produce stale `If-Modified-Since` comparisons).
   * **status == 'completed' AND books > 0:** conditional fetch. Send
     `If-Modified-Since: <last_modified>` when `validator_type='last_modified'`
     (or NULL — the v8 legacy default), or `If-None-Match: <last_modified>` when
     `validator_type='etag'`. Don't touch resume fields yet — only transition to
     `in_progress` on the 200 response, where we have a download to resume.
   * **status == 'in_progress' AND phase == 'downloading':** resume download
     (see below). Server validator is `download_etag`.
   * **status == 'in_progress' AND phase == 'parsing':** archive on disk is
     already complete; skip download, jump straight to parse with
     `last_parsed_entry` as the resume marker.

## Download with HTTP `Range`

* Stream the response body to disk at `QStandardPaths::CacheLocation/gutenberg/rdf-files.tar.bz2`.
  Open the `QFile` once at request start; flush periodically.
* Persist `bytes_downloaded` every 4 MB of received data and on `finished`/error.
  This bounds DB writes to a few hundred over an ~800 MB download.
* On the *first* attempt: GET, no `Range`. On `200 OK`, capture
  `Last-Modified` (preferred) or `ETag` (fallback) into `download_etag` so we
  can validate resumes, and set `validator_type` to the header that supplied
  the value (`'last_modified'` or `'etag'`).
* On *resume* (`bytes_downloaded > 0`, file exists, sizes match):
  send `Range: bytes=<bytes_downloaded>-` and `If-Range: <download_etag>`.
  * `206 Partial Content` → append from the existing offset.
  * `200 OK` → server's resource changed; truncate the cache file, reset
    `bytes_downloaded=0`, restart download from byte 0.
  * `304 Not Modified` (only possible if we also sent `If-Modified-Since`,
    which we do *not* during a resume) — not applicable in this path.
  * `416 Range Not Satisfiable` → cache file is past the live resource size
    (server rewound). Truncate, reset, restart.
  * Any other status / network error → leave state as-is and exit; the next
    poll tick (or app launch) tries again.
* On download completion: set `bytes_total = bytes_downloaded`, transition to
  `phase='parsing'`, set `last_parsed_entry = NULL`, reset `books_processed = 0`.
  The download is now durably cached and decoupled from the network.

## Parse with mid-archive resume

* Open the cached `rdf-files.tar.bz2` from disk. Pass a `QFile`-backed read
  callback to libarchive (no QNetworkReply this time).
* Iterate entries from the start of the archive — `.tar.bz2` is stream-only,
  so we cannot seek. For each entry:
  * Read `archive_entry_pathname`. If `last_parsed_entry` is set and
    `pathname <= last_parsed_entry` (lexicographic compare on the entry name,
    which matches Gutenberg's archive order), call `archive_read_data_skip()`
    and continue.
  * Otherwise parse the RDF and accumulate into the batch.
* Batch flush at 500 books *or* every 1000 archive entries (whichever first)
  so the resume marker stays current even if a particular archive section is
  RDF-light.
* On flush:
  1. Write the batch via the existing `BookDiscoveryService::onBooksDiscovered`
     path (idempotent inserts).
  2. After the batch transaction commits, update `last_parsed_entry` to the
     entry name of the *last* book in the batch and increment `books_processed`.
* On parse completion: set `status='completed'`, `completed_at=now`,
  `last_modified=<validator captured at download start>`,
  `validator_type=<validator_type captured at download start>`,
  clear `phase`, `download_url`, `download_etag`, `archive_path`,
  `bytes_downloaded`, `bytes_total`, `last_parsed_entry`. Delete the cached
  archive file. The validator + type pair carries over into the next
  conditional-fetch decision; passing empty values to `completeFetch` keeps
  whatever the row already had (the 304 path uses this to avoid clobbering
  the validator with an empty response).
* On parse failure (libarchive error, disk read error): set `status='failed'`,
  `error_message=...`. The next fetch tick routes through the
  `status == 'failed'` branch of `fetchBooks()` (see Decision logic above) and
  starts a fresh full fetch — no `If-Modified-Since`, resume cursors are reset
  by `beginFetch()`. Cached archive cleanup happens implicitly when the fresh
  download truncates it. We deliberately do *not* resume from
  `last_parsed_entry` after a `failed` status, because a parse error tends to
  indicate corruption upstream of the marker; restarting is the safer default.

## Idempotency

Re-inserting books is safe — `books.book_id` is `PRIMARY KEY` and inserts use
`INSERT OR IGNORE` (and the deduplication pass in `BookDiscoveryService`
unifies records sharing identifiers). A resumed parse that overlaps the
previous run by one entry boundary will not produce duplicate rows.

## One-time migration of existing `QSettings` cache

The v7→v8 migration step:

1. Creates the `sync_state` table.
2. If `~/.config/<Org>/<App>.conf` has `gutenberg_last_modified`, copies it
   into a `sync_state` row with `status='completed'`, `last_modified=<value>`,
   `completed_at=NULL`. **Then** clears the `QSettings` key so no future code
   path reads the now-stale value.
3. Stamps `PRAGMA user_version = 8`.

Note: the migration *cannot* read `QSettings` from the C++ migration code in
`database.cpp` because the migration runs before `QApplication` may exist in
some build configurations. The cache-copy step lives in the app startup path
in `main.cpp`, gated on "the migration just bumped to v8" — implementation
will record a flag.

## Failure-mode coverage

| Scenario | Behavior |
|----------|----------|
| Fresh user (no DB, no cache) | No `sync_state` row → full fetch, no conditional GET |
| Fresh DB but stale `QSettings` | Migration runs, copies cached timestamp into `sync_state`. Empty-DB guard triggers full fetch anyway. |
| Kill during download | `status='in_progress', phase='downloading'`. Next launch: `Range`-based resume. |
| Kill during parse | `status='in_progress', phase='parsing'`. Next launch: skip cache check, jump to parse from `last_parsed_entry`. |
| Network drop during download | Same as kill during download |
| Server resource changed mid-resume | `If-Range` mismatch → server sends `200` instead of `206` → we truncate cache and restart |
| Catalog truly up-to-date | `status='completed' AND books > 0` → conditional GET → `304` → no-op |
| Periodic-poll re-entry | `BookDiscoveryService::m_activeFetches` guard already prevents overlapping runs |

## Threading

All `sync_state` reads/writes happen on the collector thread, using the
`collector_connection` `QSqlDatabase`. The adapter is created on the collector
thread (see `CollectorWorker::run`), so it can call `bookhub::db` helpers
directly.

## Schema-doc consistency

This change updates these documents to stay aligned:
* `docs/design/book-identity-model.md` — no change needed; the deduplication
  contract is unchanged and resume is invisible to it.
* `docs/static-analysis-tools.md` — no change.
* `CLAUDE.md` — Architecture section needs a sentence noting the new table
  and that catalog freshness lives in the DB. Update under "Database Schema".
* `tools/migrate_db.py` — extend the docstring header to mention v7→v8 and
  that this migration is auto-applied at startup.
