# Unicode Roadmap

This document lays out a staged path from the current byte-based prototype to grapheme-aware cursor movement and eventual right-to-left text support.

Current focus:

- Completed:
  - Stage 1.5 — editable `TextStorage` on the treap core.
  - Stage 1.6 — storage-local edit-path performance recovery relative to the earlier treap prototype.
  - Stage 1.7 — bounded storage maintenance for long edit sessions without reintroducing editor/query caches into storage.
  - Stage 1.8 — recent contiguous edit fast paths plus typing-oriented benchmark coverage for in-document typing workloads.
- Current focus:
  - Stage 2 — minimal byte-type split for cursor and offset semantics.

The current state is intentionally limited:

- `TextStorage` stores UTF-8 text.
- `TextBuffer` cursor columns are UTF-8 byte offsets.
- The UI maps byte offsets to Qt cursor positions only well enough to avoid landing in the middle of a UTF-8 code point during painting.
- Cursor movement is not grapheme-aware.
- Bidirectional text behavior is not yet modeled.

The goal is to improve correctness in small steps without forcing premature Unicode-library decisions into the wrong layers.

## End state goals

- `TextStorage` has a stable storage-oriented boundary that can survive a move from the current line-based prototype to a piece tree.
- Buffer and editor movement APIs can distinguish raw byte offsets from grapheme-aligned positions.
- Cursor left/right movement is grapheme-aware.
- Vertical movement preserves a preferred visual column rather than only a byte column.
- UI rendering and cursor placement are consistent for non-Latin text.
- A dedicated Unicode boundary exists before any heavy Unicode or bidirectional logic spreads through the rest of the codebase.
- If right-to-left behavior is added, logical buffer positions and visual cursor behavior are explicitly modeled instead of being mixed implicitly.

## Stage 0: Keep the current simplification explicit

Purpose:
Record the current behavior clearly so later steps can replace it deliberately rather than accidentally.

Work:

- Keep comments and type names explicit that current cursor columns are UTF-8 byte offsets.
- Do not add more ad hoc Unicode logic to `buffer` or `ui`.
- Keep UTF-8 and Qt conversion logic isolated in the existing UI mapping layer.

Exit criteria:

- No new code assumes byte-based cursor movement is Unicode-correct.
- New APIs do not blur byte offsets with future grapheme-aware positions.

## Stage 1: Solidify `TextStorage` and `TextBuffer` boundaries

Purpose:
Make the storage and document layers stable enough that Unicode work does not have to be redone during a later representation swap.

Work:

- Keep `TextStorage` focused on storage-oriented responsibilities such as:
  - storing original text
  - retrieving text slices or line-oriented views
  - mapping between storage facts and lightweight query operations
- Keep `TextBuffer` focused on document behavior such as:
  - file identity
  - cursor movement policy
  - higher-level editing semantics
- Avoid baking the current line-vector prototype into public storage APIs.
- If an implicit treap or piece-tree prototype already exists and is likely to be the near-term direction, treat it as a candidate storage core rather than something to import directly.
- Before adopting an existing prototype behind `TextStorage`:
  - separate storage mechanics from any ad hoc Unicode, grapheme, normalization, or cursor logic layered on top of it
  - move Unicode-sensitive behavior behind the planned `unicode` boundary instead of keeping it inside the storage implementation
  - add focused unit tests, property tests, and benchmarks for the storage layer itself
  - verify that the resulting `TextStorage` API still describes storage/query capabilities rather than editor behavior
- Once those conditions are met, integrate the treap or piece-tree behind `TextStorage` before grapheme-aware cursor behavior becomes widespread.
- Preserve line-oriented query methods if they remain useful, but make sure they do not promise that lines are the underlying representation.
- Keep byte-offset semantics explicit during this stage. Do not mix storage refactors and grapheme-aware behavior in the same change unless there is a strong reason.

Exit criteria:

- `TextStorage` can evolve from the current prototype toward a piece tree without forcing major public API churn.
- `TextBuffer` no longer depends on line-vector-specific assumptions beyond the narrow storage API.
- Any reused implicit-treap or piece-tree code is carrying storage responsibilities only, not embedded ad hoc Unicode policy.
- Unicode work can proceed on top of stable storage/document boundaries.

## Stage 2: Strengthen domain types before changing behavior

Purpose:
Create the type-level separation needed for Unicode-correct movement.

Work:

- Split the current generic column concepts into more specific types as needed.
- Introduce explicit types such as:
  - `ByteColumn`
  - `GraphemeBoundaryByteColumn`
  - `VisualColumn` or `PreferredVisualColumn` if needed for vertical movement
  - `QtUtf16Column` at the UI boundary if needed
- Keep conversion functions explicit. Do not add primitive overloads that bypass the stronger types.
- Update `CursorPos` or related cursor types so the stored invariants are obvious.

