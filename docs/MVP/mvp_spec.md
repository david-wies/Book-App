# MVP Specification

## Overview

**Project name:** Classic Books + Audiobook Hub

**Goal:** Build a GUI app that lets book lovers discover public-domain books from sources like Gutenberg and Ben-Yehuda, save them to a personal library, download available ebook formats, and convert chosen text editions into audiobooks with preset and custom voices.

**Target users:** Bookworms who enjoy classic literature, multi-language editions, and personalized audiobook conversion.

---

## MVP Scope

### Core screens
- `Library`
- `Search`
- `Explore`
- `Book details`
- `Download / Audiobook flow`

### Core features
- Add books to library
- Browse books via advanced search
- Explore categories and subcategories
- View full book details, languages, formats, and sources
- Download book formats by language and source
- Convert text into audiobook using preset voices
- Upload a custom voice sample for personalized TTS

### Future-ready requirements
- Support books with multiple languages
- Support additional sources later without changing UI flow
- Use an extensible source/format model

---

## Screen map

### 1. Library
- Name: `Library` instead of “My Books”
- Shows saved books and current library status
- Display:
  - cover, title, author
  - language badges
  - source badges
  - download / audiobook status
- Actions:
  - `View details`
  - `Download`
  - `Convert to audiobook`
  - `Remove from library`

### 2. Search
- Advanced search panel
- Filters:
  - title / keywords
  - author
  - category / genre
  - publication year
  - language
  - source
  - availability (`ebook`, `audiobook support`)
- Results show metadata and quick add actions

### 3. Explore
- Category browse UI
- Top-level category cards
- Subcategory drill-down
- Discovery sections:
  - `Trending`
  - `New arrivals`
  - `Curated collections`

### 4. Book details
- Full metadata view
- Show:
  - title, author
  - summary
  - publisher / published date
  - languages available
  - sources available
  - formats available by language
- Controls:
  - `Add to Library`
  - `Download`
  - `Convert to audiobook`
- If multiple languages exist:
  - choose language first
  - show language-specific availability

### 5. Download / Audiobook flow
- Step 1: choose language edition
- Step 2: choose ebook format
- Step 3: choose source
- For audiobook:
  - choose text format first
  - choose voice
  - preview sample
  - generate and save/download

---

## Clickable wireframe outline

### Main navigation
- `Library`
- `Search`
- `Explore`

### Library flow
1. Open `Library`
2. Select book card
3. Open `Book details`
4. Tap `Download` or `Convert to audiobook`

### Search flow
1. Open `Search`
2. Apply filters
3. Pick a result
4. Open `Book details`
5. Add to library or start download/audio flow

### Explore flow
1. Open `Explore`
2. Pick category
3. Pick subcategory
4. Pick a book
5. Open `Book details`

### Book details actions
- `Add to Library`
- `Download`
- `Convert to audiobook`

### Download flow
1. Select language
2. Select format
3. Select source
4. Confirm and download

### Audiobook flow
1. Select language
2. Select text format
3. Select voice
4. Preview
5. Generate audio
6. Download audio file

---

## Data model / entities

- `Book`
  - id
  - title
  - authors
  - summary
  - publication date
  - categories
  - language editions

- `Edition`
  - bookId
  - language
  - publisher
  - publish date
  - description

- `Source`
  - id
  - name (`Gutenberg`, `Ben-Yehuda`, etc.)
  - metadata
  - supported languages

- `Format`
  - id
  - editionId
  - sourceId
  - type (`txt`, `epub`, `pdf`, `html`)
  - availability
  - download URL

- `LibraryItem`
  - bookId
  - selectedLanguage
  - selectedFormat
  - addedAt
  - status

- `Voice`
  - id
  - name
  - type (`preset`, `custom`)
  - sampleText
  - uploadStatus
  - voiceModelReference

---

## Voice customization MVP

- Predefined voices:
  - `Classic Storyteller`
  - `Warm Listener`
  - `Crisp Narrator`
- Custom voice upload:
  - upload short recording (`.wav`, `.mp3`, `.flac`)
  - validate duration and file quality
  - generate a custom voice option
  - preview generated voice
- Saved custom voices appear in voice selector

---

## Recommended folder structure

- `docs/MVP/`
  - `mvp_spec.md`
  - `mvp_spec.json`
