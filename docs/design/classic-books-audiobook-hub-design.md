# BookHub — Design Document

## 1. Project Summary

**Product:** BookHub

**Goal:** Deliver a minimal, user-friendly experience for discovering public-domain books, saving them to a personal library, downloading ebook formats, and converting text editions into audiobooks using preset and custom voices.

**Primary users:** readers of classic literature, multilingual book collectors, audiobook fans who want a lightweight discovery + conversion experience.

## 2. Design Goals

- Enable discovery across multiple public-domain sources without changing the UI as sources are added.
- Keep book details and editions clearly organized by language, format, and source.
- Support both direct download flows and audiobook generation flows.
- Make the voice workflow simple: choose text edition, choose voice, preview, generate.
- Provide an extensible data model so new formats, sources, and voices are supported later.

## 3. Recommended Architecture

### 3.1 Overall approach

Build a single native C++ application (one program) utilizing **two separate threads**:

1. **GUI Thread**: Runs the Qt desktop interface (Library, Search, Explore, BookDetailsPanel, Download/Audiobook flows). Reads from the shared SQLite database and handles user interactions.
2. **Data Collector Thread**: Runs in the background and periodically refreshes book metadata from sources (Gutenberg, Ben-Yehuda, etc.). Updates the SQLite database on a configurable schedule.

This single-program architecture keeps the GUI responsive, separates concerns, and enables user-configurable update frequency.

Recommended layers:

- **GUI layer**: Qt Quick or Qt Widgets for responsive user interface
- **GUI services**: LibraryService, AudiobookService, VoiceService (read-only access to database)
- **Data collector**: SourceAdapterService, BookDiscoveryService (fetches and normalizes metadata)
- **Persistence**: SQLite for metadata and user library, shared between both threads
- **Source adapters**: pluggable C++ components for Gutenberg, Ben-Yehuda, and future providers
- **Audio integration**: native or external TTS service for voice preview and generation

### 3.2 Technology stack recommendation

Recommended C++ stack for MVP:

- UI framework: Qt 6 with QML for responsive UI or Qt Widgets for classic desktop UI
- Build system: CMake
- Language: Modern C++ (C++20+) for strong typing, module support, and performance
- **Persistence: SQLite** (rationale in Section 3.4 below)
- Persistence: SQLite via Qt SQL or a lightweight ORM wrapper
- HTTP/network: Qt Network or libcurl for source integration and remote API access
- Audio: Qt Multimedia for playback and file handling; optional external TTS SDK or local TTS engine for generation
- Threading/Process: Qt threading (QThread) for running the data collector background tasks alongside the main GUI thread; optional platform-specific subprocess management if needed
- Packaging: platform-native installers or self-contained app bundles for Windows, macOS, and Linux
- IPC: Direct SQLite file access with proper locking, or Qt-based messaging if needed
- **Application Icon**: `book_reader_icon.jpg` embedded in the executable as a Qt resource, displayed in window title bar and application menus

### 3.3 Why C++ and Qt

- Qt provides a mature cross-platform GUI layer and threading infrastructure.
- CMake is the standard build tool for portable C++ projects.
- Separating GUI and data collection ensures the UI remains responsive even during long-running metadata fetches.

### 3.4 Why SQLite

Analysis of the example database structure:

```
Books (nested by ISBN)
├── Metadata: title, author, language, genres (array)
└── Formats (multiple per book)
    ├── Format type: pdf, epub, html, txt
    └── Sources (multiple per format)
        └── source name + download link
```

**Database evaluation:**

- **SQLite**: ✅ **RECOMMENDED**
  - File-based, no server required; ideal for desktop apps
  - Excellent Qt integration via Qt SQL module
  - Supports the normalized relational schema naturally (Books → Formats → Sources)
  - Handles array-like data (genres, multiple formats/sources) through normalization
  - Built-in locking for concurrent access from GUI and data collector threads
  - Simple deployment and user data portability
  - Sufficient performance for MVP-scale book metadata (public-domain archives typically have 50k-500k books)
  - Easy to extend later without architectural changes

**Variable-length relationships handling:**
SQLite excels at handling the variable number of genres, formats, and sources per book through proper normalization:
- **Genres**: Many-to-many relationship (book_genres junction table) - some books have 1 genre, others have 5+
- **Formats**: One-to-many (formats table) - some books have only epub, others have pdf, epub, html, txt
- **Sources**: One-to-many per format (sources table) - some formats have 1 source, others have multiple (Gutenberg + Ben-Yehuda)

This is exactly what relational databases are designed for—efficient storage and querying of variable-length relationships without duplication or wasted space.

- **PostgreSQL**: Not recommended for MVP
  - Requires a separate server process (adds deployment complexity)
  - Overkill for a single-user desktop application
  - Only justified if there's a backend API for multiple users later

- **MongoDB/Document DB**: Not recommended
  - Would store the nested structure naturally, but adds heavyweight server dependency
  - Unnecessary for a desktop app with structured queries
  - Harder to deploy and manage for typical users

- **SQLite with JSON extensions**: Possible but unnecessary
  - Could store Format/Source arrays as JSON blobs, but complicates queries
  - Normalization is cleaner and more efficient for this data

**Conclusion:** SQLite with a normalized relational schema is the optimal choice for this MVP—lightweight, portable, performant, and well-integrated with Qt.

## 4. Core Components

### 4.1 GUI Thread components

**UI Screens** (Qt-based, run in GUI thread)

1. `LibraryScreen`
   - Shows saved books with cover, title, author, language badges, source badges, and status.
   - Queries the local SQLite database for LibraryItem records.
   - Actions: view details, download, convert to audiobook, remove.

2. `SearchScreen`
   - Filter panel: title/keyword, author, category, year, language, source, availability.
   - Queries the database with filter conditions applied in the SQLite query.
   - Results list with add-to-library quick actions.

3. `ExploreScreen`
   - Category cards, subcategory drill-down, discovery sections: trending, new arrivals, curated.
   - Reads from cached category and book data in the database.

4. `BookDetailsPanel`
   - Full metadata view and availability by edition, shown as a right-side split pane (50/50 `QSplitter`) within the active screen — not a separate window.
   - Queries the database for Edition and Format records.
   - Controls for add-to-library, download, audiobook conversion.
   - Language selector when multiple editions exist.

5. `DownloadFlow`
   - Step 1: select language edition.
   - Step 2: choose ebook format.
   - Step 3: choose source and confirm download.

6. `AudiobookFlow`
   - Step 1: select language edition.
   - Step 2: select text format.
   - Step 3: choose voice (preset or custom).
   - Step 4: preview sample.
   - Step 5: generate and save audio.

**GUI Services** (run in GUI thread)

1. `LibraryService`
   - Manage saved library items (add, remove, update status).
   - Writes to the SQLite database.
   - Track selected language, format, and status.

2. `SearchService`
   - Apply filters to books and editions.
   - Query the database with title, author, category, year, language, source, and availability filters.
   - Return paginated results to the UI.

3. `VoiceService`
   - Expose preset voices and custom voice records from the database.
   - Manage voice upload metadata and preview generation.

4. `AudiobookService`
   - Orchestrate text retrieval and TTS conversion.
   - Provide preview audio and final generated audio file references.
   - Update audiobook status in the database.

### 4.2 Data Collector Thread components

**Background Services** (run in data collector thread)

1. `SourceAdapterService`
   - Connect to each source provider (Gutenberg, Ben-Yehuda, etc.).
   - Map source-specific metadata to the shared `Book` / `Edition` / `Format` model.
   - Run on a configurable update schedule (e.g., hourly, daily).
   - Update the SQLite database with discovered books and formats.
   - Keep source adapters pluggable for future providers.