Exit criteria:

- Public APIs can express whether a position is merely byte-based or known to be grapheme-aligned.
- New movement work can be added without changing unrelated APIs later.

## Stage 1.6: Recover storage-local performance before broader editor edits

Purpose:
Bring the editable treap-backed `TextStorage` back toward the performance characteristics of the earlier prototype before more editing behavior is layered on top.

Work:

- Use the old implicit-treap prototype as a comparison baseline for edit-path behavior, not as a source of editor or Unicode policy.
- Profile the current `TextStorage` edit workloads and identify which regressions come from storage-local behavior rather than from removed editor caches or Unicode shortcuts.
- Prefer restoring storage-local optimizations such as:
  - append fast paths
  - cheaper split/merge paths for common insert and erase cases
  - less repeated coalescing work
  - avoiding unnecessary temporary allocations and full-text materialization in hot paths
  - lightweight structural heuristics that stay entirely inside `TextStorage` / the treap core
- Reintroduce only optimizations that preserve the current clean boundary:
  - no editor caches
  - no cursor caches
  - no ad hoc Unicode logic
  - no UI-aware behavior
- Keep the benchmark target current and compare new measurements against:
  - the previous editable-storage baseline in this repo
  - representative results from the earlier prototype where available
- If a performance optimization would require leaking storage representation details into `TextBuffer`, `ui`, or future Unicode code, stop and reconsider rather than crossing the boundary casually.

Testing:

- Keep all correctness tests green while optimizing.
- Expand or refine benchmarks when a suspected hotspot is not already covered.
- Add focused regression tests if an optimization changes coalescing, split behavior, or mutation edge cases.

Exit criteria:

- Editable-storage benchmarks show clear improvement on the main insert/erase workloads.
- Optimizations remain local to the storage layer and do not reintroduce editor/Unicode policy into `TextStorage`.
- `TextStorage` remains byte-preserving, testable, and boundary-clean enough for the next editing and Unicode stages.

## Stage 1.7: Add bounded storage maintenance

Purpose:
Keep long edit sessions from accumulating avoidable piece fragmentation, while still keeping maintenance logic inside storage rather than in the UI or document behavior layers.

Work:

- Add a bounded compaction pass inside the treap-backed storage core that:
  - collects pieces in document order
  - merges only adjacent compatible pieces
  - stops after a merge budget rather than trying to fully rewrite the document every time
- Trigger the maintenance pass conservatively after real mutations instead of on every edit.
- Use simple heuristics such as:
  - minimum number of edits since the previous maintenance pass
  - minimum piece-count threshold before maintenance is even attempted
- Keep the maintenance logic storage-local:
  - no line finger caches
  - no cached decoded lines
  - no grapheme or visual-column caches
  - no UI-aware policies

Testing:

- Keep all existing storage and buffer tests green.
- Add targeted tests that verify maintenance preserves exact text and invariants.
- Re-run the fixed release benchmark to confirm the maintenance pass does not introduce broad regressions.

Exit criteria:

- Storage can periodically compact mergeable piece fragmentation without public API changes.
- Maintenance remains internal to storage and does not reintroduce editor/query caches into the treap layer.
- Release benchmarks remain acceptable after the maintenance heuristics are added.

Status:

- Completed.
- The current implementation has:
  - bounded compaction inside the treap-backed storage core
  - conservative maintenance triggering after real mutations
  - regression tests covering maintenance invariants
- This stage intentionally does not include:
  - line finger caches
  - decoded line caches
  - grapheme or visual-column caches
- Those remain deferred to later document/query or Unicode-aware layers.

## Stage 3: Introduce a dedicated Unicode module

Purpose:
Create one place for segmentation and position conversion logic before Unicode behavior becomes widespread.

Work:

- Add a `unicode` module with no Qt dependencies if possible.
- Start with narrow responsibilities:
  - validate or align byte offsets to code point boundaries
  - find previous and next grapheme boundaries within a line
  - convert between UTF-8 byte offsets and other position domains when needed
- Do not commit to ICU, HarfBuzz, or another library without asking the user first if it crosses a subsystem boundary or adds a new major dependency.
- If no external dependency is added yet, keep the module small and honest about what guarantees it does and does not provide.

Exit criteria:

- Grapheme and Unicode-sensitive operations no longer live directly inside `TextBuffer` or `EditorScrollArea`.
- There is one clear seam where future Unicode-library integration can happen.

## Stage 4: Make horizontal movement grapheme-aware

Purpose:
Fix the most visible cursor correctness issue without taking on bidirectional behavior yet.

Work:

- Change left/right movement to move by grapheme cluster, not by byte.
- Add APIs that return grapheme-aligned cursor positions.
- Make `home` and `end` return positions that are valid for later rendering and editing behavior.
- Preserve clamping guarantees.

Testing:

