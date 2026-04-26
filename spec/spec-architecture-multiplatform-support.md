---
title: Multiplatform Support (Task 19)
version: 1.0
date_created: 2026-04-25
status: complete
tags: [cmake, qt6, platform, database, icon, resources]
---

**Status: Complete** — All deliverables implemented in PR #28 (Task 19).

# Introduction

BookHub currently hard-codes Linux x86_64 assumptions for the database location, icon
path, and CMake dependency search path. This spec defines the exact changes required to
make the application build and run correctly on Windows and macOS without platform-specific
source branches.

## 1. Purpose & Scope

Covers three code changes and one documentation update pass that together eliminate all
Linux-specific assumptions from the build system and runtime:

1. Database file path resolution (`src/shared/database.cpp`)
2. Application icon delivery (`src/main.cpp`, `CMakeLists.txt`, new `.qrc` file)
3. CMake Qt6 search path (`CMakeLists.txt`)
4. Documentation corrections (`CLAUDE.md`, `docs/design/test-strategy.md`)

Out of scope: installer/packaging, code-signing entitlements, CI runners on non-Linux
platforms (those belong to a future packaging task).

## 2. Definitions

| Term | Meaning |
|------|---------|
| `AppDataLocation` | `QStandardPaths::AppDataLocation` — the platform-appropriate writable directory for application data (e.g. `~/.local/share/BookHub` on Linux, `%APPDATA%\BookHub` on Windows, `~/Library/Application Support/BookHub` on macOS) |
| `.qrc` | Qt resource collection file; sources are compiled into the executable by `CMAKE_AUTORCC` |
| `POST_BUILD` copy | CMake custom command that copies a file next to the executable after each build |
| `AUTORCC` | CMake variable (`CMAKE_AUTORCC ON`) that auto-processes `.qrc` files |

## 3. Requirements, Constraints & Guidelines

- **REQ-001**: `bookhub::db::databaseFilePath()` must return a path inside `QStandardPaths::AppDataLocation` on all platforms, not next to the executable.
- **REQ-002**: The directory returned by `QStandardPaths::AppDataLocation` must be created if it does not exist before the database is opened.
- **REQ-003**: The application icon must be embedded in the executable as a Qt resource and loaded via the `:/` resource path — no filesystem path, no `POST_BUILD` copy.
- **REQ-004**: The hardcoded `/usr/lib/x86_64-linux-gnu/cmake` CMake prefix path must be removed from `CMakeLists.txt`.
- **REQ-005**: `QApplication::organizationName()` and `QApplication::applicationName()` must be set in `main()` before any `QStandardPaths` call so the platform path is computed correctly.
- **CON-001**: `CMAKE_AUTORCC` is already `ON` in `CMakeLists.txt` — no CMake flag changes are needed to enable resource compilation.
- **CON-002**: `book_reader_icon.jpg` is a JPEG. Qt resources support JPEG natively; no conversion is needed.
- **CON-003**: Do not use `QProcess` anywhere (project-wide rule). Icon embedding must not shell out.
- **GUD-001**: Prefer removing the CMake prefix path outright over conditionalising it. The path is only needed on misconfigured systems; standard Qt6 installs (apt, Homebrew, vcpkg, Qt installer) place `Qt6Config.cmake` in CMake's default search paths.
- **GUD-002**: `QStandardPaths::AppDataLocation` requires `QCoreApplication::organizationName` and `QCoreApplication::applicationName` to be set; otherwise the path degenerates to the executable directory on some platforms.

## 4. Interfaces & Data Contracts

### 4.1 `bookhub::db::databaseFilePath()` — after change

**File:** `src/shared/database.cpp`

```cpp
#include <QStandardPaths>
#include <QDir>

QString databaseFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir{}.mkpath(dir);   // create if absent — no-op if already exists
    return QDir(dir).filePath(kDatabaseFileName);
}
```

The `#include <QCoreApplication>` and `#include <QDir>` headers are already present; add
`#include <QStandardPaths>`.

### 4.2 Application identity — `main()` before any `QStandardPaths` call

**File:** `src/main.cpp`

```cpp
QApplication app(argc, argv);
app.setOrganizationName(QStringLiteral("BookHub"));
app.setApplicationName(QStringLiteral("BookHub"));
// ... rest of main
```

Both calls must appear immediately after `QApplication app(argc, argv);` and before
`bookhub::db::initializeDatabase(...)`.

### 4.3 Qt resource file

**New file:** `resources.qrc` (project root, alongside `CMakeLists.txt`)

```xml
<!DOCTYPE RCC>
<RCC version="1.0">
    <qresource prefix="/">
        <file>book_reader_icon.jpg</file>
    </qresource>
</RCC>
```

