# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Onboarding (run once after cloning)

```bash
git config core.hooksPath .githooks
```

This activates the pre-push hook in `.githooks/pre-push`. The hook enforces two rules locally:

1. **No direct push to `master`** — any push that targets `master` as the remote ref is rejected, regardless of who runs it or which branch they are on.
2. **Source must be `develop` or `release/*`** — if the remote destination is `master`, only the `develop` branch or a `release/vX.Y.Z` branch (created by `tools/prepare-release.sh`) is a valid source. Pushes from `feature/*`, `fix/*`, or any other branch are blocked with a clear error message pointing to the correct workflow.

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

**What GitHub cannot enforce natively (free/personal plan limitation):** GitHub's branch protection API does not support filtering merges by the *source branch name*. There is no setting that says "only PRs from `develop` or `release/*` may target `master`". The pre-push hook is the primary enforcement for this rule. The GitHub protection layer blocks all *direct* pushes (including `git push origin develop:master`) and requires CI to pass, but it cannot reject a PR opened from `feature/foo` to `master` — that must be caught by process (code review) and the pre-push hook (which fires before the push that creates such a PR's comparison branch).

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

**During active development — build-time analysis:**
Opt into inline clang-tidy and clazy warnings while you build so issues surface immediately rather than at PR time. Always generate `compile_commands.json` so both tools find Qt includes:
```bash
cmake -B build \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_CLANG_TIDY="clang-tidy" ..
cmake --build build
```
To add clazy on top, replace the tidy value with `"clazy-standalone;-checks=level1"`. This is opt-in — the default `cmake -B build ..` stays fast.

**When running tests — sanitizers:**
Run the test suite under ASan+UBSan habitually during development, not only in CI:
```bash
cmake -B build-san -DENABLE_SANITIZERS=ON ..
cmake --build build-san
cd build-san && ctest --output-on-failure
```
Run TSan in its own build whenever you touch code that crosses the GUI/Query/Collector thread boundaries — it is incompatible with ASan:
```bash
cmake -B build-tsan -DENABLE_TSAN=ON ..
cmake --build build-tsan
cd build-tsan && ctest --output-on-failure
```
The CMake options (`ENABLE_SANITIZERS`, `ENABLE_TSAN`, etc.) are defined in `docs/static-analysis-tools.md` — add them to `CMakeLists.txt` if not yet present.

**Before starting a refactor — complexity check:**
Run Lizard on the area you are about to modify. Functions with cyclomatic complexity > 10 or length > 60 lines are high-risk targets — understand them before adding more:
```bash
source tools/venv/bin/activate
lizard src/path/to/target/ --CCN 10 --length 60 --warnings_only
```

**After adding or removing code — include audit:**
Run IWYU after any change that adds, removes, or restructures `#include` directives to keep compile times and dependency footprint minimal:
```bash
iwyu_tool.py -p build src/ | fix_includes.py --dry_run
```
Review the suggestions before applying; IWYU can be aggressive with Qt headers.

**Pre-PR — full static analysis** (details in Git workflow below)**:**
clang-tidy, clazy, cppcheck. See the numbered checklist in the Git workflow convention.

**Python-based tools** (lizard, iwyu_tool.py, codechecker, etc.) must use the project venv at `tools/venv/`. Activate it before running any Python analysis tool:
```bash
python3 -m venv tools/venv        # once
source tools/venv/bin/activate
pip install -r tools/requirements.txt lizard codechecker
```
Add any new Python tool dependencies to `tools/requirements.txt`.

Full tool reference: `docs/static-analysis-tools.md`.

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
- **Debugging:** Match the tool to the symptom. For memory errors (crash, corruption, leak): ASan. For undefined behaviour (integer overflow, bad cast, misaligned access): UBSan. For uninitialized reads: MSan (clang-only build). For race conditions in the 3-thread model: TSan (separate build from ASan). For allocation growth or hotspots without recompiling: `heaptrack`. For deep cross-function analysis without a special build: Valgrind (slow but zero recompile). See `docs/static-analysis-tools.md` for build flags and run commands.
- **Git workflow:** Every piece of work — feature, bugfix, or chore — gets its own short-lived branch cut from `develop` (e.g. `feature/task-7-search-screen`, `fix/collector-crash`, `chore/update-deps`). Keep branches small and focused: one task per branch. Merge back to `develop` via PR; never commit directly to `develop` or `master`. Both branches are protected and require PRs (0 approvals — bump to 1 in GitHub settings when a second contributor joins). **When starting a task** that modifies existing code, run Lizard on the target area first. Functions above the complexity threshold are high-risk — understand them before changing them, and avoid increasing their score:
```bash
source tools/venv/bin/activate && lizard src/path/to/target/ --CCN 10 --length 60 --warnings_only
```

