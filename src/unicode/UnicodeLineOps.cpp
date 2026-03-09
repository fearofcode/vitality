#include "unicode/UnicodeLineOps.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/stringpiece.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>

namespace vitality::unicode {

namespace {

// LineBoundaryMap is the shared internal representation that ties together the
// two coordinate systems used in this file.
//
// The editor stores line positions as UTF-8 byte columns. ICU break iteration
// works in UTF-16 code-unit indices. Most functions in this file therefore
// need to translate one coordinate system into the other. Rather than decode
// the line differently in each function, we decode it once and record every
// code-point boundary in both spaces.
//
// byte_boundaries[i] and utf16_boundaries[i] describe the same logical place
// in the line. The vectors always start with 0 for the beginning of the line
// and end with the full line length for the end of the line.
struct LineBoundaryMap {
    std::vector<std::int64_t> byte_boundaries;
    std::vector<std::int64_t> utf16_boundaries;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

// ICU APIs in this file use 32-bit lengths and offsets. Vitality's domain
// types are wider because the editor core should not inherit that limit. We
// check the line length up front before handing anything to ICU.
[[nodiscard]] bool fits_in_int32(const std::size_t value) {
    return value <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
}

// Build the UTF-8 <-> UTF-16 boundary map for one line.
//
// The loop decodes one Unicode code point at a time with ICU's UTF-8 helper
// macro. After each decoded code point, we record:
// - the UTF-8 byte offset immediately after that code point
// - the UTF-16 code-unit offset immediately after that code point
//
// Those two offsets refer to the same logical boundary in the text. Later
// functions use the map to convert safe byte boundaries into UTF-16
// boundaries, and vice versa.
//
// If decoding fails at any point, the line is malformed UTF-8 and the rest of
// the Unicode-sensitive operations must fail honestly.
[[nodiscard]] LineBoundaryMap make_line_boundary_map(const std::string_view utf8_line) {
    if (!fits_in_int32(utf8_line.size())) {
        return LineBoundaryMap{
            .success = false,
            .error = UnicodeError::DependencyFailure,
        };
    }

    LineBoundaryMap map;
    map.byte_boundaries.push_back(0);
    map.utf16_boundaries.push_back(0);

    const char *bytes = utf8_line.data();
    const auto length = static_cast<std::int32_t>(utf8_line.size());
    std::int64_t utf16_offset = 0;

    for (std::int32_t byte_offset = 0; byte_offset < length;) {
        UChar32 code_point = 0;
        // ICU's UTF-8 iteration macro advances byte_offset and mutates the
        // temporary code point as part of the API contract. Clang-tidy sees
        // those macro internals as if they were local conditional logic.
        // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions,bugprone-assignment-in-if-condition)
        U8_NEXT(bytes, byte_offset, length, code_point);
        if (code_point < 0) {
            return LineBoundaryMap{
                .success = false,
                .error = UnicodeError::InvalidUtf8,
            };
        }

        utf16_offset += U16_LENGTH(code_point);
        map.byte_boundaries.push_back(byte_offset);
        map.utf16_boundaries.push_back(utf16_offset);
    }

    map.success = true;
    return map;
}

// Normalise an arbitrary byte column into the line's valid range. Public APIs
// can be asked about negative positions or positions beyond line end, so every
// operation starts by clamping into [0, line.size()].
[[nodiscard]] std::int64_t clamped_byte_column_value(const std::string_view utf8_line, const ByteColumn column) {
    return std::clamp<std::int64_t>(column.value, 0, static_cast<std::int64_t>(utf8_line.size()));
}

// Snap a clamped byte column backward to the nearest known code-point
// boundary. If the caller points into the middle of a multibyte UTF-8
// sequence, we must not decode or map from that interior byte position.
//
// upper_bound gives the first recorded boundary strictly greater than the
// requested byte column; stepping one entry back gives the last boundary at or
// before the requested position.
[[nodiscard]] ByteColumn aligned_byte_boundary(const LineBoundaryMap &map, const std::int64_t clamped_column) {
    const auto upper = std::upper_bound(
        map.byte_boundaries.begin(),
        map.byte_boundaries.end(),
        clamped_column);
    const auto boundary_it = upper == map.byte_boundaries.begin() ? upper : std::prev(upper);
    return ByteColumn{*boundary_it};
}

// Convert a safe UTF-8 byte boundary into the matching UTF-16 boundary using
// the precomputed map. This only succeeds for exact recorded boundaries,
// because the rest of the file deliberately refuses to cross coordinate
// systems at unsafe interior positions.
[[nodiscard]] std::optional<std::int64_t> utf16_for_byte_boundary(
    const LineBoundaryMap &map,
    const ByteColumn boundary) {
    const auto it = std::lower_bound(
        map.byte_boundaries.begin(),
        map.byte_boundaries.end(),
        boundary.value);
    if (it == map.byte_boundaries.end() || *it != boundary.value) {
        return std::nullopt;
    }

    const auto index = static_cast<std::size_t>(std::distance(map.byte_boundaries.begin(), it));
    return map.utf16_boundaries[index];
}

// Invert utf16_for_byte_boundary. ICU gives us grapheme boundaries in UTF-16
// positions; the editor needs to turn them back into UTF-8 byte boundaries.
[[nodiscard]] std::optional<GraphemeBoundaryByteColumn> byte_boundary_for_utf16(
    const LineBoundaryMap &map,
    const std::int32_t utf16_boundary) {
    const auto it = std::lower_bound(
        map.utf16_boundaries.begin(),
        map.utf16_boundaries.end(),
        static_cast<std::int64_t>(utf16_boundary));
    if (it == map.utf16_boundaries.end() || *it != utf16_boundary) {
        return std::nullopt;
    }

    const auto index = static_cast<std::size_t>(std::distance(map.utf16_boundaries.begin(), it));
    return GraphemeBoundaryByteColumn{map.byte_boundaries[index]};
}

// BreakIterator works on ICU UnicodeString, not std::string_view. This helper
// performs that conversion after the caller has already decided the line is
// small enough to hand to ICU.
[[nodiscard]] std::optional<icu::UnicodeString> unicode_string_from_valid_utf8(const std::string_view utf8_line) {
    if (!fits_in_int32(utf8_line.size())) {
        return std::nullopt;
    }

    return icu::UnicodeString::fromUTF8(
        icu::StringPiece(utf8_line.data(), static_cast<std::int32_t>(utf8_line.size())));
}

// Create the ICU iterator used for grapheme-cluster navigation. ICU calls this
// a "character" break iterator, but here "character" means an extended
// grapheme cluster suitable for caret movement, not a single code point.
[[nodiscard]] std::unique_ptr<icu::BreakIterator> make_character_break_iterator(const icu::UnicodeString &line) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> iterator(
        icu::BreakIterator::createCharacterInstance(icu::Locale::getRoot(), status));
    if (U_FAILURE(status) || !iterator) {
        return nullptr;
    }

