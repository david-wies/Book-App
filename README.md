# Digital Library Hub

A small C++/Qt6 application that collects classic books information and serves it via a GUI.

## Overview
- **shared** – static library (`src/shared`) providing a SQLite‑backed `Database` implementation.
- **classic‑books‑gui** – Qt6 Widgets GUI (`src/gui/main.cpp`).
- **classic‑books‑collector** – Console tool that populates the database (`src/collector/main.cpp`).

The project is built with **CMake** (minimum version 3.24) and requires **Qt6** (Widgets, Sql) and a recent C++23‑compatible compiler.

## Prerequisites
```bash
sudo apt-get install build-essential cmake qt6-base-dev libqt6sql6
```
*(On Ubuntu/Debian, the Qt6 CMake files are in `/usr/lib/x86_64-linux-gnu/cmake`; the CMakeLists already adds this path.)*

## Build
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```
The build creates three binaries in `build/`:
- `classic-books-gui`
- `classic-books-collector`
- `libshared.a` (static library)

## Run
```bash
# Populate the database first
./classic-books-collector

# Then launch the GUI
./classic-books-gui
```
The collector writes a SQLite database (`books.db` by default) which the GUI reads.

## Project layout
```
CMakeLists.txt          # Top‑level CMake configuration
src/
  shared/               # Database implementation (static lib)
  gui/      main.cpp    # Qt6 Widgets GUI entry point
  collector/ main.cpp   # CLI data‑collector entry point
build/                  # CMake output (generated)
```

## Documentation
*(Development only – this folder will be removed later)*

## License
MIT © 2026 ClassicBooks contributors