**Before creating a PR, complete all of the following steps in order:**
1. **Design comparison** — for every changed source file, check whether a corresponding design doc exists under `docs/design/` or a spec under `spec/`. If one does, read it and verify the implementation matches the specified behaviour, interfaces, and constraints. Flag any divergence before proceeding.
2. **clang-tidy** — `run-clang-tidy -p build src/ tests/`; fix all warnings.
3. **clazy** — `clazy-standalone -checks=level1 -p build $(find src/ -name '*.cpp')`; fix all warnings.
4. **cppcheck** — `cppcheck --enable=all --std=c++23 --error-exitcode=1 --suppress=missingIncludeSystem --suppress=missingInclude -I src/ src/`; fix all errors.
5. **`/review`** — run the slash command to perform a full code review of the branch changes and address any issues found. If `/review` finds any issues — even minor ones — post a comment on the PR summarising the findings: `gh pr comment <number> --body "..."`.
6. **Test plan** — if the PR description includes a test plan, execute every step and confirm each item passes before marking the review complete.

Python-based analysis tools must run inside `tools/venv/` — see the Testing & Linting section above. The only permitted paths into `master` are a PR from `develop` (standard) or a PR from `release/vX.Y.Z` (release prep via `tools/prepare-release.sh`) — both enforced by the `.githooks/pre-push` hook (local) and GitHub branch protection (server-side). No other branch may target `master` directly.
- **Issue linking:** If a PR resolves a GitHub issue, link the issue in the PR description (e.g. `Closes #42`). Once the PR is merged, post a comment on the issue with a short paragraph explaining what was implemented and a note that the issue is now closed as part of completing the PR, then close the issue with `gh issue close <number>`.
- **GitHub task checkboxes:** When a PR or issue contains a task list (markdown checkboxes), mark each item `[x]` as soon as it is done — do not batch them at the end. Use `gh api` to edit the body in place: fetch the current body, replace `[ ]` with `[x]` for the completed item, then PATCH it back.
- **Branch cleanup:** After any PR that is not `develop → master` is merged, delete the source branch — it is no longer needed. Use `gh pr view <number> --json headRefName` to get the branch name, then `git push origin --delete <branch>` (or `gh api` equivalent) to remove it from the remote.
- **Release process:** `master` must never contain develop-only files (`docs/`, `spec/`, `tools/`, `CLAUDE.md`, `.githooks/`, `.clangd`, etc.). These are stripped automatically via `tools/prepare-release.sh` before each `develop → master` merge. Full release steps:
  1. From `develop`: `tools/prepare-release.sh v1.2.3` — creates `release/v1.2.3`, removes all paths in `.github/release-strip.txt`, and opens a PR to `master`.
  2. Review the PR, confirm the stripped file list is correct, wait for CI.
  3. Merge the PR on GitHub. Delete the `release/v1.2.3` branch.
  4. Tag the resulting `master` commit to trigger the GitHub Release workflow: `git fetch origin && git tag -a v1.2.3 origin/master -m "Release v1.2.3" && git push origin v1.2.3`.
  5. The `release.yml` workflow builds a Release-mode binary, packages it as `bookhub-v1.2.3-linux-x86_64.tar.gz` with a SHA-256 checksum, and publishes a GitHub Release.
  To add or remove a file from the strip list, edit `.github/release-strip.txt` and update `.gitattributes` (the `export-ignore` entries) to match.
- **Build artifacts:** The icon is embedded as a Qt resource (`.qrc`). The database is stored in `QStandardPaths::AppDataLocation`. The `build/` directory is git-ignored.
- **Docs:** When changing a public API, adding a feature, or altering architecture, update the relevant files in `docs/`. The `docs/` directory is for internal use and will be removed before release. When creating or modifying a file in `docs/design/` or `spec/`, review all other docs and spec files for any content that references or overlaps with the changed area and update them to stay consistent — DDL blocks, thread counts, architecture descriptions, and status fields are common drift points.
- **License:** All third-party dependencies must be MIT, BSD, Apache 2.0, or similarly permissive. GPL and LGPL dependencies are forbidden — they would force the application to be GPL-licensed. Always verify a library's license before adding it. TTS uses three engines behind the `TTSService` interface: `NativeTTSService` (built-in, no external dependencies) as the active fallback while model packaging is prepared; [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) (Apache 2.0) as the target engine for preset voices; and [PocketTTS.cpp](https://github.com/VolgaGerm/PocketTTS.cpp) (MIT) for custom voice cloning. Do not introduce the Piper GPL fork (`OHF-Voice/piper1-gpl`).
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

Tasks 1–12, 16, 19 complete: project scaffolding, SQLite schema, single-executable build, background collector with Gutenberg adapter, application icon, Library screen, Search screen, Explore screen, BookDetailsPanel, add/remove from library, download flow, audiobook conversion flow UI scaffold, UI polish, multiplatform support.

Task 15 in progress: source adapter extensibility refactor (BenYehuda and Archive adapters pending).

Tasks 13 complete: TTS integration — NativeTTSService (Phase 1), SherpaOnnxTTSService and PocketTTSService stubs with model-path discovery and graceful fallback (Phase 2/3 architecture ready; actual synthesis awaits Task 22 model management). In-app WAV preview via Qt Multimedia (BOOKHUB_HAVE_MULTIMEDIA guard), preview button toggle, 3-second playback gate, all code review fixes applied.

Tasks 14, 17–18, 20–25 not started: custom voice upload, packaging, tooltip polish, AudiobookService/VoiceService, ebook text extraction, TTS model management, cover art, configurable collector schedule, search chips.
