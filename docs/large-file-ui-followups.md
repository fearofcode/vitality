# Large File UI Follow-Ups

These are known UI-layer issues for very large files. They are intentionally
deferred and should not drive core/storage type choices backward.

## QAbstractScrollArea absolute line exposure

Current state:

- `EditorScrollArea` exposes absolute document line numbers directly through the
  `QAbstractScrollArea` / `QScrollBar` interface.
- Qt scrollbars and related widget APIs are `int`-based.

Why this matters:

- This is workable for normal files, but it does not scale to documents whose
  line counts or logical positions exceed the practical range of Qt widget
  integers.
- The current UI model assumes a near-1:1 mapping between document line numbers
  and scrollbar values. That will break down for massive files even though the
  storage/core layers now use wider integer types.

Deferred direction:

- Keep storage and domain coordinates 64-bit.
- Later, replace the current absolute-line scrollbar model with an explicit
  viewport mapping layer that compresses very large logical document ranges into
  the smaller range supported by Qt scrollbars.
- Treat Qt scrollbar values as viewport control inputs, not as the editor's
  authoritative document coordinate system.

This is a follow-up item, not an immediate task.
