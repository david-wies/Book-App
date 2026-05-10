# Static Analysis & Bug Detection Tools

Recommended free, non-LLM tools for the BookHub C++23/Qt6 project.

## Prerequisites

```bash
sudo apt install cppcheck clang-tidy clang-format valgrind clazy iwyu
```

---

## Compilation Database

Most analysis tools need `compile_commands.json` to find the correct compiler flags and Qt
includes. Always generate it when configuring:

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

---

## Linting & Code Formatting

### 1. clang-format

Official LLVM code formatter. Enforces consistent style (braces, indentation, line length).

**Create `.clang-format` in project root:**

```yaml
BasedOnStyle: LLVM
Language: Cpp
Standard: c++23
ColumnLimit: 100
IndentWidth: 4
UseTab: Never
PointerAlignment: Left
```

**Check files:**
```bash
clang-format --style=file --dry-run -Werror $(find src/ tests/ -name '*.cpp' -o -name '*.h')
```

**Fix files (in-place):**
```bash
clang-format -i --style=file $(find src/ tests/ -name '*.cpp' -o -name '*.h')
```

**Integrate with CMake:**
```cmake
find_program(CLANG_FORMAT_EXE NAMES clang-format-18 clang-format-17 clang-format)
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE ALL_SOURCES src/*.cpp src/*.h tests/*.cpp tests/*.h)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${ALL_SOURCES}
    COMMENT "Running clang-format"
  )
endif()
```

**Git hook (pre-commit) — place in `.githooks/pre-commit`:**
```bash
#!/bin/bash
CHANGED=$(git diff --name-only --cached | grep -E '\.(cpp|h)$')
[ -z "$CHANGED" ] && exit 0
clang-format --style=file --dry-run -Werror $CHANGED || exit 1
```

---

## Static Analysis

### 1. clang-tidy

Best static analyzer for this project. C++23 aware, integrates with CMake via the compilation
database. This is the primary linter — run it before every PR.

**Run (requires compilation database):**
```bash
run-clang-tidy -p build src/ tests/
```

**Configuration — create `.clang-tidy` in project root:**
```yaml
Checks: '-*, modernize-*, performance-*, bugprone-*, misc-*, clang-analyzer-*, readability-*'
WarningsAsErrors: ''
CheckOptions:
  - key: modernize-use-nullptr.NullMacros
    value: 'Q_NULLPTR'
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
```

**Integrate into the build (catches issues during `cmake --build`):**
```cmake
cmake -B build -DCMAKE_CXX_CLANG_TIDY=clang-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cmake --build build
```

### 2. clazy

Qt-specific clang plugin. Catches Qt anti-patterns that clang-tidy misses: wrong connect()
argument counts, missing `Q_DECLARE_METATYPE`, inefficient QString operations, signal emission
outside the GUI thread, and more. Highly recommended for any Qt project.

**Install:**
```bash
sudo apt install clazy
```

**Run:**
```bash
clazy-standalone -checks=level1 -p build $(find src/ -name '*.cpp')
```

Levels: `level0` (safe, always on), `level1` (good default), `level2` (more aggressive, some
false positives). Start with `level1`.

**Integrate with CMake:**
```cmake
cmake -B build -DCMAKE_CXX_CLANG_TIDY="clazy-standalone;-checks=level1" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

### 3. cppcheck

Good complement to clang-tidy. Finds different classes of bugs: uninitialized variables,
out-of-bounds access, resource leaks. Does not require a compilation database.

**Run:**
```bash
cppcheck --enable=all --std=c++23 --template=gcc \
  --suppress=missingIncludeSystem --suppress=missingInclude \
  -I src/ src/
```

The `--suppress=missingInclude*` flags are required to silence false positives from Qt headers
that cppcheck cannot resolve — without them, real issues are buried in noise.

**With XML output (CI-friendly):**
```bash
cppcheck --enable=all --std=c++23 --xml \
  --suppress=missingIncludeSystem --suppress=missingInclude \
  -I src/ src/ 2> cppcheck.xml
