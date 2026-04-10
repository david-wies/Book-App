**AGENTS.md – Essential guidance for OpenCode agents**

- **Out‑of‑tree CMake builds** – always `mkdir -p build && cd build && cmake .. && make`. Running `cmake ..` from the source root fails because the Qt6 CMake files are only added via a fallback path in `CMakeLists.txt`.

- **Qt6 discovery on Ubuntu/Debian** – Qt6 CMake modules live in `/usr/lib/x86_64-linux-gnu/cmake`; the top‑level CMake script appends this path automatically.

- **Component layout**
  - `src/shared/` → static `shared` library (SQLite `Database`).
  - `src/collector/` → console executable, links to `shared`.
  - `src/gui/` → Qt6 Widgets GUI executable, links to `shared` and `Qt6::Widgets`.

- **Run order** – collector must be executed first to create/refresh `books.db`; the GUI reads this DB. Skipping the collector leaves the GUI with no data.

- **Compiler requirement** – C++23 (`set(CMAKE_CXX_STANDARD 23)`) → need GCC 13+ / Clang 16+.

- **CI workflow** – GitHub Actions runs:
  1. `apt-get install -y build-essential cmake qtbase5-dev libgtk-3-dev`
  2. `mkdir build && cd build && cmake ..`
  3. `cd build && make -j$(nproc)`
  4. `cd build && ctest --output-on-failure || echo "No tests found"`

- **No test suite** – verification is manual: run collector, then GUI.

- **Docs folder** – `docs/` is for internal development only and will be removed; it does not affect build or runtime.

- **Agents catalog** – custom agents are listed under `.github/agents/` (e.g., `expert-cpp-software-engineer.agent.md`). Use `/agent-name` in Copilot Chat to invoke.
