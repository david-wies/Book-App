# BookHub

A native C++/Qt6 desktop application for discovering public-domain literature from multiple sources, managing a personal library, downloading ebooks, and generating audiobooks.

## 🌟 Features

- **Discovery**: Search and explore books across multiple public-domain sources (e.g., Project Gutenberg).
- **Library Management**: Save books to a local library for quick access.
- **Multi-Format Support**: Download books in various formats including EPUB, PDF, HTML, and more.
- **Audiobook Conversion**: (In Progress) Convert text editions into audiobooks using high-quality TTS voices.
- **Background Synchronization**: A dedicated collector thread periodically refreshes metadata without interrupting the UI.

## 🏗️ Architecture

The application is built as a single native C++ program utilizing a multi-threaded architecture:

- **GUI Thread**: Runs the Qt6 Widgets interface, providing a responsive experience for browsing and library management.
- **Data Collector Thread**: Runs in the background to fetch, normalize, and deduplicate book metadata from external sources.
- **Shared Persistence**: Both threads interact with a local SQLite database (`bookhub.db`) to store metadata and user state.

## 🛠️ Prerequisites

Requires GCC 13+/Clang 16+ with C++23 support, and the `tar` binary at runtime.

```bash
# On Ubuntu/Debian
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6
```

## 🚀 Build & Run

### Build

```bash
# Configure (must be out-of-tree — never run cmake from the source root)
mkdir -p build && cd build
cmake ..

# Build
make -j$(nproc)
```

The build process produces the main executable:

- `bookhub`: The primary application.
- `libshared.a`: A static library containing the database and shared logic.

### Run

```bash
./build/bookhub
```

> **Note:** The application will automatically initialize the `bookhub.db` database in the same directory as the executable on its first run.

## 📁 Project Layout

```text
.
├── CMakeLists.txt            # Top-level CMake configuration
├── book_reader_icon.jpg
├── src/
│   ├── main.cpp              # Application entry point: init DB, start collector, show window
│   ├── shared/               # Database schema and utilities (built as static library)
│   ├── collector/            # Background metadata discovery
│   │   ├── source_adapter    # ISourceAdapter interface + DiscoveredBook struct
│   │   ├── gutenberg_adapter # Fetches and parses Project Gutenberg RDF catalog
│   │   ├── book_discovery_service  # Orchestrates adapters, writes to DB
│   │   └── collector_worker  # QThread wrapper with polling timer
│   └── gui/                  # Qt6 Widgets UI
│       ├── main_window       # Top-level QMainWindow; hosts the screen stack
│       ├── style_tokens.h    # Design token constants (colours, spacing, typography)
│       ├── screens/
│       │   └── library_screen  # Personal library list view
│       ├── widgets/
│       │   ├── book_card_delegate  # Custom QStyledItemDelegate for book list rows
│       │   ├── empty_state_widget  # Placeholder shown when the library is empty
│       │   └── badge_label         # Inline pill badge (e.g. language, format tags)
│       └── services/
│           └── library_service  # Reads library_items from SQLite for the GUI thread
└── docs/                     # Internal design documents
```

> **Note:** The SQLite database (`bookhub.db`) is generated at runtime in the executable's directory and is not tracked in version control.

## 📄 License

MIT © 2026 ClassicBooks Contributors