Resource path exposed to C++: `":/book_reader_icon.jpg"`

### 4.4 Icon loading — `main()` after change

**File:** `src/main.cpp`

```cpp
// Remove:
const QString iconPath =
    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("book_reader_icon.jpg"));
const QIcon appIcon(iconPath);

// Replace with:
const QIcon appIcon(QStringLiteral(":/book_reader_icon.jpg"));
```

The `#include <QDir>` and `#include <QCoreApplication>` imports in `main.cpp` may be
removed if they are no longer used elsewhere in the file after this change.

### 4.5 CMakeLists.txt — resource file registration and prefix path removal

**Remove lines 11–12:**
```cmake
# Ubuntu/Debian Qt6 install path fallback
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")
```

**Add `resources.qrc` to the `bookhub` executable sources:**
```cmake
add_executable(bookhub
    src/main.cpp
    resources.qrc          # <-- add this line
    ${COLLECTOR_SOURCES}
    ${GUI_SOURCES}
)
```

**Remove the `POST_BUILD` icon copy command (lines 57–62):**
```cmake
# Delete this entire block:
add_custom_command(TARGET bookhub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/book_reader_icon.jpg"
        "$<TARGET_FILE_DIR:bookhub>/book_reader_icon.jpg"
    COMMENT "Copying book_reader_icon.jpg next to executable"
)
```

## 5. Acceptance Criteria

- **AC-001**: Given a clean build on Linux (non-x86_64, e.g. aarch64), When `cmake .. && make`, Then the build succeeds without the x86_64 path fallback.
- **AC-002**: Given a fresh run of the application, When it starts for the first time on any platform, Then `bookhub.db` is created inside `QStandardPaths::AppDataLocation`, not next to the executable.
- **AC-003**: Given no `book_reader_icon.jpg` file next to the executable, When the application starts, Then the window and taskbar still show the icon (loaded from the embedded resource).
- **AC-004**: Given a build directory without `book_reader_icon.jpg` copied into it, When the build completes, Then no `POST_BUILD` copy step appears in the build log.
- **AC-005**: Given `app.setOrganizationName` and `app.setApplicationName` are set before `databaseFilePath()` is called, When the path is resolved on macOS, Then it resolves to `~/Library/Application Support/BookHub/bookhub.db`.

## 6. Rationale & Context

- **Database path:** Writing next to the executable fails under Windows UAP sandboxing, macOS App Sandbox, and Flatpak/Snap on Linux. `QStandardPaths::AppDataLocation` is the Qt-idiomatic answer and is tested across all three desktop platforms.
- **Icon as resource:** A `POST_BUILD` copy is fragile — it breaks if the build directory is wiped, the file is renamed, or the app is run from a different directory. Embedding eliminates the dependency entirely.
- **CMake prefix path:** The `/usr/lib/x86_64-linux-gnu/cmake` path was added as a workaround for misconfigured Ubuntu installs. Standard Qt6 apt packages place `Qt6Config.cmake` in CMake's default search paths. Keeping it silently breaks cross-compilation and non-x86 Linux builds.

## 7. Dependencies

| Dependency | Notes |
|-----------|-------|
| Qt6 Core (`QStandardPaths`) | Already linked via `Qt6::Widgets` → `Qt6::Core` |
| `CMAKE_AUTORCC ON` | Already set in `CMakeLists.txt` line 9 |
| `book_reader_icon.jpg` | Must remain at the project root for `.qrc` to reference it |

## 8. Documentation Updates After Implementation

Apply these after the code changes are merged:

| File | Location | Old text | New text |
|------|----------|----------|----------|
| `CLAUDE.md` | Two-Thread Model section | "Both threads share a single SQLite database (`bookhub.db`, placed next to the executable)" | "Both threads share a single SQLite database (`bookhub.db`, stored in `QStandardPaths::AppDataLocation`)" |
| `CLAUDE.md` | Key Conventions → Build artifacts | "The icon and database are co-located with the executable via a CMake `POST_BUILD` copy." | "The icon is embedded as a Qt resource (`.qrc`). The database is stored in `QStandardPaths::AppDataLocation`." |
| `docs/design/test-strategy.md` | Database helpers section (line 156) | "`databaseFilePath()` resolves next to the executable." | "`databaseFilePath()` resolves to `QStandardPaths::AppDataLocation`." |

## 9. Related Specifications

- `docs/design/multiplatform-support.md` — design doc this spec is derived from
- `docs/tasks/task-breakdown.md` — Task 19 description
- `docs/tasks/status.md` — Task 19 status
