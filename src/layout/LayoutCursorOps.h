#pragma once

#include <string_view>

#include "core/CoreTypes.h"
#include "core/PositionConversions.h"

namespace vitality::layout {

// This module is the editor's line-local visual cursor service. The buffer
// still owns logical cursor policy and stores the canonical cursor in logical
// document order, but that is not enough to answer questions like:
//
// - "what cursor stop is visually to the left of this Arabic grapheme run?"
// - "where is the visually leftmost edge of this mixed-direction code line?"
// - "what logical cursor corresponds to the on-screen x position directly above
//    the caret on the next line?"
//
// Those are layout questions, not storage questions. The code here sits above
// storage and the logical buffer APIs and below the editor widget's key
// handling. It works on one line at a time, uses the line's actual visual
// layout, and returns logical grapheme-aligned cursors so the editor can keep
// storing logical state even when the command semantics are visual.

// VisualCursorQuery packages the exact information the layout service needs to
// reason about one line's visual cursor behavior.
//
// The service needs:
//
// - the logical line index, so results can name the same line explicitly
// - the line's UTF-8 text, because the service performs line-local layout work
// - a logical grapheme-aligned cursor, because visual movement must never split
//   a grapheme cluster and should start from a cursor stop that the rest of the
//   editor can safely round-trip back into logical state
//
// The query is intentionally line-local. It does not know about the whole
// document, selections, viewport scrolling, or persistent editor state. Its
// job is narrower: given one line and one logical grapheme cursor on that line,
// determine the corresponding visual cursor information for that same line
// under the code-editor layout policy.
struct VisualCursorQuery {
    LineIndex line;
    std::string_view utf8_line;
    LogicalGraphemeCursorPos logical_cursor;
};

// LogicalVisualCursorResult is the richest successful mapping result in this
// header. It says:
//
// - which logical grapheme cursor the service ended up using
// - which visual cursor stop that corresponds to from left to right on screen
// - which line-local visual x position that stop occupies
//
// The result includes both logical and visual information because higher-level
// editor code often needs both at once. For example, a bidi-aware horizontal
// command wants the logical cursor to store after the move, while a vertical
// movement heuristic may also want the visual x-position so the next line can
// aim for what is visually above or below the caret.
//
// visual_cursor is not a pixel coordinate. It is the index of a visual cursor
// stop in the line's laid-out left-to-right screen order. visual_x is the
// line-local on-screen x position of that stop. Keeping those two ideas
// separate is important:
//
// - one is an ordering among valid cursor stops
// - the other is a geometric position the view can try to preserve
//
// The service reports failure explicitly if it cannot produce a trustworthy
// mapping, for example because the line is malformed UTF-8.
struct LogicalVisualCursorResult {
    LogicalGraphemeCursorPos logical_cursor;
    VisualCursorPos visual_cursor;
    VisualCursorX visual_x;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// LogicalCursorMappingResult is the lighter-weight result used by the visual
// movement commands. Those commands only need to know which logical grapheme
// cursor should be stored after the move; they do not necessarily need the full
// visual ordering metadata at the same time.
//
// This type exists so callers do not have to unpack irrelevant fields when
// they only need the next logical cursor stop after a visual left/right/home/end
// decision or after mapping a visual x-position back into a logical cursor.
struct LogicalCursorMappingResult {
    LogicalGraphemeCursorPos logical_cursor;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// logical_to_visual_cursor takes one logical grapheme cursor on one line and
// asks where that cursor stop lives in the line's visual order.
//
// Internally, the implementation is free to build the line layout, enumerate
// the valid logical grapheme cursor stops, and derive their visual left-to-right
// ordering. The public contract is simpler: on success, the caller gets both
// the corresponding visual cursor-stop index and the visual x-position for that
// logical cursor.
//
// This is the core "tell me how the stored logical cursor appears on screen"
// query. It is the foundation that the other movement helpers build on.
[[nodiscard]] LogicalVisualCursorResult logical_to_visual_cursor(const VisualCursorQuery &query);

// logical_cursor_for_visual_x answers the inverse question: given one line and
// one line-local visual x-position, which logical grapheme cursor stop best
// corresponds to that on-screen location?
//
// The editor uses this for vertical movement on bidi lines. When the user
// presses Up or Down, preserving only a logical grapheme display column is not
// enough to stay visually aligned in mixed-direction text. This function lets
// the view say "find the logical cursor on the target line that is visually
// closest to this x-position."
//
// The returned logical cursor is still grapheme-aligned and safe to store as
// the canonical cursor if the mapping succeeds.
[[nodiscard]] LogicalCursorMappingResult logical_cursor_for_visual_x(
    LineIndex line,
    std::string_view utf8_line,
    VisualCursorX visual_x);

// visual_left_cursor returns the logical grapheme cursor that is one visual
// cursor stop to the left of the queried cursor on screen.
//
// This function is the layout-aware answer to a keyboard Left command on a
// mixed-direction code line. It does not return a visual cursor because the
// editor still stores logical state. Instead, it uses visual ordering
// internally and returns the logical grapheme cursor that should become the new
// stored cursor after moving one stop to the visually left neighbor.
[[nodiscard]] LogicalCursorMappingResult visual_left_cursor(const VisualCursorQuery &query);

// visual_right_cursor is the symmetric counterpart to visual_left_cursor. It
// returns the logical grapheme cursor that is one visual cursor stop to the
// right of the queried cursor on screen.
//
// On purely left-to-right ASCII lines this often matches the ordinary logical
// next-grapheme behavior. On mixed-direction lines it may differ, because the
// visually right neighbor is not always the next logical grapheme stop in
// document order.
[[nodiscard]] LogicalCursorMappingResult visual_right_cursor(const VisualCursorQuery &query);

// visual_home_cursor returns the logical grapheme cursor at the visually
// leftmost edge of the laid-out line.
//
// This is deliberately a visual line-edge query rather than a logical
// line-start query. The difference matters on mixed-direction lines, where the
// line's visual left edge may not correspond to a simple "byte column 0" style
// interpretation of movement intent.
[[nodiscard]] LogicalCursorMappingResult visual_home_cursor(const VisualCursorQuery &query);

// visual_end_cursor returns the logical grapheme cursor at the visually
// rightmost edge of the laid-out line.
//
// Like visual_home_cursor, this describes the visible line edge under the
// code-editor layout policy rather than the next-best logical approximation.
// The caller still receives a logical grapheme cursor because that is what the
// editor stores after the move.
[[nodiscard]] LogicalCursorMappingResult visual_end_cursor(const VisualCursorQuery &query);

}  // namespace vitality::layout
