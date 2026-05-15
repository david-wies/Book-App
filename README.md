# BookHub

A native C++/Qt6 desktop application for discovering public-domain literature, managing a personal library, downloading ebooks, and generating audiobooks via text-to-speech.

## Features

- **Discovery** — Browse and search books across multiple public-domain sources (starting with Project Gutenberg).
- **Library Management** — Save books to a local library for quick access and offline reading.
- **Multi-Format Downloads** — Fetch books in EPUB, PDF, HTML, plain text, and more.
- **Audiobook Conversion** — *(In Progress)* Convert text editions into audiobooks using high-quality, on-device TTS voices.
- **Background Sync** — A dedicated collector thread refreshes metadata every 10 minutes without blocking the UI. Per-adapter sync state is persisted in the database so an interrupted fetch (kill, crash, network drop) resumes the download and parse on the next launch instead of starting over.

## Architecture

The application uses a three-thread model:

- **GUI Thread**: Runs `QApplication::exec()` with the Qt6 Widgets interface.
- **Query Thread**: Dedicated background thread executing all SQLite read/write queries asynchronously. Services send requests via Qt signals and receive results on the GUI thread.
- **Collector Thread**: Background thread polling external book sources.

All three threads share a single SQLite database (`bookhub.db`, stored in `QStandardPaths::AppDataLocation`). WAL mode enables concurrent reads from the query thread while the collector writes. Thread isolation is maintained by giving each thread its own named Qt SQL connection.

Book metadata flows through an adapter pattern. `ISourceAdapter` defines the interface; `GutenbergAdapter` is the only current implementation. It downloads Gutenberg's RDF catalog (`.tar.bz2`) to a cache directory (`QStandardPaths::CacheLocation`), parses it in-process via libarchive, and emits books in batches of 500. `BookDiscoveryService` writes each batch to the database in a single transaction with per-book `SAVEPOINT`s for failure isolation.

Catalog freshness and resume cursors live in the `sync_state` table — download progress (`Range`/`If-Range`) and parse progress (`last_parsed_entry`) are persisted at safe boundaries so a force-close never costs more than the in-flight batch. See `docs/design/sync-state-resume.md` for the state machine, and `docs/design/` for the rest of the architecture documents.

## Prerequisites

Requires GCC 13+/Clang 16+ with C++23 support.

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6 libqt6network6 libarchive-dev
```

## Supported Platforms

| Platform | Status |
|----------|--------|
| Linux (Ubuntu/Debian) | Primary development target |
| Windows | In progress |
| macOS | In progress |

## Build & Run

```bash
# Configure (out-of-tree build is required — never run cmake from the source root)
mkdir -p build && cd build
cmake ..

# Build
cmake --build . --parallel

# Run (Linux/macOS)
./build/bookhub

# Run (Windows)
build\bookhub.exe
```

The application creates and initializes `bookhub.db` in the platform-appropriate app data directory on first run. If you see a schema version mismatch error after upgrading, run:

```bash
# Linux:   ~/.local/share/BookHub/BookHub/bookhub.db
# macOS:   ~/Library/Application Support/BookHub/BookHub/bookhub.db
# Windows: %APPDATA%\BookHub\BookHub\bookhub.db
python3 tools/migrate_db.py --db-path <path above>
```

`migrate_db.py` auto-detects the platform path when `platformdirs` is installed (`pip install platformdirs`); pass `--db-path` to override.

## Testing

BookHub uses `Qt6::Test` for unit, integration, and GUI testing. The suite is divided into tiers to ensure fast feedback during development while maintaining high release-quality confidence.

### Running Tests

```bash
# Run the fast sanity tier (< 5 s)
cd build && ctest -L sanity --output-on-failure