```

### 4. scan-build (Clang Static Analyzer)

Clang's inter-procedural static analyzer. Catches deeper semantic bugs than clang-tidy:
uninitialized value use, dead stores, null dereferences through multiple call frames. Ships
with LLVM — no extra install needed.

**Run:**
```bash
scan-build cmake --build build
```

Or for a clean analysis pass:
```bash
scan-build -o scan-results cmake -B build-scan .. && scan-build -o scan-results cmake --build build-scan
```

Open `scan-results/` in a browser to view the bug reports with full call-path traces.

### 5. include-what-you-use (IWYU)

Audits `#include` directives — flags headers you include but don't use, and headers you use but
don't include directly. Keeps compile times down as the project grows.

**Install:**
```bash
sudo apt install iwyu
```

**Run:**
```bash
iwyu_tool.py -p build src/ | fix_includes.py --dry_run
```

Remove `--dry_run` to apply fixes automatically. IWYU can be aggressive; review its suggestions
before applying.

**Integrate with CMake:**
```cmake
cmake -B build -DCMAKE_CXX_INCLUDE_WHAT_YOU_USE=iwyu \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

### 6. gcc -fanalyzer

Built into GCC 13+. Catches malloc/free mismatches, use-after-free, double-free via
inter-procedural data-flow analysis.

**Run:**
```bash
cmake -B build -DCMAKE_CXX_FLAGS="-fanalyzer" ..
cmake --build build
```

**Warning:** `-fanalyzer` can increase compile times by 5–10×. Reserve it for CI or a dedicated
analysis build, not the regular developer build.

### 7. Infer (Meta)

Inter-procedural static analyzer. Particularly good at resource leaks, null dereferences, and
use-after-free across function boundaries. Free and open source (MIT).

**Install:** Download a pre-built binary from https://github.com/facebook/infer/releases

**Run:**
```bash
infer run -- cmake --build build
```

Reports are written to `infer-out/`. Infer performs incremental analysis — re-running after
fixing a bug only re-analyzes affected files.

### 8. CodeChecker

Orchestration layer that runs clang-tidy and cppcheck together, deduplicates findings, and
produces a unified HTML/JSON report or a local web UI. Useful for tracking analysis results
over time.

**Install:**
```bash
pip install codechecker
```

**Run:**
```bash
CodeChecker analyze build/compile_commands.json -o ./cc-results
CodeChecker parse ./cc-results
```

**Serve a local web UI:**
```bash
CodeChecker server &
CodeChecker store ./cc-results --name bookhub
```

---

## Dynamic Analysis (Runtime Bugs)

### 1. AddressSanitizer (ASan)

Detects: memory leaks, heap/stack buffer overflows, use-after-free, use-after-return.

**Add to CMakeLists.txt:**
```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
```

**Build & Run:**
```bash
cmake -B build -DENABLE_ASAN=ON ..
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

### 2. UndefinedBehaviorSanitizer (UBSan)

Detects: integer overflow, null pointer dereference, invalid shifts, misaligned access.
Compatible with ASan — run them together.

**Add to CMakeLists.txt:**
```cmake
option(ENABLE_UBSAN "Enable UBSan" OFF)
if(ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()
```

**Build & Run:**
```bash
cmake -B build -DENABLE_UBSAN=ON ..
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

### 3. MemorySanitizer (MSan)

Detects reads of uninitialized memory — bugs that ASan cannot catch. Requires a
clang build; GCC is not supported.

**Add to CMakeLists.txt:**
```cmake
option(ENABLE_MSAN "Enable MemorySanitizer" OFF)
if(ENABLE_MSAN)
  add_compile_options(-fsanitize=memory -fno-omit-frame-pointer -fPIE)
  add_link_options(-fsanitize=memory -pie)
endif()
```

**Build & Run (clang only):**
```bash
CC=clang CXX=clang++ cmake -B build -DENABLE_MSAN=ON ..
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

**Note:** MSan is incompatible with ASan and TSan. Run it in an isolated build.

### 4. ThreadSanitizer (TSan)

Critical for this project — detects data races in the 3-thread architecture (GUI, Query,
Collector threads). Incompatible with ASan and MSan; run in its own build.

**Add to CMakeLists.txt:**
```cmake
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
```

**Build & Run:**
```bash
cmake -B build -DENABLE_TSAN=ON ..
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

---

## Combined ASan + UBSan Build

ASan and UBSan are compatible and are commonly run together. **Do not add TSan or MSan to this
combination** — they conflict with ASan at the runtime level.

**Add to CMakeLists.txt:**
```cmake
option(ENABLE_SANITIZERS "Enable ASan + UBSan together" OFF)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
```

**Build & Run:**
```bash
cmake -B build -DENABLE_SANITIZERS=ON ..
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

