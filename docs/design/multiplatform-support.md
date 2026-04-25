# Multiplatform Support

This document describes changes required to support multiple platforms (beyond Ubuntu/Linux).

## Current Limitations

The application currently has several hardcoded paths that assume:

- Unix-like filesystem with executable co-located with mutable data
- x86_64 architecture on Linux
- SQLite database stored next to the executable

## Required Changes

### 1. Database Location

**File:** `src/shared/database.cpp:15`

```cpp
// Current (fails on Windows/macOS due to sandboxing):
return QDir(QCoreApplication::applicationDirPath()).filePath(kDatabaseFileName);

// Multiplatform (cross-platform):
return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + kDatabaseFileName;
```

**Rationale:** Modern OS sandboxing (especially Windows UAP/code signing, macOS App Sandbox) prevents writing to the executable's directory. `QStandardPaths::AppDataLocation` provides the proper platform-specific location.

### 2. Icon Path

**File:** `src/main.cpp:31`

```cpp
// Current:
QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("book_reader_icon.jpg"));

// Multiplatform:
QDir(QCoreApplication::applicationDirPath() + "/..").filePath(QStringLiteral("book_reader_icon.jpg"));
// Or better: embed as Qt resources (see below)
```

**Alternative:** Consider embedding the icon as a Qt resource (`.qrc` file) instead of a filesystem copy. This eliminates platform-specific path issues entirely.

### 3. CMake Qt6 Path Fallback

**File:** `CMakeLists.txt:11-12`

```cmake
# Current (x86_64 only):
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")

# Multiplatform:
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    list(APPEND CMAKE_PREFIX_PATH "/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/cmake")
endif()
```

**Note:** The best approach is to remove this fallback entirely and rely on CMake's default search paths. Only add if Qt6 cannot be found.

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