# Run the full suite
cd build && ctest --output-on-failure
```

### Documentation

- **[Test Strategy](docs/design/test-strategy.md)** — Comprehensive plan covering goals, layers, and implementation quality rules.
- **[Sanity-Check Test Tier](docs/design/sanity-check-test-tier.md)** — Detailed policy on tiering, selection rules, and CI integration.

### CI Integration

Pull requests targeting `develop` run only the `sanity` tier. Pull requests targeting `master` must pass the `full` suite before merging.


## Project Layout

```text
.
├── CMakeLists.txt
├── LICENSE                      # MIT license
├── resources.qrc                # Qt resource manifest (embeds app icon)
├── book_reader_icon.jpg         # Application icon (embedded in executable)
├── spec/                        # Feature specifications
├── docs/                        # Internal design documents
├── tools/                       # Maintenance scripts (e.g. migrate_db.py)
├── tests/
│   ├── support/                 # TestDatabase, isolateSettings helpers
│   ├── unit/                    # Pure-logic tests (no Qt event loop)
│   ├── integration/             # Tests that hit a real SQLite database
│   └── gui/                     # Widget tests (offscreen Qt platform)
└── src/
    ├── main.cpp                  # Entry point: init DB, run QSettings→sync_state migration, start collector, show window
    ├── shared/                   # Database schema and utilities (static library)
    │   └── database.cpp/.h               # Schema, migrations, sync_state helpers, language/format normalisation
    ├── collector/                # Background metadata discovery
    │   ├── source_adapter.cpp/.h        # ISourceAdapter interface + DiscoveredBook
    │   ├── gutenberg_adapter.cpp/.h     # Project Gutenberg RDF catalog adapter (Range/If-Range resume, mid-archive skip)
    │   ├── book_discovery_service.cpp/.h # Orchestrates adapters, writes batches in one transaction + per-book SAVEPOINTs
    │   └── collector_worker.cpp/.h      # QThread wrapper with polling timer
    └── gui/                      # Qt6 Widgets UI
        ├── main_window.cpp/.h           # QMainWindow; hosts the screen stack
        ├── query_worker.cpp/.h          # All GUI-thread DB queries (moved to a dedicated QThread)
        ├── style_tokens.h               # Design token constants
        ├── screens/                     # Top-level navigable screens
        │   ├── library_screen.cpp/.h
        │   ├── search_screen.cpp/.h
        │   └── explore_screen.cpp/.h
        ├── panels/                      # Reusable composite views
        │   └── book_details_panel.cpp/.h
        ├── dialogs/                     # Modal workflows
        │   ├── download_flow_dialog.cpp/.h
        │   ├── audiobook_flow_dialog.cpp/.h
        │   └── voice_upload_dialog.cpp/.h
        ├── widgets/
        │   ├── badge_label.cpp/.h
        │   ├── book_card_delegate.cpp/.h
        │   ├── empty_state_widget.cpp/.h
        │   ├── mini_audio_player_widget.cpp/.h
        │   ├── search_result_delegate.cpp/.h
        │   ├── step_indicator_widget.cpp/.h
        │   └── voice_selector_widget.cpp/.h
        ├── services/                    # Async facades around QueryWorker; plus TTS engines
        │   ├── library_service.cpp/.h
        │   ├── search_service.cpp/.h
        │   ├── explore_service.cpp/.h
        │   ├── book_details_service.cpp/.h
        │   ├── tts_service.cpp/.h           # ITTSService interface
        │   ├── tts_types.h
        │   ├── native_tts_service.cpp/.h    # Built-in fallback engine
        │   ├── sherpa_onnx_tts_service.cpp/.h # Apache 2.0 preset-voice engine
        │   └── pocket_tts_service.cpp/.h    # MIT zero-shot voice-cloning engine
        └── utils/
            └── wav_utils.h
```

## Branching and Update Rules

### Branch structure

| Branch | Purpose |
|--------|---------|
| `master` | Stable, release-ready code. Never commit directly. |
| `develop` | Integration branch. All work merges here first. |
| `feature/<name>` | New features, cut from `develop`. |
| `fix/<name>` | Bug fixes, cut from `develop`. |
| `chore/<name>` | Non-functional changes (deps, CI, docs), cut from `develop`. |

### Workflow

1. Cut a short-lived branch from `develop`: `git checkout -b feature/my-feature develop`
2. Make changes in small, focused commits.
3. Run `/review` before opening a PR to catch issues early.
4. Open a PR targeting `develop`. CI must pass.
5. Merge to `develop` via PR — never push directly.
6. Releases land on `master` exclusively via a PR from `develop`.

Keep branches small and focused — one task per branch. After merging, delete the branch.

### Protections

A pre-push hook (`.githooks/pre-push`) enforces these rules locally:

- Direct pushes to `master` are rejected.
- The only allowed source branch for a push targeting `master` is `develop`.

Activate the hook after cloning:

```bash
git config core.hooksPath .githooks
```

GitHub branch protection mirrors these rules server-side: direct pushes are blocked, CI must pass, and all conversations must be resolved before merging.

Emergency bypass (release engineer only): `SKIP_BRANCH_GUARD=1 git push ...`

## License

MIT © 2026 BookHub Contributors