2. `BookDiscoveryService`
   - Orchestrate periodic metadata refresh from all active source adapters.
   - Normalize and deduplicate book records across sources.
   - Manage the update schedule and configuration (stored in the database or config file).
   - Log update progress and errors.

3. `DownloadService`
   - Resolve and validate download URLs for formats.
   - Update the database with current availability and download links.
   - May run as a separate update cycle or on-demand from the GUI.

### 4.3 Shared Database Schema

The SQLite database is shared between the GUI and Data Collector threads and contains **book metadata and details with links to external content**, not the actual book or audio files. The database stores references, download URLs, and user library state.

Entities stored in the database:

- `Book`
  - `book_id` (canonical identifier — LCCN, OCLC, ISBN, or source-specific fallback), title, authors, summary, publicationDate
  - Note: does not store actual book content; stores metadata only
  - See `docs/design/book-identity-model.md` for the full ID resolution strategy

- `BookIdentifier`
  - All known identifiers for a work (LCCN, OCLC, ISBN, Gutenberg ID, etc.)
  - Enables cross-source deduplication: before inserting a new book, query this table for any matching identifier

- `Edition`
  - id, bookId, language, publisher, publishDate, formats
  - Language-specific metadata and format availability

- `Source`
  - id, name, supportedLanguages, type, metadata
  - Information about book sources (Gutenberg, Ben-Yehuda, Internet Archive, etc.)

- `Format`
  - id, editionId, sourceId, type (txt, epub, pdf, html, etc.), downloadUrl, availability
  - Links to actual ebook files hosted externally; not stored in database

- `LibraryItem`
  - id, bookId, selectedLanguage, selectedFormat, status, addedAt
  - User's personal library state; downloads are stored on user's local filesystem

- `Voice`
  - id, name, type, sampleText, uploadStatus, voiceModelReference
  - Metadata for preset voices and custom voice uploads; audio files stored separately on filesystem

### 4.4 Normalized SQLite schema

The following normalized SQLite tables implement the entity model above. The key change from the
initial design is that `books.isbn TEXT PRIMARY KEY` is replaced by `books.book_id TEXT PRIMARY KEY`,
which holds whichever standard identifier is most authoritative for a given work. A new
`book_identifiers` table stores every known identifier, enabling cross-source deduplication.

The `book_id` value is a prefixed string in the format `"<type>:<value>"`, e.g.:
- `"lccn:n78095332"` — Library of Congress Control Number (highest priority; work-level, stable)
- `"oclc:42707429"` — OCLC/WorldCat number (second priority; broad coverage)
- `"isbn:9780141439518"` — ISBN-13 (third priority; edition-specific but unambiguous)
- `"gutenberg:1342"` — Gutenberg numeric ID (fallback when no standard identifier is present)
- `"archive:moby-dick"` — Internet Archive item ID (fallback for Archive-sourced records)

For the full ID priority order, resolution algorithm, and adapter responsibilities, see
`docs/design/book-identity-model.md`.

