# Classic Books + Audiobook Hub

A modern C++/Qt6 application designed for discovering public-domain literature, managing a personal library, and converting classic texts into audiobooks.

## 🌟 Features

- **Discovery**: Search and explore books across multiple public-domain sources (e.g., Project Gutenberg).
- **Library Management**: Save your favorite classics to a local library for quick access.
- **Multi-Format Support**: Download books in various formats including EPUB, PDF, and HTML.
- **Audiobook Conversion**: (In Progress) Convert text editions into audiobooks using high-quality TTS voices.
- **Background Synchronization**: A dedicated collector thread periodically refreshes metadata without interrupting the UI.

## 🏗️ Architecture

The application is built as a single native C++ program utilizing a multi-threaded architecture:

- **GUI Thread**: Runs the Qt6 Widgets interface, providing a responsive experience for browsing and library management.
- **Data Collector Thread**: Runs in the background to fetch, normalize, and deduplicate book metadata from external sources.
- **Shared Persistence**: Both threads interact with a local SQLite database (`classic_books.db`) to store metadata and user state.

## 🛠️ Prerequisites

To build the project, you need a recent C++23-compatible compiler and the following dependencies:

```bash
# On Ubuntu/Debian
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6
```

## 🚀 Build & Run

### Build
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The build process produces the main executable:
- `classic-books`: The primary application.
- `libshared.a`: A static library containing the database and shared logic.

### Run
```bash
./build/classic-books
```
*Note: The application will automatically initialize the `classic_books.db` database in the same directory as the executable on its first run.*

## 📁 Project Layout

```text
.
├── CMakeLists.txt        # Top-level CMake configuration
├── AGENTS.md           # AI assistant agent definitions
├── .github/            # GitHub configuration (workflows, agents, skills)
├── src/
│   ├── main.cpp        # Application entry point
│   ├── shared/        # Database & common utilities (static library)
│   ├── gui/           # Qt6 interface components (to be expanded)
│   └── collector/     # Background data discovery & normalization
└── docs/              # Design documents and task tracking
```

**Note:** The SQLite database (`classic_books.db`) is generated at runtime in the executable's directory and is not tracked in version control.

## 📄 License

MIT © 2026 ClassicBooks Contributors
