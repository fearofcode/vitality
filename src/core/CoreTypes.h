#pragma once

#include <cstdint>

namespace vitality {

// LineIndex names one logical line in the document. It is zero-based and is
// always interpreted in logical document order, not in visual order.
struct LineIndex {
    std::int64_t value = 0;
};

// LineCount is a quantity of logical lines. It is distinct from LineIndex so
// APIs can say clearly whether they want "which line?" or "how many lines?".
struct LineCount {
    std::int64_t value = 0;
};

// VisibleLineCount is a view-facing capacity: "how many logical lines can this
// viewport show right now?" It is intentionally separate from the document's
// full LineCount.
struct VisibleLineCount {
    std::int64_t value = 0;
};

// ByteOffset is an absolute byte position in a whole UTF-8 document or storage
// buffer. It is not line-relative.
struct ByteOffset {
    std::int64_t value = 0;
};

// ByteCount is a size measured in bytes. Keeping counts separate from offsets
// makes mutation and range APIs easier to read and harder to misuse.
struct ByteCount {
    std::int64_t value = 0;
};

// ByteColumn is a line-relative UTF-8 byte offset. Today it is the active
// column coordinate used by storage and by the editor's canonical stored
// cursor. It is not a grapheme column, visual column, Qt UTF-16 column, or
// pixel x-coordinate.
struct ByteColumn {
    std::int64_t value = 0;
};

// ByteRange is a half-open absolute byte range: [start, start + length).
// Mutation APIs use this type when they need both a document/storage origin and
// a byte count.
struct ByteRange {
    ByteOffset start;
    ByteCount length;
};

// ByteCursorPos is the editor's current production cursor type. It names a
// logical document position using:
// - one logical line index
// - one byte column within that line
//
// This is intentionally a weaker invariant than the grapheme-aligned cursor
// types below. The editor can store this even when a cursor is not yet known to
// sit on a grapheme boundary.
struct ByteCursorPos {
    LineIndex line;
    ByteColumn column;
};

// The editor's canonical stored cursor is still logical and byte-based today.
// ByteCursorPos remains the active production cursor type, while the more
// explicit Logical* cursor names below exist so later bidi/layout work does not
// have to rely on inference from comments alone.
//
// In practice ByteCursorPos and LogicalByteCursorPos currently describe the
// same coordinate space. The difference is documentary: this type says the
// logical role out loud so future code can distinguish logical and visual
// cursor concepts in signatures instead of only in prose.
struct LogicalByteCursorPos {
    LineIndex line;
    ByteColumn column;
};

// Grapheme/logical cursor-domain types. GraphemeBoundaryCursorPos is the active
// stronger invariant in production code today: it means "logical cursor in
// document order, known to sit on a grapheme boundary." LogicalGraphemeCursorPos
// restates that same role explicitly for the future logical-vs-visual split.
//
// GraphemeBoundaryByteOffset is an absolute byte offset that is known to land
// exactly on a grapheme-cluster boundary. That stronger guarantee matters for
// cursor movement and editing operations that must not split a user-visible
// grapheme cluster.
struct GraphemeBoundaryByteOffset {
    std::int64_t value = 0;
};

// GraphemeBoundaryByteColumn is the line-relative version of the same idea: a
// UTF-8 byte column that is known to be on a grapheme boundary.
struct GraphemeBoundaryByteColumn {
    std::int64_t value = 0;
};

// GraphemeBoundaryCursorPos is the active stronger cursor invariant in the
// editor today. It means:
// - logical line position in document order
// - line-relative UTF-8 byte column
// - that byte column is known to be on a grapheme boundary
//
// This is the type used when movement or layout code needs a cursor that is
// safe to treat as a whole user-visible grapheme stop.
struct GraphemeBoundaryCursorPos {
    LineIndex line;
    GraphemeBoundaryByteColumn column;
};

// LogicalGraphemeCursorPos is the explicitly named version of the same logical
// grapheme-aligned concept. It exists so later layout and bidi code can speak
// about "logical grapheme cursor" and "visual cursor" as distinct notions
// without depending on surrounding commentary.
struct LogicalGraphemeCursorPos {
    LineIndex line;
    GraphemeBoundaryByteColumn column;
};

// PreferredVisualColumn stores the editor's persistent target display column
// for vertical movement. Even with visual cursor services, this type still
// means the logical grapheme display-column count used by the current vertical
// navigation model, not bidi visual-order position.
//
// In other words, this is "where the user wants vertical movement to keep
// aiming in the logical line's displayed grapheme columns," not "which visual
// bidi stop is currently active."
struct PreferredVisualColumn {
    std::int64_t value = 0;
};

// Grapheme column counts cursor stops from the start of a line. Unlike
// GraphemeBoundaryByteColumn, this is not a byte offset into UTF-8 text.
// Column 0 means line start, column 1 means after the first grapheme, and so
// on. This is the logical display-column model shown in the status bar when
// Unicode queries succeed.
struct GraphemeColumn {
    std::int64_t value = 0;
};

// Visual cursor types are line-local derived layout positions. VisualCursorPos
// means "which visual cursor stop from left to right in this laid-out
// code-editor line," not a pixel coordinate and not the logical grapheme
// display-column value shown in the status bar.
//
// VisualCursorColumn is therefore an index into the visual stop ordering of one
// laid-out line, not into document bytes and not into logical grapheme-count
// columns.
struct VisualCursorColumn {
    std::int64_t value = 0;
};

// VisualCursorX is a line-local x coordinate in the laid-out code-editor line.
// Unlike VisualCursorColumn, this is not "which visual cursor stop" but "where
// on screen, in line-local pixel space, this logical cursor ended up." The
// editor uses it as transient navigation state for vertical movement so bidi
// lines can move to what is visually above or below the caret without making
// the stored cursor itself visual.
//
// It is intentionally a view/layout artifact, not a persistent document
// coordinate.
struct VisualCursorX {
    std::int64_t value = 0;
};

// VisualCursorPos is the structured form of the visual-stop idea: one logical
// line index plus one visual cursor-stop index from left to right in that
// line's laid-out code-editor presentation.
//
// This is not stored as the canonical editor cursor. The editor still stores a
// logical cursor and derives VisualCursorPos transiently when visual bidi-aware
// commands need it.
struct VisualCursorPos {
    LineIndex line;
    VisualCursorColumn column;
};

// UI-boundary types. Qt text/layout APIs use UTF-16-style positions; keep that
// space explicit at the UI edge instead of passing raw ints around.
//
// QtUtf16Column is therefore a line-local UTF-16 code-unit column that belongs
// only at the layout/UI boundary. It should not leak into storage or logical
// buffer policy.
struct QtUtf16Column {
    std::int64_t value = 0;
};

}  // namespace vitality