For thread safety, run a separate TSan build (`-DENABLE_TSAN=ON`).

---

## Valgrind (Alternative to ASan)

Traditional memory debugger. Lower performance than ASan but works without recompiling.

**Run:**
```bash
QT_QPA_PLATFORM=offscreen valgrind --leak-check=full --show-leak-kinds=all \
  --track-origins=yes ./build/tests/test_library_service
```

**Common options:**
- `--leak-check=summary` — quick summary
- `--show-leak-kinds=definite` — only definite leaks
- `--vgdb=yes` — interactive debugging

**Qt false positives:** Valgrind reports many apparent leaks from Qt's internal memory pools,
font caches, and static initializers. These are expected and benign. Use a Qt suppression file
to filter them:
```bash
valgrind --suppressions=/usr/share/doc/qt6-base-dev/valgrind.supp \
  --leak-check=full --show-leak-kinds=definite \
  QT_QPA_PLATFORM=offscreen ./build/tests/test_library_service
```

---

## heaptrack (Memory Profiler)

Tracks every heap allocation with call stacks. Much lower overhead than Valgrind (~2–3× vs
~20–50×). Useful for finding allocation hotspots and unexpected memory growth — not just leaks.

**Install:**
```bash
sudo apt install heaptrack heaptrack-gui
```

**Run:**
```bash
QT_QPA_PLATFORM=offscreen heaptrack ./build/bookhub
heaptrack_gui heaptrack.bookhub.*.gz
```

---

## Cyclomatic Complexity (Lizard)

Reports functions with high cyclomatic complexity — a proxy for hard-to-test, error-prone code.
Pure Python, no compilation database needed.

**Install:**
```bash
pip install lizard
```

**Run:**
```bash
lizard src/ --CCN 10 --length 60 --warnings_only
```

- `--CCN 10` — warn on complexity > 10
- `--length 60` — warn on functions longer than 60 lines

---

## CI Integration

```bash
#!/bin/bash
set -e

# 1. Configure with compilation database
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# 2. Format check
clang-format --style=file --dry-run -Werror \
  $(find src/ tests/ -name '*.cpp' -o -name '*.h')

# 3. Static analysis (clang-tidy — errors fail the build)
run-clang-tidy -p build src/ tests/

# 4. Qt-specific analysis
clazy-standalone -checks=level1 -p build $(find src/ -name '*.cpp')

# 5. cppcheck
cppcheck --enable=all --std=c++23 --error-exitcode=1 \
  --suppress=missingIncludeSystem --suppress=missingInclude \
  -I src/ src/

# 6. Build with ASan + UBSan
cmake -B build-san -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build build-san

# 7. Run tests under sanitizers
cd build-san && ctest --output-on-failure
```

---

## Tool Summary

| Tool | Type | Detects | Recommended For |
|------|------|---------|-----------------|
| **clang-format** | Formatter | Code style | Pre-commit, CI |
| **clang-tidy** | Static+Lint | Style, bugs, modernize | Always — primary linter |
| **clazy** | Static (Qt) | Qt anti-patterns, signal issues | Always — Qt complement |
| **cppcheck** | Static | Memory, null safety | Complement to clang-tidy |
| **scan-build** | Static | Deep semantic bugs | Periodic / CI |
| **IWYU** | Static | Unnecessary/missing includes | Periodic cleanup |
| **gcc -fanalyzer** | Static | Use-after-free, leaks | CI (slow — not dev builds) |
| **Infer** | Static | Resource leaks, null dereferences | CI |
| **CodeChecker** | Orchestration | Unified report (tidy + cppcheck) | CI dashboards |
| **ASan** | Runtime | Memory errors | Development, CI |
| **UBSan** | Runtime | Undefined behavior | Development, CI |
| **MSan** | Runtime | Uninitialized reads | Targeted investigation |
| **TSan** | Runtime | Race conditions | Critical for 3-thread app |
| **Valgrind** | Runtime | Memory leaks | When sanitizers unavailable |
| **heaptrack** | Profiler | Allocation hotspots, memory growth | Performance investigation |
| **Lizard** | Complexity | High-complexity functions | Periodic / refactor planning |
