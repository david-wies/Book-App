# Multiplatform Support

This document describes the multiplatform changes implemented in Task 19 (PR #28).

## Implemented Changes

### 1. Database Location

**File:** `src/shared/database.cpp`

`databaseFilePath()` uses `QStandardPaths::AppDataLocation` — the platform-appropriate writable location:

- **Linux:** `~/.local/share/BookHub/BookHub/bookhub.db`
- **macOS:** `~/Library/Application Support/BookHub/BookHub/bookhub.db`
- **Windows:** `%APPDATA%\BookHub\BookHub\bookhub.db`

**Rationale:** Modern OS sandboxing prevents writing to the executable's directory. `QStandardPaths::AppDataLocation` provides the correct per-user, per-application data path on all platforms.

### 2. Application Icon

**File:** `resources.qrc`, `src/main.cpp`

The application icon (`book_reader_icon.jpg`) is embedded as a Qt resource via `resources.qrc`. The icon is loaded with `QIcon(":/book_reader_icon.jpg")` — no filesystem path required, no `POST_BUILD` copy step.

### 3. CMake Qt6 Path Fallback

**File:** `CMakeLists.txt`

The hardcoded x86_64 `CMAKE_PREFIX_PATH` fallback has been removed. CMake finds Qt6 via its standard search paths.

## Platform-Specific Dependencies

| Platform | Qt6 Package | libarchive | Build Tool |
|----------|------------|------------|------------|
| Ubuntu/Debian | `qt6-base-dev libqt6sql6` | `libarchive-dev` | CMake |
| Windows (MSVC) | Qt6 MSVC installer | vcpkg or prebuilt | CMake + MSVC |
| Windows (MinGW) | Qt6 MinGW installer | vcpkg or prebuilt | CMake + MinGW |
| macOS (Intel) | Homebrew `qt6` | `libarchive` (brew) | CMake |
| macOS (Apple Silicon) | Homebrew `qt6` | `libarchive` (brew) | CMake |

## Recommended Approach

1. **Use Qt resources for icons** — Eliminates one path issue entirely
2. **Use QStandardPaths** — All mutable data goes to platform-appropriate locations
3. **Remove hardcoded CMake paths** — Let CMake find Qt6 via standard means
4. **Consider vcpkg** — For consistent Windows dependency management

## Documentation Updates Required After Implementation

These files contain Linux-specific statements that will become stale once Task 19 changes are applied:

| File | Location | Stale Statement | Replacement |
|------|----------|-----------------|-------------|
| `CLAUDE.md` | Two-Thread Model section | "Both threads share a single SQLite database (`bookhub.db`, placed next to the executable)" | Note that the database is stored in `QStandardPaths::AppDataLocation` |
| `CLAUDE.md` | Key Conventions → Build artifacts | "The icon and database are co-located with the executable via a CMake `POST_BUILD` copy" | Icon is embedded as a Qt resource (`.qrc`); database lives in `AppDataLocation` |
| `docs/design/test-strategy.md` | Database helpers section (line 156) | "`databaseFilePath()` resolves next to the executable" | Resolves to `QStandardPaths::AppDataLocation` |

**Note:** References to `build/bookhub.db` in `docs/tasks/book-identity-model-implementation.md` are developer migration tooling context and do not need updating — developers still run migrations from the build directory.

## Future Considerations

- **macOS code signing:** May require entitlements for network access
- **Windows Store deployment:** Requires specific sandboxing compliance
- **Flatpak/Snap:** Alternative Linux distribution with sandboxing
- **AppImage:** Portable single-file distribution (no sandboxing issues)