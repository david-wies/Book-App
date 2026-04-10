# Project Instructions

This file consolidates the most important operational details for developers working on **Digital Library Hub**.

## Build & Run
```bash
# Build (out‑of‑tree)
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```
```bash
# Populate the database first
./classic-books-collector

# Then start the GUI
./classic-books-gui
```

## Key Architecture
- **src/shared** – static library `shared` exposing a SQLite `Database`.
- **src/collector** – console tool that creates/updates `books.db`.
- **src/gui** – Qt6 Widgets GUI that reads `books.db`.

## Compiler
C++23 is required (`set(CMAKE_CXX_STANDARD 23)`). Use GCC 13+ or Clang 16+.

## CI (GitHub Actions)
1. Install dependencies (`build-essential cmake qtbase5-dev libgtk-3-dev`).
2. Configure: `mkdir build && cd build && cmake ..`.
3. Build: `make -j$(nproc)`.
4. Test step runs `ctest` – it will simply report "No tests found" because the repo has no test suite.

## Gotchas
- **Never run `cmake ..` from the source root** – the Qt6 modules are only found when building out‑of‑tree.
- The `docs/` directory is for internal use only and will be removed before release.
- No automated tests; manual verification is required.

## Git Workflow
- **`master`** – public-facing branch with stable release (README only).
- **`develop`** – integration branch for ongoing development.
- **Feature branches** – create new branches off `develop` for each feature/fix:
  ```bash
  git checkout develop
  git checkout -b feature/<short-description>
  ```
  Work on the feature branch, then merge back to `develop` when ready.
- **Never push directly to `master`** – only merge from `develop` after testing.

## Agents
Custom agents live in `.github/agents/`. Invoke them in Copilot Chat with `/agent-name` (e.g., `/expert-cpp-software-engineer`).
