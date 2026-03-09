#pragma once

#include <string_view>

#include "core/PositionConversions.h"

namespace vitality::unicode {

// These functions are the editor's line-local Unicode query surface. They all
// operate on one UTF-8 line at a time and return explicit result structs rather
// than throwing or silently inventing guarantees. The rest of the editor uses
// them to answer questions such as:
//
// - "is this byte column on a safe code-point boundary?"
// - "where is the previous or next grapheme stop on this line?"
// - "which displayed grapheme column does this logical byte cursor represent?"
// - "how do I convert this logical byte column into the UTF-16 column Qt uses?"
//
// The important architectural point is that these helpers are line-local and
// Qt-free. They do not know about stored editor state, vertical movement
// policy, visual bidi cursor order, or rendering widgets. They simply take one
// line of UTF-8 text and answer Unicode-sensitive questions about it.
//
// When the input is malformed UTF-8, these functions report failure through
// their result types so callers can choose a conservative fallback. That keeps
// Unicode-sensitive logic honest and prevents unrelated layers from smuggling
// ad hoc UTF-8 heuristics into the rest of the codebase.

// align_byte_column_to_grapheme_boundary takes an arbitrary line-relative byte
// column and returns the grapheme boundary at or before that position.
//
// This is the line-local answer to the question "if the current byte cursor is
// inside a grapheme cluster, which grapheme stop should I treat it as
// belonging to?" The result is stronger than ByteColumn because the returned
// column is guaranteed to sit on a grapheme boundary when the call succeeds.
//
// The implementation is allowed to align to a code-point boundary first and
// then use Unicode grapheme segmentation to decide which containing grapheme
// cluster owns that position. That makes the function useful for:
//
// - turning arbitrary byte cursors into grapheme-aligned logical cursors
// - deriving preferred columns from a cursor that might sit inside a cluster
// - stabilizing later visual-layout work that expects grapheme-safe stops
//
// If the input is malformed UTF-8, the function reports failure rather than
// manufacturing a grapheme guarantee it cannot actually justify.
[[nodiscard]] GraphemeBoundarySearchResult align_byte_column_to_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

// previous_grapheme_boundary returns the grapheme boundary immediately to the
// left of the given position in logical line order.
//
// The input does not have to be perfectly aligned already. The implementation
// may first normalize it to a safe interpretation and then ask for the
// preceding grapheme stop. This is the primitive the buffer uses for logical
// grapheme-aware "move left" behavior.
//
// The returned boundary is line-local and logical. It says nothing about bidi
// visual order on screen. Visual left/right behavior is a higher-level layout
// concern handled elsewhere.
//
// At line start, the function returns the line start boundary and succeeds.
// When UTF-8 is malformed, it reports failure so the caller can fall back to a
// conservative byte-based step if needed.
[[nodiscard]] GraphemeBoundarySearchResult previous_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

// next_grapheme_boundary returns the grapheme boundary immediately to the right
// of the given position in logical line order.
//
// This is the logical counterpart to previous_grapheme_boundary and is the
// primitive behind grapheme-aware "move right" behavior in the buffer. It uses
// the same line-local logical model: one UTF-8 line, one byte position, and a
// returned grapheme boundary in document order, not visual screen order.
//
// At line end, the function returns the line end boundary and succeeds. On
// malformed UTF-8, it reports failure rather than producing a result that might
// split a grapheme cluster or misinterpret the input bytes.
[[nodiscard]] GraphemeBoundarySearchResult next_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

// grapheme_column_at_byte_column converts a byte cursor position into the
// grapheme-based display-column model used by the editor's status bar and by
// preferred-column vertical movement.
//
// The returned GraphemeColumn is not a byte offset. It is the count of
// grapheme cursor stops from the start of the line:
//
// - column 0 means the start of the line
// - column 1 means after the first grapheme
// - column N means after the Nth grapheme
//
// This function exists because user-facing column reporting should match
// visible grapheme stops better than raw UTF-8 byte offsets do. It also gives
// vertical navigation a stable line-local column model that survives shorter
// and longer lines more naturally than byte columns.
//
// On malformed UTF-8, the function reports failure so callers can choose a
// fallback policy explicitly.
[[nodiscard]] GraphemeColumnResult grapheme_column_at_byte_column(
    std::string_view utf8_line,
    ByteColumn column);

// grapheme_boundary_for_display_column performs the inverse mapping of
// grapheme_column_at_byte_column.
//
// It answers the question "if I want the cursor stop for grapheme display
// column N on this line, which grapheme boundary byte offset is that?" This is
// the function that lets higher-level code preserve a preferred grapheme
// display column across different lines and still land on a grapheme-aligned
// byte cursor.
//
// If the requested display column is past the end of the line, the function
// clamps to the last grapheme boundary on the line and still succeeds. That
// behavior is important for editor navigation because a preferred column must
// remain meaningful even when some lines are shorter than others.
//
// If the line is malformed UTF-8, the function reports failure rather than
// claiming it knows which grapheme stop corresponds to the requested display
// column.
[[nodiscard]] GraphemeBoundarySearchResult grapheme_boundary_for_display_column(
    std::string_view utf8_line,
    GraphemeColumn column);

// map_byte_column_to_qt_utf16 converts the editor's logical byte-column model
// into the UTF-16 code-unit column model Qt text layout APIs use.
//
// This function exists because the editor stores logical cursors in UTF-8 byte
// space, while Qt's cursor and layout APIs speak UTF-16 positions. Keeping this
// mapping here avoids sprinkling ad hoc "decode a prefix and ask Qt for its
// length" logic throughout the UI.
//
// The result reports both:
//
// - the Qt UTF-16 column that should be used at the UI boundary
// - the actual byte column that was safe to use after code-point alignment
//
// The mapping remains line-local and does not introduce Qt types into this
// public header. QtUtf16Column is just a named value type in core. On malformed
// UTF-8 the function reports failure so the caller can render or navigate
// conservatively instead of assuming the conversion was trustworthy.
[[nodiscard]] ByteToQtColumnResult map_byte_column_to_qt_utf16(
    std::string_view utf8_line,
    ByteColumn column);

}  // namespace vitality::unicode