```sql
-- Books table (one row per logical work; language lives in editions)
-- book_id format: "<type>:<value>" — e.g. "lccn:n78095332", "gutenberg:1342"
CREATE TABLE IF NOT EXISTS books (
    book_id          TEXT PRIMARY KEY,
    title            TEXT NOT NULL,
    author           TEXT,
    publish_year     INTEGER,
    publication_date TEXT,
    summary          TEXT
);

-- All known identifiers for a book (one row per identifier type+value)
-- Primary deduplication mechanism: query this table before inserting a new book
CREATE TABLE IF NOT EXISTS book_identifiers (
    book_id  TEXT NOT NULL,
    type     TEXT NOT NULL,   -- 'lccn', 'oclc', 'isbn', 'gutenberg', 'archive', 'benyehuda'
    value    TEXT NOT NULL,
    PRIMARY KEY (type, value),
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE
);

-- Editions (one row per language variant of a work)
CREATE TABLE IF NOT EXISTS editions (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id      TEXT NOT NULL,
    language     TEXT NOT NULL,
    publisher    TEXT,
    publish_date TEXT,
    UNIQUE (book_id, language),
    FOREIGN KEY (book_id) REFERENCES books(book_id) ON DELETE CASCADE
);

-- Genres (many-to-many with books; genres belong to the work, not a language edition)
CREATE TABLE IF NOT EXISTS genres (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    genre_name TEXT UNIQUE NOT NULL
);

CREATE TABLE IF NOT EXISTS book_genres (
    book_id  TEXT NOT NULL,
    genre_id INTEGER NOT NULL,
    PRIMARY KEY (book_id, genre_id),
    FOREIGN KEY (book_id)  REFERENCES books(book_id) ON DELETE CASCADE,
    FOREIGN KEY (genre_id) REFERENCES genres(id)     ON DELETE CASCADE
);

-- Formats (many per edition, not per book)
CREATE TABLE IF NOT EXISTS formats (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    edition_id  INTEGER NOT NULL,
    format_type TEXT NOT NULL,   -- 'epub', 'pdf', 'txt', 'html', 'mobi', etc.
    FOREIGN KEY (edition_id) REFERENCES editions(id) ON DELETE CASCADE
);

-- Sources with download links (one row per format+source combination)
CREATE TABLE IF NOT EXISTS sources (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    format_id     INTEGER NOT NULL,
    source_name   TEXT NOT NULL,    -- 'Gutenberg', 'Ben-Yehuda', 'Archive', etc.
    download_link TEXT NOT NULL,
    FOREIGN KEY (format_id) REFERENCES formats(id) ON DELETE CASCADE
);

-- User's library (tracks which edition the user chose)
CREATE TABLE IF NOT EXISTS library_items (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id     TEXT NOT NULL,
    edition_id  INTEGER,
    added_date  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status      TEXT,   -- 'saved', 'downloading', 'downloaded', 'converting', 'audiobook_ready', 'error'
    FOREIGN KEY (book_id)    REFERENCES books(book_id) ON DELETE CASCADE,
    FOREIGN KEY (edition_id) REFERENCES editions(id)
);
```

**Normalized structure benefits:**
- `book_id` is semantically meaningful and human-readable (no opaque integer PKs)
- `book_identifiers` enables deduplication across sources without schema changes for new ID types
- All other relationships (editions, genres, formats, sources, library items) are unchanged in
  structure; only the foreign key column names change from `book_isbn` to `book_id`
- Maintains full referential integrity with `ON DELETE CASCADE` throughout

### 4.5 Extensibility model

Design source and voice support as plugin-style adapters.

- `SourceAdapter` interface:
  - `discoverBooks()`, `getBookDetails(bookId)`, `getEditionFormats(editionId)`
  - New sources can implement this interface and register with the backend.

- `VoiceProvider` interface:
  - `listPresetVoices()`, `createCustomVoice(upload)`, `generateAudio(voiceId, text)`
  - Custom voice artifacts are stored as metadata and exposed in the voice selector.

## 5. Data Flow

### 5.1 Discovery and search

1. Frontend calls `GET /api/books` or `GET /api/explore`.
2. Backend queries the book repository and source adapters.
3. Search filters are applied in the backend.
4. UI renders matching books with badges for language and source.

### 5.2 Library management

1. User taps `Add to Library`.
2. Frontend calls `POST /api/library` with selected book/edition.
3. Backend creates a `LibraryItem` record and returns library state.
4. Library screen fetches saved items and displays status.

### 5.3 Download flow

1. User selects book → choose language → choose format → choose source.
2. Frontend calls `GET /api/formats?editionId=...&language=...`.
3. Backend validates availability and returns download URL.
4. User confirms download; frontend opens URL or downloads file.

### 5.4 Audiobook flow

1. User selects a text edition and voice.
2. Frontend calls `POST /api/audiobooks/preview` using the selected voice and sample text.
3. Backend retrieves text, sends it to TTS, and returns a preview audio URL.
4. On generate, frontend calls `POST /api/audiobooks/generate`.
5. Backend stores generated audio metadata and returns a download link.

