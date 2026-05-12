# Design: Test Strategy and Suggested Coverage

## Table of Contents
- [Overview](#overview)
- [Testing Goals](#testing-goals)
- [Scope by Delivery Stage](#scope-by-delivery-stage)
- [Recommended Test Layers](#recommended-test-layers)
- [Framework Setup](#framework-setup)
- [Highest‑Priority Tests](#highest‑priority-tests)
- [Unit Test Suggestions](#unit-test-suggestions)
- [Integration Test Suggestions](#integration-test-suggestions)
- [Test Implementation Quality Rules](#test-implementation-quality-rules)
- [GUI Test Suggestions](#gui-test-suggestions)
- [Manual Acceptance Checklist](#manual-acceptance-checklist)
- [Suggested Seed Datasets](#suggested-seed-datasets)
- [Implementation Order](#implementation-order)
- [Definition of Done for New Screens](#definition-of-done-for-new-screens)
- [Revision History](#revision-history)

## Overview

This document turns the current design work into a practical test plan for BookHub. It is intentionally
forward‑looking: the scope includes both:

- behavior that already exists in the current codebase
- behavior that is planned in the design documents but not implemented yet

It is based on:

- `docs/design/gui-design-spec.md`
- `docs/design/book-identity-model.md`
- the current SQLite schema in `src/shared/database.cpp`
- the current collector flow in `src/collector/`

The goal is to catch regressions and guide implementation in two high‑risk areas:

1. data correctness, especially book identity resolution and cross‑source deduplication
2. GUI behavior, especially screen states, navigation, and per‑book actions

---

## Testing Goals

- Define expected behavior before all planned screens and flows are built.
- Verify that identifier resolution produces stable canonical `book_id` values.
- Verify that repeated discovery runs do not create duplicate logical books.
- Verify that schema rules and foreign keys preserve data integrity during promotion and deletion.
- Verify that planned GUI flows behave correctly across loading, empty, populated, and error states.
- Verify that user‑visible actions map cleanly to service/database changes.

See the tiering policy in [Sanity-Check Test Tier](sanity-check-test-tier.md).

---

## Scope by Delivery Stage

### Current implementation

These tests can be written against code that already exists or is partially in place:

- database schema creation and version checks
- sample data insertion behavior
- Gutenberg identifier normalization and RDF parsing helpers
- `BookDiscoveryService` insertion, deduplication, and promotion logic
- main window shell behavior
- library screen behavior

### Planned implementation

These tests should be treated as design‑level acceptance criteria for upcoming work:

- Search screen filters, pagination, and add‑to‑library flow
- Explore screen category drill‑down, breadcrumbs, and carousel behavior
- Book details panel language switching, summary expansion, and action‑state rules
- Download flow dialog validation, progress, completion, and retry behavior
- Audiobook flow dialog voice selection, preview, upload validation, generation progress, and completion

For planned features, the tests define the intended behavior even when the production code does not exist yet.
That makes this document a test‑design reference, not only a snapshot of current coverage needs.

---

## Recommended Test Layers

| Layer | Scope | Best fit |
|------|-------|----------|
| Unit tests | pure logic and small helpers | ID normalization, priority selection, query/state helpers |
| Integration tests | SQLite + service behavior | `BookDiscoveryService`, schema constraints, library queries |
| GUI tests | widgets and flows | tab switching, empty states, dialogs, button enable/disable logic |
| Manual acceptance tests | final UX sanity checks | sizing, layout quality, keyboard flow, visual regressions |

---

## Framework Setup

- Use `Qt6::Test` for widget tests, signal assertions, and event‑loop‑aware checks.
- Add a dedicated `tests/` tree with temporary SQLite databases per test case.
- Prefer small fixture builders that seed `books`, `book_identifiers`, `editions`, `formats`, `sources`, and `library_items`.
- Keep network and filesystem side effects mocked or isolated behind temporary directories.

Suggested structure:

```text
tests/
├── unit/
│   ├── test_gutenberg_id_resolution.cpp
│   └── test_database_schema.cpp
├── integration/
│   ├── test_book_discovery_service.cpp
│   ├── test_explore_service.cpp
│   ├── test_library_service.cpp
│   └── test_search_service.cpp
└── gui/
    ├── test_main_window.cpp
    ├── test_library_screen.cpp
    ├── test_search_screen.cpp
    └── test_explore_screen.cpp
```

---

## Highest‑Priority Tests

These should be implemented first because they protect the most fragile behavior and provide the strongest
foundation for future planned screens.

### 1. Book identity resolution

- Resolves `lccn` ahead of `oclc`, `isbn`, and source fallback.
- Accepts `lccn:`-prefixed strings and normalises them to canonical `lccn:<value>` form.
- Rejects LOC names-authority URIs (`/authorities/names/`) — they identify persons, not works.
- Converts ISBN‑10 into normalized ISBN‑13.
- Falls back to `gutenberg:<id>` when no stronger identifier exists.
- Stores all discovered identifiers, not only the winning one.

### 2. Deduplication and promotion

- Inserts a new book when no identifiers match an existing row.
- Reuses the existing `book_id` when any incoming identifier already exists.
- Promotes a fallback key such as `gutenberg:1342` to a stronger identifier (e.g. `lccn:n79025140` or `oclc:42707429`) when one arrives in a later discovery run.
- Preserves related `editions`, `formats`, `sources`, and `library_items` after promotion.
- Leaves the old fallback identifier searchable through `book_identifiers`.

### 3. Schema integrity

- `book_identifiers(type, value)` uniqueness blocks duplicate identifier rows.
- `ON UPDATE CASCADE` updates dependent rows during `book_id` promotion.
- `ON DELETE CASCADE` removes dependent rows when a book is deleted.
- `verifySchemaVersion()` accepts fresh databases and rejects mismatched versions.

### 4. Library screen behavior

- Empty state appears when there are no library items.
- Populated state renders rows from the database.
- Sort order changes list order correctly.
- Remove action shows confirmation and deletes the item on confirm.
- Grid/list mode toggle persists through `QSettings`.

---

## Unit Test Suggestions

### `GutenbergAdapter`

- `normalizeLccn()` zero‑pads pre‑2001 numeric portions correctly.
- `normalizeLccn()` preserves prefixed letter segments.
- `resolveBookId()` chooses the highest‑priority available identifier (LCCN > OCLC > ISBN > gutenberg fallback).
- `resolveBookId()` ignores `/authorities/names/` URIs — they identify persons, not works. With only such a URI present the resolver falls back to `gutenberg:<id>`.
- `resolveBookId()` falls back to OCLC when a names-authority URI accompanies a valid OCLC number.
- `normalizeFormatName()` maps known MIME types to expected internal names.
- RDF parsing ignores image‑only formats.
- RDF parsing stores only `lccn`-prefixed identifiers in `book.identifiers`; names-authority URIs produce no `lccn` entry.
- RDF parsing strips MARC 21 subfield markers (e.g. `$b`) from title strings, replacing them with `": "` to preserve subtitle semantics.
- RDF title parsing collapses embedded newlines and leading whitespace via `simplified()`.
- RDF title parsing ignores `<title>` elements that are not direct children of `<ebook>`.
- RDF language codes are stored raw from the RDF (`"nl"`, `"fr"`, etc.); normalisation to full names happens in `BookDiscoveryService`.
- Multiple formats of the same MIME type are stored with `_N` suffixed keys (`epub_1`, `epub_2`) so neither URL is lost.

### Database helpers

- `databaseFilePath()` resolves to `QStandardPaths::AppDataLocation`.
- `createSchema()` creates all expected tables.
- `insertSampleData()` is idempotent — running it twice produces the same row counts.
- `verifySchemaVersion()` runs the v4→v5 migration: strips `_N` format-type suffixes, cleans MARC-contaminated titles, and remaps names-authority `lccn:n...` book IDs to `gutenberg:<id>` fallback.

### Query/state helpers to extract later

If search, explore, and details logic grow, extract small query‑builder or presenter helpers and add unit tests for:

- active filter to SQL fragment mapping
- sort option to `ORDER BY` mapping
- status value to badge/icon/label mapping
- button state rules such as `Add to Library`, `Download`, and `Convert to Audiobook`

---

## Integration Test Suggestions

### `BookDiscoveryService`

- Inserts one book with one edition, multiple formats, and matching source rows.
- Handles repeated discovery of the same source book without exploding row counts.
- Merges two source records that share an LCCN into one logical book.
- Accepts a later stronger identifier and promotes the primary key in a transaction‑safe way.
- Keeps first‑writer book metadata when a later source has different title/summary values.
- Normalises ISO 639‑1/2 language codes (e.g. `"nl"`) to English full names (e.g. `"Dutch"`) before storage.
- Strips the `_N` de-collision suffix from format keys before storage (`"epub_1"` → `"epub"`).
- Stores a single `format_type` row when a book has two files of the same MIME type, with both download URLs as separate `sources` rows.

Representative scenarios:

1. Gutenberg‑only book with fallback ID
2. Gutenberg book later rediscovered with LCCN
3. Gutenberg and Archive records with shared `oclc`
4. two editions with same `book_id` but different languages
5. duplicate identifier insertion on a second collector run

### `SearchService`

- Keyword containing `%` or `_` is escaped before being passed to the `LIKE` clause, so it matches
  literal characters rather than SQL wildcards.
- Empty keyword with no filters returns all books. `SearchParams{}` is a no‑filter query and must
  return every row in the database; test with `QCOMPARE(results.size(), totalBooks)`.
- AND semantics hold across all active filters simultaneously.

### Library/database integration

- Adding a library item makes it appear in the library query.
- Removing a book cleans up dependent library rows safely.
- Audiobook‑ready status is queryable for search filters.
- Distinct languages, formats, and source lists are returned correctly for details/search views.

---

## Test Implementation Quality Rules

These rules apply to every test in this suite. They exist because several fragility patterns appeared
during early implementation and are easy to repeat.

### Never hardcode database row IDs

Row IDs from `autoincrement` columns depend on insertion order and are not stable across sample data
changes. Always query for the ID after the row is inserted:

```cpp
// Wrong
service.addBook("gutenberg:1184", 4);

// Right
const int editionId = testDb.scalarInt("SELECT id FROM editions WHERE book_id = 'gutenberg:1184'");
service.addBook("gutenberg:1184", editionId);
```

### Never use a whitespace string as a wildcard keyword

Using `keyword = " "` to retrieve all results is fragile: if the service trims whitespace before
querying, it returns zero results, and an assertion like `QVERIFY(results.size() >= N)` passes
vacuously. Use `SearchParams{}` with no keyword to express "no filter", or test the empty‑keyword
contract explicitly with `QCOMPARE`.

### Avoid `QTest::qWait` for debounce timing

`QTest::qWait(550)` is slow and flaky on loaded CI runners. Prefer one of:

- Make the debounce delay configurable and set it to 0 in tests.
- Expose a `triggerSearch()` slot that bypasses the debounce timer entirely.
- Use `QSignalSpy::wait()` with a generous timeout instead of a fixed sleep.

### Avoid short timers to accept modal dialogs

A `QTimer` with a 10ms interval fires before the dialog renders on a slow machine. Use
`QTimer::singleShot(0, ...)` plus `QCoreApplication::processEvents()` after the action, or
spy on the dialog's `finished` signal directly.

### Document non‑obvious ordering assertions

If a test asserts a specific item at position 0, add a comment explaining the invariant that
produces that order. Assertions like `QCOMPARE(trending.first().bookId, "book:25")` with no
explanation are opaque and break silently when query ordering changes.

### Clarify fresh‑database contracts

`verifySchemaVersion` returning `true` for an empty database is only correct if the production
code defines an empty database as fresh‑and‑valid. Wherever this contract is tested, name the
test `freshDatabase_isValid` and `versionMismatch_isInvalid` as separate cases with a comment
stating the intended invariant.

---

## GUI Test Suggestions

These tests align directly with `docs/design/gui-design-spec.md`.

Most of the sections below target planned implementation. They should be used as acceptance criteria as
those screens and flows are added.

### Main window and navigation

- Window opens at or above minimum size.
- Navigation buttons switch the `QStackedWidget` index correctly.
- Only one nav tab is active at a time.
- Status bar text and activity indicator update when collector signals fire.
- Keyboard activation works with Tab and Enter/Space on nav buttons.

### Library screen

- Loading indicator appears while the initial query is in progress.
- Empty state shows the expected message and CTA when the library is empty.
- Clicking `Explore Books` switches to the Explore tab.
- Double‑clicking a row opens the details panel.
- List‑mode row buttons trigger details, download, audiobook, and remove actions.
- Grid mode hides inline action buttons and still supports double‑click to details.
- Status labels render correctly for `saved`, `downloading`, `downloaded`, `converting`, `audiobook_ready`, and `error`.

### Search screen

- Debounced keyword search triggers only once after the delay.
- Enter in the search bar triggers an immediate search.
- Filter combinations use AND semantics.
- `Clear filters` resets all controls and refreshes results.
- Year range clamps invalid `from > to` input.
- `Load more` appends the next page instead of replacing the existing model.
- `+ Library` changes to `In Library` after adding a result.
- No‑results state shows the expected guidance.

### Explore screen

- Top‑level categories render with counts.
- Clicking a category navigates to subcategories.
- Clicking a subcategory shows the book grid.
- Back button and breadcrumb reflect navigation depth.
- Empty‑genre state appears with the collector note.
- Carousel cards open the details panel.

### Book details panel

- Panel opens from library, search, and explore contexts.
- Language selector appears only for multi‑language books.
- Changing language refreshes available formats and sources.
- Summary defaults to collapsed and expands with `Show more`.
- `Add to Library` changes to `In Library` after success.
- `Download` is disabled or hidden when no formats exist.
- `Convert to Audiobook` is enabled only when text‑compatible formats exist.

### Download flow dialog

- `Next` stays disabled until a choice is made at each step.
- Changing language clears a previously selected format.
- Pre‑seeded launch from the details panel skips or auto‑selects known values correctly.
- Progress bar updates on download progress signals.
- Error state surfaces retry behavior.
- Completion state exposes `Open file` and `Close`.

### Audiobook flow dialog

- Step indicator advances and rewinds correctly.
- Only text‑compatible formats are available in the format step.
- Voice preview buttons start the correct sample.
- Upload dialog validates file type and duration bounds.
- Generate step updates progress and supports cancel.
- Completion state exposes output actions and marks the book as audiobook‑ready if that behavior is implemented.

---

## Manual Acceptance Checklist

- Resize from minimum width up to large desktop sizes without clipped controls.
- Verify keyboard‑only navigation across shell, list rows, filters, and dialogs.
- Verify screen‑reader labels for major controls and cards.
- Verify placeholder cover, empty state, loading state, and error state visuals.
- Verify long titles, multiple authors, and multi‑language books do not break layouts.
- Verify right‑to‑left or Hebrew metadata does not render incorrectly in cards and details views.

---

## Suggested Seed Datasets

Create a few reusable fixtures to keep tests readable.

### Fixture A: simple single‑source book

- one book
- one language
- one format
- one source
- not in library

### Fixture B: multi‑language classic

- one book
- English and French editions
- multiple formats per edition
- summary present
- in library with `saved` status

### Fixture C: deduplication promotion case

- existing DB row keyed as `gutenberg:1342`
- incoming discovery record with matching Gutenberg identifier plus a stronger key (e.g. `lccn:n79025140` or `oclc:42707429`)
- existing dependent rows in `editions`, `formats`, `sources`, and `library_items`

### Fixture D: audiobook‑capable book

- text format available
- library item status transitions from `saved` to `converting` to `audiobook_ready`

### Fixture E: edge‑case metadata

- missing summary
- no cover
- long title
- multiple authors
- unusual or missing publish year

---

## Implementation Order

1. Add `Qt6::Test` support in CMake and create a `tests/` target.
2. Implement unit tests for identifier normalization and resolution.
3. Implement integration tests for `BookDiscoveryService` and schema cascade behavior.
4. Add library screen widget tests, since that screen already exists.
5. Create pending or scaffolded test cases for Search, Explore, Details, Download, and Audiobook behavior directly from the design docs.
6. Convert those scaffolded planned‑feature tests into active tests as each component lands.

---

## Definition of Done for New Screens

A new screen or flow should not be considered complete until it has:

- one happy‑path GUI test
- one empty/loading/error‑state GUI test
- one integration test covering its backing query or service behavior
- one regression test for its riskiest branching rule

This keeps the test suite aligned with the design documents instead of adding coverage only after bugs appear.

---

## Revision History

| Date | Author | Change |
|---|---|---|
| 2026‑04‑28 | Antigravity | Added Table of Contents, cross‑link to sanity‑check tier, standardized headings, and revision history. |
| 2026‑05‑12 | David | Expanded unit test suggestions to cover MARC title stripping, names-authority rejection, raw language storage, and multi-format suffix keys; expanded `BookDiscoveryService` integration suggestions to cover language normalisation, format suffix stripping, and multi-URL deduplication; added migration test to database-helper suggestions; updated Fixture C and promotion example to remove incorrect `lccn:n78095332` reference. |

(End of file)
