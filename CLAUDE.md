# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Onboarding (run once after cloning)

```bash
git config core.hooksPath .githooks
```

This activates the pre-push hook that blocks direct pushes to `master`. All changes to `master` must go through a pull request from `develop`.

## Build & Run

```bash
# Configure (out-of-tree build is required — never run cmake from the source root)
mkdir -p build && cd build && cmake ..

# Build
make -j$(nproc)

# Run
./build/bookhub
```

**Dependencies:**
```bash
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6
```

Requires C++23, GCC 13+/Clang 16+, and Qt6 (Widgets, Sql, Network modules). The `tar` binary must be present at runtime (used by the Gutenberg adapter via `QProcess`).

## Testing & Linting

There is no automated test suite — manual verification is required. The CI runs `ctest` but finds no tests. No linter or static analysis is configured.

## Architecture

**BookHub** is a native C++/Qt6 desktop application for discovering public-domain books (initially from Project Gutenberg), managing a personal library, downloading ebook formats, and converting books to audiobooks via TTS.

### Two-Thread Model

The app runs two threads in one process:

- **GUI Thread** — `QApplication::exec()` drives a `QMainWindow`. All widget interaction happens here.
- **Collector Thread** — `CollectorWorker : QThread` polls for book metadata every 10 minutes and writes to SQLite. Starts fetching immediately on launch.

Both threads share a single SQLite database (`bookhub.db`, placed next to the executable). Thread isolation is maintained by giving each thread its own named Qt SQL connection: `QSqlDatabase::defaultConnection` for the GUI and `"collector_connection"` for the collector.

### Data Source Adapter Pattern

`ISourceAdapter` (pure abstract `QObject`) defines the interface for all book metadata sources. New sources implement it and register with `BookDiscoveryService::addAdapter()`. Currently only `GutenbergAdapter` is implemented.

`GutenbergAdapter` downloads `rdf-files.tar.bz2` from Gutenberg, extracts it via a `tar` subprocess, and parses the RDF/XML files. It uses `If-Modified-Since` HTTP caching (stored in `QSettings`) to skip re-downloads. Books are emitted in batches of 500 via the `booksDiscovered` signal to bound memory usage.

`BookDiscoveryService` orchestrates adapters and writes discovered books to the database in those same 500-book batches.

### Database Schema

SQLite with foreign keys enabled (`PRAGMA foreign_keys = ON`). Tables:

- `books` — `book_id TEXT PRIMARY KEY` (prefixed identifier string, e.g. `"lccn:n78095332"`, `"gutenberg:1342"`; never a bare ISBN)
- `book_identifiers` — all known identifiers for a book (`type`, `value`); used for cross-source deduplication before insert
- `editions` — (`book_id`, `language`) UNIQUE; language lives here, not in `books`
- `genres`, `book_genres` — many-to-many junction; `book_genres.book_id` FK to `books`
- `formats` — (`edition_id` FK); format type (epub, pdf, txt, …) hangs off editions
- `sources` — (`format_id` FK); download URL per (format, source) pair
- `library_items` — (`book_id` FK, `edition_id` FK); user's personal library state

**ID resolution priority:** LCCN > OCLC > ISBN > source-specific fallback (e.g. `gutenberg:1342`). The `GutenbergAdapter` extracts `dcterms:identifier` fields from RDF files to resolve the highest-priority available ID. Before inserting a new `books` row, `BookDiscoveryService` queries `book_identifiers` for any matching identifier to deduplicate records from multiple sources.

Full ID model: `docs/design/book-identity-model.md`. Schema DDL: `src/shared/database.cpp` (`bookhub::db::createSchema()`).

### Key Conventions

- **Namespaces:** All code lives under `bookhub::`, with sub-namespaces `bookhub::db`, `bookhub::collector`, and `bookhub::gui`.
- **Shutdown coordination:** Uses `std::atomic_bool` flags (`m_shutdownRequested`, `m_updateInProgress`) and a mix of `Qt::QueuedConnection` / `Qt::DirectConnection` for safe cross-thread teardown.
- **Comments:** Explain *why*, not *what*. Use tags: `TODO:`, `FIXME:`, `HACK:`, `NOTE:`, `WARNING:`, `PERF:`, `SECURITY:`.
- **Git workflow:** Every piece of work — feature, bugfix, or chore — gets its own short-lived branch cut from `develop` (e.g. `feature/task-7-search-screen`, `fix/collector-crash`, `chore/update-deps`). Keep branches small and focused: one task per branch. Merge back to `develop` via PR; never commit directly to `develop` or `master`. Both branches are protected and require PRs (0 approvals — bump to 1 in GitHub settings when a second contributor joins).
- **Build artifacts:** The icon and database are co-located with the executable via a CMake `POST_BUILD` copy. The `build/` directory is git-ignored.
- **Docs:** When changing a public API, adding a feature, or altering architecture, update the relevant files in `docs/`. The `docs/` directory is for internal use and will be removed before release.
- **License:** All third-party dependencies must be MIT, BSD, Apache 2.0, or similarly permissive. GPL and LGPL dependencies are forbidden — they would force the application to be GPL-licensed. Always verify a library's license before adding it. TTS uses [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0) for preset voices and [PocketTTS.cpp](https://github.com/VolgaGerm/PocketTTS.cpp) (MIT) for custom voice cloning. Do not introduce the Piper GPL fork (`OHF-Voice/piper1-gpl`).
- **QProcess:** Minimise use of `QProcess`. It is currently used only for the `tar` subprocess in `GutenbergAdapter` and must not be introduced elsewhere without a compelling reason. Prefer linking against libraries directly, using Qt's networking/IO APIs, or `QThread`/`QThreadPool` for background work.

## Custom Commands

Project-specific slash commands live in `.claude/commands/`:

| Command | Purpose |
|---------|---------|
| `/cpp` | C++ expert mode — modern C++23, Core Guidelines, DDD |
| `/debug` | Structured debug mode — reproduce → root cause → fix → verify |
| `/db` | Database specialist — SQLite schema, queries, migrations |
| `/think` | Critical thinking mode — questions only, no solutions |
| `/plan` | Context architect — map dependencies before making changes |
| `/deep` | Autonomous mode — work to completion without stopping |
| `/design` | MVP/architecture design — produces docs in `docs/design/` |
| `/spec` | Specification writer — produces files in `spec/` |
| `/decompose` | Break a design into tasks — produces `docs/tasks/task-breakdown.md` |
| `/brainstorm` | App idea generator — interactive questioning toward a spec |
| `/describe-image` | Generate a text description of an image |

### Current Status

Tasks 1–5 complete: project scaffolding, SQLite schema, single-executable build, background collector with Gutenberg adapter, application icon.

Tasks 6–17 not started: all GUI screens (Library, Search, Explore, Book Details), library management, download flow, audiobook/TTS conversion, additional adapters, and packaging.
