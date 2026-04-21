# Task Status Tracking

## Task 1: Set up C++ Qt project structure
- **Status**: Completed
- **Notes**: Project skeleton created with CMake, Qt6, and SQLite integration support; both GUI and collector binaries build and run successfully.

## Task 2: Design and implement SQLite database schema
- **Status**: Completed
- **Notes**: Implemented normalized SQLite schema in C++ with QSqlQuery for books, genres, formats, sources, and library items. Added schema initialization and sample data insertion on startup.

## Task 3: Refactor architecture to single executable
- **Status**: Completed
- **Notes**: Merged GUI and Data Collector binaries into a single `classic-books` executable. Configured CMake to build the single target and enabled Qt's `CMAKE_AUTOMOC`. The GUI is launched from `src/main.cpp`, which also spawns a `CollectorWorker` background thread (QThread) that runs alongside the GUI.

## Task 4: Implement data collector background thread
- **Status**: Completed
- **Notes**: Implemented the `BookDiscoveryService` and the `ISourceAdapter` plugin interface. Added the `GutenbergAdapter` that fetches book JSON from `gutendex.com` via `QNetworkAccessManager`. Configured thread-safe SQLite access using a named connection ("collector_connection") to insert books, formats, and sources seamlessly in the background.

## Task 5: Create Library screen GUI
- **Status**: Not Started
- **Notes**: 

## Task 6: Implement Search screen
- **Status**: Not Started
- **Notes**: 

## Task 7: Create Explore screen
- **Status**: Not Started
- **Notes**: 

## Task 8: Build Book details screen
- **Status**: Not Started
- **Notes**: 

## Task 9: Implement add/remove from library functionality
- **Status**: Not Started
- **Notes**: 

## Task 10: Develop download flow
- **Status**: Not Started
- **Notes**: 

## Task 11: Build audiobook conversion flow
- **Status**: Not Started
- **Notes**: 

## Task 12: Integrate TTS for voice preview and generation
- **Status**: Not Started
- **Notes**: 

## Task 13: Add custom voice upload feature
- **Status**: Not Started
- **Notes**: 

## Task 14: Implement source adapters for extensibility
- **Status**: Not Started
- **Notes**: 

## Task 15: Add UI polish and error handling
- **Status**: Not Started
- **Notes**: 

## Task 16: Package and deploy application
- **Status**: Not Started
- **Notes**:
