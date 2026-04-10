## Task Breakdown

### Task 1: Set up C++ Qt project structure
- **Description**: Initialize a new C++ project using CMake and Qt 6, create basic folder structure for GUI and data collector processes, set up SQLite integration.
- **Dependencies**: None
- **Priority**: High
- **Testability**: Verify project builds successfully and Qt window appears.
- **Trackability**: Project compiles without errors, basic Qt window launches.
- **Assignee**: Software Engineer (Backend/Infrastructure specialist)

### Task 2: Design and implement SQLite database schema
- **Description**: Create tables for Books, Formats, Sources, Genres, Library (user's saved books), and relationships. Include support for multiple languages, formats, and sources per book.
- **Dependencies**: Task 1
- **Priority**: High
- **Testability**: Run SQL queries to insert and retrieve sample book data.
- **Trackability**: Database schema created and sample data inserted successfully.
- **Assignee**: Database Specialist (Database Architect/Engineer)

### Task 3: Implement data collector process
- **Description**: Build the background process that fetches metadata from sources like Gutenberg and Ben-Yehuda, normalizes data, and updates the SQLite database.
- **Dependencies**: Task 2
- **Priority**: High
- **Testability**: Process runs and updates database with real book metadata.
- **Trackability**: Metadata from at least one source is fetched and stored.
- **Assignee**: Software Engineer (Backend/Integration specialist)

### Task 4: Create Library screen GUI
- **Description**: Implement the Library screen showing saved books with cover, title, author, language badges, source badges, and download/audiobook status. Include actions for view details, download, convert to audiobook, remove.
- **Dependencies**: Task 1, Task 2
- **Priority**: High
- **Testability**: Screen displays sample books and responds to user interactions.
- **Trackability**: Library screen renders correctly with mock data.
- **Assignee**: UI/UX Engineer (Frontend Qt specialist)

### Task 5: Implement Search screen
- **Description**: Build advanced search panel with filters for title/keywords, author, category/genre, publication year, language, source, availability. Display results with metadata and quick add actions.
- **Dependencies**: Task 1, Task 2, Task 3
- **Priority**: High
- **Testability**: Search queries return filtered results and add to library works.
- **Trackability**: Search functionality operational with sample data.
- **Assignee**: UI/UX Engineer (Frontend Qt specialist)

### Task 6: Create Explore screen
- **Description**: Implement category browse UI with top-level category cards, subcategory drill-down, and discovery sections like Trending, New arrivals, Curated collections.
- **Dependencies**: Task 1, Task 2, Task 3
- **Priority**: High
- **Testability**: Categories display and drill-down navigation works.
- **Trackability**: Explore screen shows categories and navigates properly.
- **Assignee**: UI/UX Engineer (Frontend Qt specialist)

### Task 7: Build Book details screen
- **Description**: Create full metadata view showing title, author, summary, publisher/date, languages, sources, formats. Include controls for Add to Library, Download, Convert to audiobook. Handle multiple languages.
- **Dependencies**: Task 1, Task 2
- **Priority**: High
- **Testability**: Details display correctly and actions trigger appropriately.
- **Trackability**: Book details screen shows complete metadata.
- **Assignee**: UI/UX Engineer (Frontend Qt specialist)

### Task 8: Implement add/remove from library functionality
- **Description**: Add backend logic and UI integration for saving books to user's library and removing them. Update library status across screens.
- **Dependencies**: Task 4, Task 7
- **Priority**: High
- **Testability**: Books can be added and removed, reflected in Library screen.
- **Trackability**: Library management works end-to-end.
- **Assignee**: Full-Stack Developer (Backend/Database integration)

### Task 9: Develop download flow
- **Description**: Implement format selection by language, source selection per format, download confirmation flow. Validate language availability and format/source filtering.
- **Dependencies**: Task 7
- **Priority**: Medium
- **Testability**: Users can select language, format, source and initiate download.
- **Trackability**: Download flow completes successfully for sample books.
- **Assignee**: Full-Stack Developer (Backend/Database integration)

### Task 10: Build audiobook conversion flow
- **Description**: Add preset voice selection, text-format-first audiobook flow, preview and generate audio flow. Update library state for audio-ready books.
- **Dependencies**: Task 7, Task 9
- **Priority**: Medium
- **Testability**: Users can select text format, choose voice, preview, and generate audiobook.
- **Trackability**: Audiobook generation works for sample text.
- **Assignee**: Audio Engineer (TTS specialist)

### Task 11: Integrate TTS for voice preview and generation
- **Description**: Integrate native or external TTS service for voice preview and audiobook generation. Support preset voices.
- **Dependencies**: Task 10
- **Priority**: Medium
- **Testability**: Voices play previews and generate audio files.
- **Trackability**: TTS integration functional with sample voices.
- **Assignee**: Audio Engineer (TTS specialist)

### Task 12: Add custom voice upload feature
- **Description**: Allow users to upload short recordings (.wav, .mp3, .flac), validate duration and quality, create custom voice model placeholder, show in voice selector.
- **Dependencies**: Task 10, Task 11
- **Priority**: Low
- **Testability**: Users can upload and select custom voices.
- **Trackability**: Custom voice upload and selection works.
- **Assignee**: Audio Engineer (ML/Audio processing specialist)

### Task 13: Implement source adapters for extensibility
- **Description**: Create pluggable C++ components for Gutenberg, Ben-Yehuda, and future providers. Ensure UI doesn't change when adding sources.
- **Dependencies**: Task 3
- **Priority**: Low
- **Testability**: New source can be added without UI changes.
- **Trackability**: Source adapter system extensible.
- **Assignee**: Software Engineer (Backend/Architecture specialist)

### Task 14: Add UI polish and error handling
- **Description**: Improve UI messaging, status badges, error handling, help text for voice upload and source availability. Add language-specific filters.
- **Dependencies**: All previous tasks
- **Priority**: Low
- **Testability**: UI handles errors gracefully and provides helpful messages.
- **Trackability**: Application polished and user-friendly.
- **Assignee**: UI/UX Engineer (Frontend Qt specialist)

### Task 15: Package and deploy application
- **Description**: Create platform-native installers or self-contained app bundles for Windows, macOS, and Linux.
- **Dependencies**: All previous tasks
- **Priority**: Low
- **Testability**: Application installs and runs on target platforms.
- **Trackability**: Deployable packages created.
- **Assignee**: DevOps Engineer (Packaging specialist)

