# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Onboarding (run once after cloning)

```bash
git config core.hooksPath .githooks
```

This activates the pre-push hook in `.githooks/pre-push`. The hook enforces two rules locally:

1. **No direct push to `master`** — any push that targets `master` as the remote ref is rejected, regardless of who runs it.
2. **Source must be `develop`** — if the remote destination is `master`, the local branch being pushed must be `develop`. Pushes of `feature/*`, `fix/*`, or any other branch directly to `master` are blocked with a clear error message pointing to the correct workflow.

Emergency bypass (release engineer only): `SKIP_BRANCH_GUARD=1 git push ...`

**GitHub branch protection** on `master` (enforces the same rules server-side, so the hook cannot be bypassed by skipping local hooks or using the GitHub web UI):

| Setting | Value |
|---------|-------|
| Require pull request before merging | Yes (0 approvals — bump to 1 when a second contributor joins) |
| Dismiss stale reviews on new push | Yes |
| Require branch up to date before merge | Yes (`strict` mode) |
| Required status checks | `build` (CI must pass) |
| Require conversation resolution | Yes |
| Allow force pushes | No |
| Allow deletions | No |
| Enforce for admins | Yes |
| Linear history required | Yes |

**What GitHub cannot enforce natively (free/personal plan limitation):** GitHub's branch protection API does not support filtering merges by the *source branch name*. There is no setting that says "only PRs from `develop` may target `master`". The pre-push hook is the primary enforcement for this rule. The GitHub protection layer blocks all *direct* pushes (including `git push origin develop:master`) and requires CI to pass, but it cannot reject a PR opened from `feature/foo` to `master` — that must be caught by process (code review) and the pre-push hook (which fires before the push that creates such a PR's comparison branch).

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
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6 libqt6network6 libarchive-dev
```

Requires C++23, GCC 13+/Clang 16+, Qt6 (Widgets, Sql, Network modules), and libarchive (used by the Gutenberg adapter for in-process `.tar.bz2` extraction).

**Database migration:** If the app shows a schema version mismatch error at startup, run:
```bash
python3 tools/migrate_db.py
```
This upgrades an existing `bookhub.db` to the current schema version. See `src/shared/database.cpp` for the migration SQL.

## Testing & Linting

The test suite uses Qt Test. Ten targets are registered with CTest across `unit`, `integration`, and `gui` categories; the `sanity` label marks the fast-gate subset. Run with `ctest -L sanity` (fast) or `ctest` (full suite) from the build directory.

**Static analysis:** clang-tidy is used for linting. Run it from the build directory before opening a PR:
```bash
run-clang-tidy -p build src/ tests/
```
Fix all warnings before creating the PR. Common checks enforced: `const &` on value-by-copy parameters, `qsizetype` for Qt string index returns, `static_cast` for signed/unsigned conversions, and `std::move` on last-use locals passed to `emit`.

## Architecture

**BookHub** is a native C++/Qt6 desktop application for discovering public-domain books (initially from Project Gutenberg), managing a personal library, downloading ebook formats, and converting books to audiobooks via TTS.

### Three-Thread Model

The app runs three threads in one process:

- **GUI Thread** — `QApplication::exec()` drives a `QMainWindow`. All widget interaction happens here.
- **Query Thread** — `QueryWorker : QObject` (moved to a `QThread` via `moveToThread()`) executes all GUI database queries. `MainWindow` owns the thread and the worker. The worker opens `"gui_query_connection"` in `onThreadStarted()` and closes it in `onThreadFinished()`. Services (`SearchService`, `LibraryService`, `ExploreService`) route requests to the worker via Qt signals and receive results back on the GUI thread via `Qt::AutoConnection` (queued cross-thread, direct same-thread in tests).
- **Collector Thread** — `CollectorWorker : QThread` polls for book metadata every 10 minutes and writes to SQLite. Starts fetching immediately on launch.

All three threads share a single SQLite database (`bookhub.db`, stored in `QStandardPaths::AppDataLocation`). WAL mode enables concurrent reads from the query thread while the collector writes. Thread isolation is maintained by giving each thread its own named Qt SQL connection: `QSqlDatabase::defaultConnection` for the GUI, `"gui_query_connection"` for the query thread, and `"collector_connection"` for the collector.

**Testing:** `TestQueryWorker` (in `tests/support/test_query_worker.h`) is a subclass of `QueryWorker` that overrides all `handle*Request` slots to call the same `internal::*` free functions but target `QSqlDatabase::defaultConnection`. It is never moved to a thread — staying on the GUI thread causes `Qt::AutoConnection` to resolve to `Qt::DirectConnection`, making the entire async chain synchronous in tests without `QTest::qWait`.

### Data Source Adapter Pattern

`ISourceAdapter` (pure abstract `QObject`) defines the interface for all book metadata sources. New sources implement it and register with `BookDiscoveryService::addAdapter()`. Currently only `GutenbergAdapter` is implemented.

`GutenbergAdapter` downloads `rdf-files.tar.bz2` from Gutenberg, extracts it in-process via libarchive, and parses the RDF/XML files. It uses `If-Modified-Since` HTTP caching (stored in `QSettings`) to skip re-downloads. Books are emitted in batches of 500 via the `booksDiscovered` signal to bound memory usage.

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
- **Git workflow:** Every piece of work — feature, bugfix, or chore — gets its own short-lived branch cut from `develop` (e.g. `feature/task-7-search-screen`, `fix/collector-crash`, `chore/update-deps`). Keep branches small and focused: one task per branch. Merge back to `develop` via PR; never commit directly to `develop` or `master`. Both branches are protected and require PRs (0 approvals — bump to 1 in GitHub settings when a second contributor joins). **Before creating a PR, always run clang-tidy (`run-clang-tidy -p build src/ tests/`) and fix all warnings, then run `/review` to perform a code review of the branch changes and address any issues found.** The only permitted path into `master` is a PR from `develop` — this is enforced both by the `.githooks/pre-push` hook (local) and GitHub branch protection (server-side). No other branch may target `master` directly.
- **Issue linking:** If a PR resolves a GitHub issue, link the issue in the PR description (e.g. `Closes #42`). Once the PR is merged, post a comment on the issue with a short paragraph explaining what was implemented and a note that the issue is now closed as part of completing the PR, then close the issue with `gh issue close <number>`.
- **Branch cleanup:** After any PR that is not `develop → master` is merged, delete the source branch — it is no longer needed. Use `gh pr view <number> --json headRefName` to get the branch name, then `git push origin --delete <branch>` (or `gh api` equivalent) to remove it from the remote.
- **Build artifacts:** The icon is embedded as a Qt resource (`.qrc`). The database is stored in `QStandardPaths::AppDataLocation`. The `build/` directory is git-ignored.
- **Docs:** When changing a public API, adding a feature, or altering architecture, update the relevant files in `docs/`. The `docs/` directory is for internal use and will be removed before release. When creating or modifying a file in `docs/design/` or `spec/`, review all other docs and spec files for any content that references or overlaps with the changed area and update them to stay consistent — DDL blocks, thread counts, architecture descriptions, and status fields are common drift points.
- **License:** All third-party dependencies must be MIT, BSD, Apache 2.0, or similarly permissive. GPL and LGPL dependencies are forbidden — they would force the application to be GPL-licensed. Always verify a library's license before adding it. TTS uses [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0) for preset voices and [PocketTTS.cpp](https://github.com/VolgaGerm/PocketTTS.cpp) (MIT) for custom voice cloning. Do not introduce the Piper GPL fork (`OHF-Voice/piper1-gpl`).
- **QProcess:** Do not use `QProcess` anywhere in the codebase. The earlier `tar` subprocess in `GutenbergAdapter` was replaced with a direct libarchive link. Prefer linking against libraries directly, using Qt's networking/IO APIs, or `QThread`/`QThreadPool` for background work.

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

Tasks 1–8 complete: project scaffolding, SQLite schema, single-executable build, background collector with Gutenberg adapter, application icon, Library screen, Search screen, Explore screen.

Tasks 9–17 not started: Book Details screen, library management, download flow, audiobook/TTS conversion, additional source adapters, UI polish, and packaging.
