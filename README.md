# BookHub

A native C++/Qt6 desktop application for discovering public-domain literature, managing a personal library, downloading ebooks, and generating audiobooks via text-to-speech.

## Features

- **Discovery** — Browse and search books across multiple public-domain sources (starting with Project Gutenberg).
- **Library Management** — Save books to a local library for quick access and offline reading.
- **Multi-Format Downloads** — Fetch books in EPUB, PDF, HTML, plain text, and more.
- **Audiobook Conversion** — *(In Progress)* Convert text editions into audiobooks using high-quality, on-device TTS voices.
- **Background Sync** — A dedicated collector thread refreshes metadata every 10 minutes without blocking the UI.

## Architecture

BookHub runs two threads in a single process:

- **GUI Thread** — `QApplication::exec()` drives a `QMainWindow`. All widget interaction lives here.
- **Collector Thread** — `CollectorWorker : QThread` polls for book metadata on a timer and writes results to SQLite. It starts immediately on launch.

Both threads share a single SQLite database (`bookhub.db`, stored in `QStandardPaths::AppDataLocation`). Thread isolation is maintained by giving each thread its own named Qt SQL connection.

Book metadata flows through an adapter pattern. `ISourceAdapter` defines the interface; `GutenbergAdapter` is the only current implementation. It downloads Gutenberg's RDF catalog (`.tar.bz2`), extracts it in-process via libarchive, and parses the RDF/XML files. `BookDiscoveryService` orchestrates adapters and writes discovered books to the database in batches of 500.

See `docs/design/` for detailed architecture documents.

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

```bash
# Run the fast sanity tier (< 5 s)
cd build && ctest -L sanity --output-on-failure

# Run the full suite
cd build && ctest --output-on-failure
```

## Project Layout

```text
.
├── CMakeLists.txt
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
    ├── main.cpp                  # Entry point: init DB, start collector, show window
    ├── shared/                   # Database schema and utilities (static library)
    │   ├── database.cpp/.h
    ├── collector/                # Background metadata discovery
    │   ├── source_adapter.cpp/.h        # ISourceAdapter interface + DiscoveredBook
    │   ├── gutenberg_adapter.cpp/.h     # Project Gutenberg RDF catalog adapter
    │   ├── book_discovery_service.cpp/.h # Orchestrates adapters, writes to DB
    │   └── collector_worker.cpp/.h      # QThread wrapper with polling timer
    └── gui/                      # Qt6 Widgets UI
        ├── main_window.cpp/.h           # QMainWindow; hosts the screen stack
        ├── style_tokens.h               # Design token constants
        ├── screens/
        │   ├── library_screen.cpp/.h
        │   ├── search_screen.cpp/.h
        │   └── explore_screen.cpp/.h
        ├── widgets/
        │   ├── book_card_delegate.cpp/.h
        │   ├── empty_state_widget.cpp/.h
        │   ├── badge_label.cpp/.h
        │   └── search_result_delegate.cpp/.h
        └── services/
            ├── library_service.cpp/.h
            ├── search_service.cpp/.h
            └── explore_service.cpp/.h
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