    iterator->setText(line);
    return iterator;
}

// Small result helpers keep the actual Unicode logic readable. The interesting
// work in this file is coordinate conversion and ICU traversal, not repetitive
// result construction.
[[nodiscard]] GraphemeBoundarySearchResult invalid_grapheme_result(const UnicodeError error) {
    return GraphemeBoundarySearchResult{
        .success = false,
        .error = error,
    };
}

[[nodiscard]] GraphemeBoundarySearchResult grapheme_boundary_result(
    const GraphemeBoundaryByteColumn column) {
    return GraphemeBoundarySearchResult{
        .column = column,
        .success = true,
        .error = UnicodeError::None,
    };
}

// Convert one UTF-16 boundary reported by ICU back into the editor's
// grapheme-aligned byte-column model. If the map cannot translate the
// boundary, something has gone wrong in our assumptions or in an external
// dependency, so we surface DependencyFailure instead of guessing.
[[nodiscard]] GraphemeBoundarySearchResult grapheme_boundary_from_utf16(
    const LineBoundaryMap &map,
    const std::int32_t utf16_boundary) {
    const auto byte_boundary = byte_boundary_for_utf16(map, utf16_boundary);
    if (!byte_boundary.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    return grapheme_boundary_result(*byte_boundary);
}

[[nodiscard]] GraphemeColumnResult invalid_grapheme_column_result(const UnicodeError error) {
    return GraphemeColumnResult{
        .success = false,
        .error = error,
    };
}

[[nodiscard]] GraphemeColumnResult grapheme_column_result(const GraphemeColumn column) {
    return GraphemeColumnResult{
        .column = column,
        .success = true,
        .error = UnicodeError::None,
    };
}

}  // namespace

GraphemeBoundarySearchResult align_byte_column_to_grapheme_boundary(
    const std::string_view utf8_line,
    const ByteColumn column) {
    // This function answers "which grapheme cluster does this arbitrary byte
    // position belong to?" The answer is the grapheme boundary at or before
    // the requested byte column.
    //
    // The algorithm is:
    // 1. Build the UTF-8 <-> UTF-16 boundary map.
    // 2. Clamp the requested byte column into the line.
    // 3. Snap backward to a safe code-point boundary.
    // 4. Convert that safe byte boundary into the equivalent UTF-16 position.
    // 5. Ask ICU whether that UTF-16 position is already a grapheme boundary.
    // 6. If yes, return it.
    // 7. If no, ask ICU for the previous grapheme boundary and map that back
    //    to the editor's byte-column model.
    //
    // We align backward rather than forward because an interior byte position
    // conceptually belongs to the grapheme cluster the caret is already
    // inside. Later cursor code relies on that "containing grapheme" rule.
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return invalid_grapheme_result(map.error);
    }

