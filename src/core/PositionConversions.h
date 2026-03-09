#pragma once

#include <cstdint>

#include "core/CoreTypes.h"

namespace vitality {

// UnicodeError is the small shared failure vocabulary for Unicode-sensitive
// query and conversion helpers. The intent is to distinguish:
// - ordinary success
// - malformed UTF-8 in the source text
// - failure in an external dependency or layout/query mechanism
//
// It is intentionally narrow. Callers usually only need to know whether they
// can trust the stronger Unicode-related invariant they asked for.
enum class UnicodeError : std::uint8_t {
    None,
    InvalidUtf8,
    DependencyFailure,
};

// ByteToQtColumnResult describes one explicit boundary crossing:
// - input/editor side: UTF-8 byte column in one logical line
// - output/UI side: Qt UTF-16 code-unit column in that same line
//
// aligned_byte_column tells the caller which byte column was actually used
// after code-point-safe alignment. The result does not claim anything about
// grapheme boundaries by itself.
struct ByteToQtColumnResult {
    QtUtf16Column qt_column;
    ByteColumn aligned_byte_column;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// GraphemeBoundarySearchResult reports a successful search for a grapheme
// boundary within one line. The returned column is stronger than ByteColumn:
// callers may assume it lands exactly on a grapheme-cluster boundary.
struct GraphemeBoundarySearchResult {
    GraphemeBoundaryByteColumn column;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// GraphemeColumnResult reports a logical display-column count measured in
// grapheme cursor stops from the start of a line. This is the column model used
// by the status bar and by preferred-column vertical movement.
struct GraphemeColumnResult {
    GraphemeColumn column;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// GraphemeBoundaryCursorResult lifts the grapheme-boundary guarantee from a
// line-local column into a full logical cursor position.
struct GraphemeBoundaryCursorResult {
    GraphemeBoundaryCursorPos cursor;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// LogicalGraphemeCursorResult is the explicitly named logical version of the
// same idea. It is used when the code wants to make the logical-vs-visual role
// of the returned grapheme-aligned cursor explicit.
struct LogicalGraphemeCursorResult {
    LogicalGraphemeCursorPos cursor;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

}  // namespace vitality
