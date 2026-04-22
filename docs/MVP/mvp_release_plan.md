# MVP Release Plan

## Objective
Build a minimal, usable version of the BookHub that lets bookworms discover public-domain books, save them to a library, download formats by language/source, and generate audiobooks with preset and custom voices.

---

## Release phases

### Phase 1: Core discovery and library
- Implement `Library` screen with saved books and status indicators
- Implement `Search` screen with advanced filters
- Implement `Explore` screen with categories and subcategories
- Add `Book details` screen with metadata, language selection, and availability
- Support adding/removing books from the library

### Phase 2: Download experience
- Implement format selection by language
- Support source selection per format
- Build download confirmation flow
- Validate per-book language availability and format/source filtering

### Phase 3: Audiobook conversion
- Add preset voice selection
- Support text-format-first audiobook flow
- Add preview and generate audio flow
- Add library state for audio-ready books

### Phase 4: Custom voice uploads
- Allow users to upload a short recording (`.wav`, `.mp3`, `.flac`)
- Validate duration and basic audio quality
- Create a custom voice model placeholder
- Show custom voices in voice selector

### Phase 5: Extendability and polish
- Enable additional sources without UI changes
- Add language-specific filters and multi-language edition support
- Improve UI messaging, status badges, and error handling
- Add help text for voice upload and source availability

---

## Backlog items

### High priority
- `Library` book card with language and source badges
- `Book details` with multi-language support
- Download flow with text format and source selection
- Audiobook flow with voice selection and preview
- Preset narrator voices

### Medium priority
- Advanced search filters for category, author, year, language, source
- `Explore` category/subcategory browsing
- Book library status labels and quick actions
- Language selection on book details and download steps

### Low priority
- Custom voice upload and generation
- Saved custom voices in selector
- “Trending” and “Curated collections” sections
- Additional source plugin support
- Estimated reading time and sample previews

---

## Implementation notes
- Model sources and formats using an extensible data structure so new sources can be added later.
- Keep the audiobook flow separate from direct downloads: select text source first, then voice.
- Treat each book as a parent entity with language-specific editions and format availability.
- Use consistent naming: `Library`, `Search`, `Explore`, `Book details`, `Download flow`, `Audiobook flow`.

---

## Validation criteria
- Users can add a book to the library and view it in the `Library` screen
- Users can search and explore books with relevant filters
- Users can download a book format by language and source
- Users can convert a chosen text edition into an audiobook using a preset voice
- Users can preview a voice selection before generating audio