    const ByteColumn aligned_column = aligned_byte_boundary(map, clamped_byte_column_value(utf8_line, column));
    const auto utf16_position = utf16_for_byte_boundary(map, aligned_column);
    if (!utf16_position.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    const auto unicode_line = unicode_string_from_valid_utf8(utf8_line);
    if (!unicode_line.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    std::unique_ptr<icu::BreakIterator> iterator = make_character_break_iterator(*unicode_line);
    if (!iterator) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    if (iterator->isBoundary(static_cast<std::int32_t>(*utf16_position))) {
        return grapheme_boundary_result(GraphemeBoundaryByteColumn{aligned_column.value});
    }

    const std::int32_t boundary = iterator->preceding(static_cast<std::int32_t>(*utf16_position));
    const std::int32_t safe_boundary = boundary == icu::BreakIterator::DONE ? 0 : boundary;
    return grapheme_boundary_from_utf16(map, safe_boundary);
}

GraphemeBoundarySearchResult previous_grapheme_boundary(
    const std::string_view utf8_line,
    const ByteColumn column) {
    // Find the grapheme boundary immediately before this logical byte
    // position. The input does not need to be aligned already; we first snap
    // it to a safe code-point boundary and then ask ICU for the previous
    // grapheme stop in logical line order.
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return invalid_grapheme_result(map.error);
    }

    const ByteColumn aligned_column = aligned_byte_boundary(map, clamped_byte_column_value(utf8_line, column));
    const auto utf16_position = utf16_for_byte_boundary(map, aligned_column);
    if (!utf16_position.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    if (*utf16_position <= 0) {
        return grapheme_boundary_result(GraphemeBoundaryByteColumn{});
    }

    const auto unicode_line = unicode_string_from_valid_utf8(utf8_line);
    if (!unicode_line.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    std::unique_ptr<icu::BreakIterator> iterator = make_character_break_iterator(*unicode_line);
    if (!iterator) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    const std::int32_t boundary = iterator->preceding(static_cast<std::int32_t>(*utf16_position));
    const std::int32_t safe_boundary = boundary == icu::BreakIterator::DONE ? 0 : boundary;
    return grapheme_boundary_from_utf16(map, safe_boundary);
}

GraphemeBoundarySearchResult next_grapheme_boundary(
    const std::string_view utf8_line,
    const ByteColumn column) {
    // Find the grapheme boundary immediately after this logical byte
    // position. The shape mirrors previous_grapheme_boundary:
    // - normalise to a safe code-point boundary
    // - convert to UTF-16
    // - ask ICU for the following grapheme boundary
    // - convert that UTF-16 boundary back into a byte column
    //
    // The explicit line-end fast path matters because the line end is already
    // a valid caret stop and ICU would otherwise report DONE.
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return invalid_grapheme_result(map.error);
    }

    const ByteColumn aligned_column = aligned_byte_boundary(map, clamped_byte_column_value(utf8_line, column));
    const auto utf16_position = utf16_for_byte_boundary(map, aligned_column);
    if (!utf16_position.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    if (*utf16_position >= map.utf16_boundaries.back()) {
        return grapheme_boundary_result(GraphemeBoundaryByteColumn{map.byte_boundaries.back()});
    }

    const auto unicode_line = unicode_string_from_valid_utf8(utf8_line);
    if (!unicode_line.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    std::unique_ptr<icu::BreakIterator> iterator = make_character_break_iterator(*unicode_line);
    if (!iterator) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    const std::int32_t boundary = iterator->following(static_cast<std::int32_t>(*utf16_position));
    const std::int32_t safe_boundary = boundary == icu::BreakIterator::DONE
        ? static_cast<std::int32_t>(map.utf16_boundaries.back())
        : boundary;
    return grapheme_boundary_from_utf16(map, safe_boundary);
}

GraphemeColumnResult grapheme_column_at_byte_column(
    const std::string_view utf8_line,
    const ByteColumn column) {
    // Convert a stored byte cursor into the editor's displayed grapheme
    // column. Conceptually this asks:
    //
    // "How many grapheme cursor stops are there from line start up to the
    // grapheme that contains this byte position?"
    //
    // We first align to the containing grapheme boundary. Then we iterate ICU
    // grapheme boundaries from the start of the line and count how many fall
    // strictly before that aligned position.
    const auto aligned_boundary = align_byte_column_to_grapheme_boundary(utf8_line, column);
    if (!aligned_boundary.success) {
        return invalid_grapheme_column_result(aligned_boundary.error);
    }

    const auto unicode_line = unicode_string_from_valid_utf8(utf8_line);
    if (!unicode_line.has_value()) {
        return invalid_grapheme_column_result(UnicodeError::DependencyFailure);
    }

    std::unique_ptr<icu::BreakIterator> iterator = make_character_break_iterator(*unicode_line);
    if (!iterator) {
        return invalid_grapheme_column_result(UnicodeError::DependencyFailure);
    }

    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return invalid_grapheme_column_result(map.error);
    }

    const auto utf16_position = utf16_for_byte_boundary(
        map,
        ByteColumn{aligned_boundary.column.value});
    if (!utf16_position.has_value()) {
        return invalid_grapheme_column_result(UnicodeError::DependencyFailure);
    }

    std::int64_t grapheme_count = 0;
    for (std::int32_t boundary = iterator->first();
         boundary != icu::BreakIterator::DONE && boundary < *utf16_position;
         boundary = iterator->next()) {
        ++grapheme_count;
    }

    return grapheme_column_result(GraphemeColumn{grapheme_count});
}

GraphemeBoundarySearchResult grapheme_boundary_for_display_column(
    const std::string_view utf8_line,
    const GraphemeColumn column) {
    // This is the inverse of grapheme_column_at_byte_column.
    //
    // Instead of asking "what displayed grapheme column is this byte cursor
    // on?", we ask "which grapheme boundary corresponds to displayed column
    // N?" That is the operation vertical preferred-column movement needs in
    // order to land on a target line at the same displayed grapheme column.
    //
    // We walk grapheme boundaries from the start of the line, count them, and
    // return the boundary that corresponds to the requested displayed column.
    // If the requested column is beyond line end, we clamp to the final
    // boundary so short lines still yield a usable caret position.
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return invalid_grapheme_result(map.error);
    }

    const auto unicode_line = unicode_string_from_valid_utf8(utf8_line);
    if (!unicode_line.has_value()) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    std::unique_ptr<icu::BreakIterator> iterator = make_character_break_iterator(*unicode_line);
    if (!iterator) {
        return invalid_grapheme_result(UnicodeError::DependencyFailure);
    }

    const std::int64_t requested_column = std::max<std::int64_t>(column.value, 0);
    std::int64_t current_column = 0;
    std::int32_t boundary = iterator->first();
    if (requested_column == 0) {
        return grapheme_boundary_from_utf16(map, boundary);
    }

    for (boundary = iterator->next();
         boundary != icu::BreakIterator::DONE;
         boundary = iterator->next()) {
        ++current_column;
        if (current_column >= requested_column) {
            return grapheme_boundary_from_utf16(map, boundary);
        }
    }

    return grapheme_boundary_result(GraphemeBoundaryByteColumn{map.byte_boundaries.back()});
}

ByteToQtColumnResult map_byte_column_to_qt_utf16(
    const std::string_view utf8_line,
    const ByteColumn column) {
    // This is the narrow bridge from the editor's UTF-8 byte-column model to
    // Qt's UTF-16 cursor-position model.
    //
    // The job here is intentionally simpler than the grapheme-aware functions
    // above. Qt only needs a code-point-safe UTF-16 position, not a grapheme
    // segmentation decision. So we:
    // 1. build the boundary map
    // 2. clamp the requested byte column into the line
    // 3. snap backward to a safe code-point boundary
    // 4. look up the matching UTF-16 boundary and return both values
    //
    // Returning the aligned byte column alongside the Qt column is useful
    // because callers often need to know which logical byte boundary the Qt
    // position actually corresponds to after alignment.
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return ByteToQtColumnResult{
            .success = false,
            .error = map.error,
        };
    }

    const ByteColumn aligned_column = aligned_byte_boundary(map, clamped_byte_column_value(utf8_line, column));
    const auto utf16_position = utf16_for_byte_boundary(map, aligned_column);
    if (!utf16_position.has_value()) {
        return ByteToQtColumnResult{
            .aligned_byte_column = aligned_column,
            .success = false,
            .error = UnicodeError::DependencyFailure,
        };
    }

    return ByteToQtColumnResult{
        .qt_column = QtUtf16Column{*utf16_position},
        .aligned_byte_column = aligned_column,
        .success = true,
        .error = UnicodeError::None,
    };
}

}  // namespace vitality::unicode
