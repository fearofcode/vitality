# Current Unicode Support

This document describes what Vitality's Unicode and bidirectional-text behavior
actually does today. It is the current source of truth for the editor's Unicode
and bidi behavior.

At the time of writing, the core Unicode navigation and rendering goals from
Stages 1 through 7 have been completed. The editor is now a Unicode-aware file
viewer with logical cursor storage, grapheme-aware text navigation, and
line-local bidi-aware visual cursor behavior for code-editor lines.

## Canonical cursor model

The editor's stored cursor is still a logical document-order cursor.

That means the canonical cursor state is expressed as:

- a logical line index
- a UTF-8 byte column within that line

The editor does not store a visual-order cursor as the source of truth. Visual
cursor positions are derived transiently from line layout when they are needed
for bidi-aware keyboard behavior.

This is an intentional architectural decision. It keeps storage, buffer, and
external protocol integrations grounded in stable logical positions even when
the screen-order behavior on a mixed-direction line becomes more complicated.

## Storage and buffer text model

`TextStorage` stores text as UTF-8 and remains byte-preserving. It does not
validate UTF-8 eagerly and it does not own Unicode policy.

`TextBuffer` remains the owner of logical cursor policy and line-local document
queries. Unicode-sensitive line operations are delegated into the dedicated
`unicode` module rather than being reimplemented ad hoc in `buffer` or `ui`.

This means the current architecture is:

- `storage` stores bytes and exposes storage/query operations
- `buffer` owns logical document and cursor behavior
- `unicode` owns line-local Unicode segmentation and position conversion
- `layout` owns bidi-aware visual cursor behavior for code-editor line layout
- `ui` consumes those services and stores only logical cursor state plus
  transient editor-view navigation state

## Horizontal movement

Horizontal movement is grapheme-aware on valid UTF-8 text.

Concretely:

- `Left` and `Right` do not step into the middle of a grapheme cluster
- combining-mark sequences are treated as one cursor stop
- common emoji modifier sequences are treated as one cursor stop
- common zero-width-joiner emoji sequences are treated as one cursor stop

If Unicode grapheme analysis fails because a line is malformed UTF-8, the
editor falls back to the older byte-based horizontal behavior so navigation
remains total and the file stays viewable.

## Vertical movement

Vertical movement preserves a persistent preferred column, but that column is no
longer just a raw byte offset.

The main Stage 5 behavior is still present:

- `Up`, `Down`, `PageUp`, and `PageDown` preserve a grapheme-based logical
  display column across lines of different lengths
- if a shorter intermediate line forces the cursor leftward, the editor keeps
  the original preferred column and restores it when later lines are long
  enough

Stage 7 added one important refinement for mixed-direction lines:

- when line layout can provide a reliable bidi-aware visual x-position, the
  editor also preserves that transient visual x-position across vertical moves

This matters on lines that contain right-to-left runs inside a left-to-right
code-editor line. Without that extra hint, moving vertically from the visually
left edge of an Arabic run could land on a logical grapheme column that is not
actually above the caret on screen. The editor now prefers the visual x-position
first and falls back to the logical preferred column only when the layout query
fails.

The preferred-column state itself still belongs to the editor view, not the
document.

## Status-bar column model

The status bar's `Col` value is not a raw byte count anymore when Unicode
queries succeed.

Today it means a logical grapheme display column:

- column `1` means line start
- column `2` means after the first grapheme on the line
- and so on

This matches what users expect better for CJK text, combining sequences, and
emoji than reporting a UTF-8 byte offset would.

The status bar does not currently report a bidi visual-stop index. It stays on
the logical grapheme display-column model even when horizontal keyboard
movement is visual on mixed-direction lines.

## Bidirectional text behavior

Vitality now has bidi-aware visual cursor services for code-editor lines.

The current policy is intentionally code-editor-specific rather than generic
rich-text behavior:

- each line is laid out with a forced left-to-right base paragraph direction
- mixed-direction runs are then reordered within that left-to-right code line
- `Left`, `Right`, `Home`, and `End` operate visually on that laid-out line

This gives behavior that is closer to editors such as Sublime Text and CLion
for cases like:

- `// السلام عليكم`
- mixed English identifiers and Arabic comments
- punctuation followed by right-to-left text inside a code line

The result is:

- the line still behaves like a left-to-right code-editor line
- comment prefixes such as `// ` stay on the left
- the Arabic run is visually ordered within that line
- visual horizontal movement follows what the user sees on screen

The editor still stores the resulting cursor logically after the move.

## What is intentionally not implemented yet

The core Unicode text-viewing goals are done, but several follow-up areas are
still explicitly outside the current implementation.

Vitality does not yet implement:

- mouse hit testing that understands bidi visual cursor order
- bidi-aware selection behavior
- dual-caret or split-caret rendering at bidi boundaries
- a visual-order status-bar column model
- external protocol conversions for:
  - LSP positions
  - tree-sitter byte offsets
  - any richer editor protocol position model

Those topics belong to later work and should be treated as separate features,
not as unfinished hidden behavior inside the current viewer.

## Practical summary

The current editor can be described accurately as follows:

Vitality is a UTF-8, grapheme-aware, bidi-aware code-editor-style file viewer
that stores logical cursors, renders mixed-direction code lines under a forced
left-to-right layout policy, moves horizontally in visual order on bidi lines,
and preserves sensible grapheme/display-column intent during vertical movement.

That is the behavior future changes should preserve unless a later feature
explicitly changes the policy.