### 5.5 Custom voice upload

1. User uploads `.wav`, `.mp3`, or `.flac`.
2. Frontend sends file to `POST /api/voices/custom`.
3. Backend validates duration and audio quality.
4. Backend stores upload metadata and placeholder voice record.
5. Custom voices appear in `Voice` selection.

## 6. Deployment Strategy

### 6.1 Hosting model

For a C++ native MVP, deploy as a desktop application rather than a web-hosted SPA.

- Application distribution: packaged binaries or installers for Linux, Windows, and macOS
- Metadata storage: local SQLite database stored in the user profile or app-specific directory
- Actual book content: not stored in database; downloaded to user's local filesystem on demand or referenced from external sources
- Voice assets: local files for generated audio or user-uploaded voice samples stored in app data directory
- Remote services: optional HTTPS endpoints only for source APIs (Gutenberg, Ben-Yehuda, etc.) or hosted TTS if required

### 6.2 Development vs production

- Development: local C++ app with test source adapters and sample metadata.
- Production: packaged app bundles with real source connectors to discover books from public archives.

### 6.3 Performance and reliability

- Keep core discovery and library metadata local for fast response.
- Use background threads for search, download resolution, and audio generation.
- Validate voice uploads and audio file handling before adding custom voices.
- Cache source metadata to minimize network requests during discovery updates.

## 7. Architecture Diagram

See [classic-books-architecture.drawio](classic-books-architecture.drawio) for the complete visual architecture diagram for BookHub.

The architecture has three main layers:

1. **UI layer**
   - Qt application (Qt Quick or Qt Widgets)
   - Screens: Library, Search, Explore, BookDetailsPanel (split-pane), DownloadFlow, AudiobookFlow
   - Directly interacts with C++ application services

2. **Application layer**
   - BookService, SearchService, LibraryService, VoiceService, AudiobookService
   - Source adapters for external connectors
   - Local plugin-style architecture for future source and voice extensions

3. **Persistence & integration layer**
   - SQLite database for **metadata and details only** (no actual content)
   - Local file storage for downloaded books, generated audio, and voice samples
   - Optional remote TTS provider or HTTP integration for source APIs (Gutenberg, Ben-Yehuda)

This separation keeps the UI responsive, the business logic centralized in C++, source extension manageable, and the database lightweight by storing only metadata and links.
## 8. Rationale and Recommendations

- **Thread separation within a single program**: GUI and data collector run independently as separate threads, keeping the UI responsive while metadata is refreshed in the background on a configurable schedule.
- **Metadata-only database**: The SQLite database stores only book details, links, and user library state—not actual content. This keeps the database lightweight and allows flexibility in future content storage decisions.
- **Local content storage**: Downloaded books and generated audio are stored on the user's local filesystem, not in the database, reducing database size and simplifying backups.
- **Clear separation between discovery/search and audiobook generation**: Supports future extension and allows each flow to evolve independently.
- **The shared domain model**: Encourages consistent UI behavior across all sources and languages.
- **Voice uploads as metadata**: Treated as metadata MVP rather than a full voice-training pipeline, which keeps the first release feasible.
- **C++ native application**: Provides performance, cross-platform support, and a responsive user experience without web framework overhead.
- **Designing sources as adapters**: Enables adding Gutenberg, Ben-Yehuda, and more without UI changes.

## 9. Suggested next steps

1. Define SQLite database schema for Book, Edition, Source, Format, LibraryItem, and Voice entities.
2. Implement the first source adapter (e.g., Gutenberg) to fetch and normalize book metadata.
3. Build the data collector background thread with configurable update schedule.
4. Build the GUI components with Qt and implement Library and BookDetailsPanel first.
5. Implement Search and Explore screens once the core metadata queries are stable.
6. Add Download flow and Audiobook flow after the core discovery experience is working.
7. Implement voice preview and generation as the final MVP phase.
