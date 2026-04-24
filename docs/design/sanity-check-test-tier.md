# Design: Sanity Check Test Tier

## Purpose

BookHub now has enough automated coverage that we should split CI into two test tiers:

- `sanity check`: fast, high-signal verification for pull requests into `develop`
- `full suite`: complete automated verification for pull requests into `master`

The goal is to keep `develop` feedback fast while still protecting the highest-risk paths, and to keep `master` protected by the full regression suite.

---

## Proposed CI Policy

### PR to `develop`

Run only the `sanity check` tier.

This tier should:

- finish quickly
- cover core database and identity rules
- cover one smoke-level path for the main GUI shell
- avoid broad widget permutations, pagination-heavy tests, and slower event-loop-heavy scenarios unless they catch a critical regression class

### PR to `master`

Run the full automated test suite.

This tier should:

- include every `sanity check` test
- include all service, integration, and GUI behavior tests
- serve as the release-quality regression gate

---

## Selection Rules

A test belongs in the `sanity check` tier if it satisfies most of these:

- it protects a core architectural invariant
- it catches high-severity regressions
- it is deterministic in headless CI
- it is fast to execute
- it does not depend on unfinished UI
- it gives broad confidence relative to its runtime cost

A test should stay out of the `sanity check` tier if it is primarily:

- a detailed widget behavior test
- a pagination/debounce/timer-heavy scenario
- a wide combinatorial filter test
- a design-acceptance test for non-critical UX details
- a test for future or partial functionality

---

## Proposed Sanity Check Scope

These are the initial tests that should make up the `sanity check` group.

### 1. Database and schema smoke

- test target: `test_database_schema`
- why it belongs:
  - validates schema creation
  - validates schema version checks
  - validates uniqueness and cascade behavior
  - catches the most damaging database regressions early

### 2. Gutenberg identifier resolution smoke

- test target: `test_gutenberg_id_resolution`
- why it belongs:
  - validates canonical identifier resolution
  - protects LCCN normalization and fallback behavior
  - covers one of the most fragile data-normalization paths

### 3. Discovery deduplication and promotion smoke

- test target: `test_book_discovery_service`
- why it belongs:
  - validates insertion, deduplication, and stronger-ID promotion
  - protects the book identity model, which is a central project risk

### 4. Library service smoke

- test target: `test_library_service`
- why it belongs:
  - validates add/remove/update operations
  - ensures core user-library persistence still works

### 5. Main window shell smoke

- test target: `test_main_window`
- why it belongs:
  - validates application shell construction
  - validates core navigation
  - provides a cheap top-level GUI smoke test

---

## Tests Excluded From Sanity Check

These should still run in the full suite, but not in the initial `develop` gate.

### Full-suite only for now

- `test_library_screen`
- `test_search_service`
- `test_search_screen`
- `test_explore_service`
- `test_explore_screen`

### Why they are excluded

- they test broader feature behavior rather than minimal platform health
- some are more UI-interaction-heavy than necessary for a fast `develop` gate
- some cover secondary workflows rather than the most critical invariants

This does not mean they are low value. It only means they are better suited to the full regression gate on `master`.

---

## Growth Rules

We should add a test to `sanity check` only if one of these becomes true:

- a regression escaped because the full suite ran too late
- the test repeatedly catches real breakages in core workflows
- the runtime cost stays low while confidence gain is high

We should remove a test from `sanity check` if:

- it becomes flaky in CI
- its runtime grows noticeably
- it duplicates confidence already provided by another cheaper test

---

## Mapping To Current Test Tasks

Included in `sanity check`:

- Test Task 1: Database and schema integrity tests
- Test Task 2: Gutenberg identifier resolution tests
- Test Task 3: Book discovery deduplication and promotion tests
- Test Task 4: Library service tests
- Test Task 6: Main window shell and navigation tests

Full-suite only:

- Test Task 5: Library screen GUI tests
- Test Task 7: Search service tests
- Test Task 8: Search screen GUI tests
- Test Task 9: Explore service tests
- Test Task 10: Explore screen GUI tests
- Test Task 11+: partial and blocked buckets

---

## Recommended Naming

Use these names consistently in code and CI:

- `sanity`: the fast `develop` PR gate
- `full`: the complete `master` PR gate

Avoid names like `smoke`, `quick`, or `basic` unless we intentionally want a third tier later.

---

## Future Implementation Notes

When we wire this into CMake/CTest or CI, the simplest shape is:

- one label or grouping mechanism for `sanity`
- one full run that executes all tests

The `full` tier should not be a separately maintained list if we can avoid it. Prefer:

- `sanity` = explicit subset
- `full` = everything

That keeps maintenance low and avoids forgetting to add new tests to the full suite.

---

## Current Runner Integration

The CMake/CTest suite now labels the sanity-check tests with the `sanity` label.

### Local commands

Run the sanity tier only:

```bash
ctest --test-dir build -L sanity --output-on-failure
```

Run the full suite:

```bash
ctest --test-dir build --output-on-failure
```

List all sanity tests:

```bash
ctest --test-dir build -N -L sanity
```

### Current sanity-labeled tests

- `test_database_schema`
- `test_gutenberg_id_resolution`
- `test_book_discovery_service`
- `test_library_service`
- `test_main_window`

### CI mapping

Suggested `develop` gate:

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build -L sanity --output-on-failure
```

Suggested `master` gate:

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```