- Unit tests for movement across:
  - ASCII
  - multi-byte code points
  - combining marks
  - emoji with skin-tone modifiers
  - ZWJ sequences
  - Hangul and other non-Latin scripts
- Property tests ensuring movement never returns invalid positions and remains idempotent at boundaries.

Exit criteria:

- Left/right movement no longer steps into the middle of a grapheme cluster.
- Cursor rendering remains stable for the tested input classes.

## Stage 5: Add preferred-column behavior for vertical movement

Purpose:
Make up/down/page movement behave like an editor rather than merely clamping byte offsets line by line.

Work:

- Introduce a preferred visual column concept for cursor movement.
- Distinguish stored logical position from transient preferred visual movement state if needed.
- Use Unicode-aware boundaries when clamping movement to shorter lines.
- Keep this separate from bidirectional visual ordering for now.

Testing:

- Unit tests for moving vertically across lines with different script and grapheme widths.
- Tests that repeated up/down movement preserves intent better than naive byte-column clamping.

Exit criteria:

- Vertical movement is based on a stable user-facing column concept rather than only the current byte offset.

## Stage 6: Clarify logical vs visual cursor semantics

Purpose:
Prepare for bidirectional text without mixing buffer semantics and screen semantics.

Work:

- Define whether the editor stores cursor position in logical document order, visual order, or both.
- Introduce explicit types or terminology for:
  - logical cursor position
  - visual cursor position
  - display segment or shaped run if needed
- Decide where visual ordering belongs. It should not leak into `TextStorage`.
- Keep `TextStorage` and `TextBuffer` focused on document/logical semantics as long as possible.

Exit criteria:

- There is a documented model for logical versus visual cursor behavior.
- Right-to-left support can be added without guessing which APIs are in logical order and which are in visual order.

## Stage 7: Introduce bidirectional and shaping-aware services if needed

Purpose:
Support right-to-left text and other layout-sensitive behavior through an explicit service boundary.

Work:

- Decide whether Qt text layout alone is enough for the editor's needs or whether a dedicated Unicode/shaping service is required.
- If heavier dependencies such as ICU or HarfBuzz are needed outside the normal UI layout path, ask the user before introducing them.
- Add narrow services for:
  - bidirectional run analysis
  - logical-to-visual cursor mapping
  - visual-to-logical hit testing if needed
- Keep dependency direction disciplined:
  - `unicode` may depend on specialized libraries
  - `buffer` should depend on `unicode` abstractions, not directly on those libraries
  - `ui` may combine Qt layout and Unicode services, but should not absorb all logic itself

Testing:

- Mixed LTR and RTL line cases
- Cursor movement at run boundaries
- Home/end behavior policy for bidi lines
- Visual hit testing, if implemented

Exit criteria:

- RTL text no longer relies on accidental behavior from the current byte-based model.
- Logical and visual cursor behavior are both deliberate and tested.

## Stage 8: Integrate external position protocols carefully

Purpose:
Keep LSP, tree-sitter, and editor behavior consistent once multiple position domains exist.

Work:

- Add explicit conversion functions between:
  - buffer byte offsets
  - grapheme-aligned cursor positions
  - Qt UTF-16 positions
  - LSP positions
  - tree-sitter byte offsets
- Keep these conversions in dedicated modules or helpers, not scattered across call sites.
- Negotiate and document LSP position encoding explicitly.
- Keep tree-sitter on UTF-8 byte offsets unless there is a concrete reason to change.

Exit criteria:

- External integrations do not force the rest of the editor to abandon its own domain types.
- Position conversions are explicit, tested, and centralized.

## Recommended order of implementation

Recommended next steps from the current codebase:

1. Stage 1: solidify `TextStorage` and `TextBuffer` boundaries, including a likely path to a piece tree.
2. Stage 1.5: make `TextStorage` editable on the treap core.
3. Stage 1.6: recover storage-local performance regressions before broader editing work builds on the slower paths.
4. Stage 2: strengthen cursor and column domain types.
5. Stage 3: add a dedicated `unicode` module boundary.
6. Stage 4: make left/right grapheme-aware.
7. Stage 5: add preferred-column vertical movement.
8. Stage 6: document logical versus visual cursor semantics.
9. Stage 7: add bidi-aware services only when needed.
10. Stage 8: harden LSP and tree-sitter conversions against the new position model.

## Things to avoid

- Do not switch the whole buffer to `QString` just to avoid short-term conversions.
- Do not add ad hoc Unicode checks in multiple files.
- Do not let Qt UTF-16 indices become the only cursor model.
- Do not introduce ICU, HarfBuzz, or similar dependencies into the wrong layer without asking first.
- Do not conflate grapheme correctness with full right-to-left support. They are related, but not the same milestone.
- Do not force a future piece tree to preserve line-vector-shaped construction APIs if those APIs are only serving the current prototype.
