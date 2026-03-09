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

struct LineBoundaryMap {
    std::vector<std::int64_t> byte_boundaries;
    std::vector<std::int64_t> utf16_boundaries;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

[[nodiscard]] bool fits_in_int32(const std::size_t value) {
    return value <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
}

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

[[nodiscard]] std::int64_t clamped_byte_column_value(const std::string_view utf8_line, const ByteColumn column) {
    return std::clamp<std::int64_t>(column.value, 0, static_cast<std::int64_t>(utf8_line.size()));
}

[[nodiscard]] ByteColumn aligned_byte_boundary(const LineBoundaryMap &map, const std::int64_t clamped_column) {
    const auto upper = std::upper_bound(
        map.byte_boundaries.begin(),
        map.byte_boundaries.end(),
        clamped_column);
    const auto boundary_it = upper == map.byte_boundaries.begin() ? upper : std::prev(upper);
    return ByteColumn{*boundary_it};
}

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

[[nodiscard]] std::optional<icu::UnicodeString> unicode_string_from_valid_utf8(const std::string_view utf8_line) {
    if (!fits_in_int32(utf8_line.size())) {
        return std::nullopt;
    }

    return icu::UnicodeString::fromUTF8(
        icu::StringPiece(utf8_line.data(), static_cast<std::int32_t>(utf8_line.size())));
}

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

ByteColumnAlignmentResult align_byte_column_to_code_point_boundary(
    const std::string_view utf8_line,
    const ByteColumn column) {
    const LineBoundaryMap map = make_line_boundary_map(utf8_line);
    if (!map.success) {
        return ByteColumnAlignmentResult{
            .aligned_column = ByteColumn{},
            .success = false,
            .error = map.error,
        };
    }

    return ByteColumnAlignmentResult{
        .aligned_column = aligned_byte_boundary(map, clamped_byte_column_value(utf8_line, column)),
        .success = true,
        .error = UnicodeError::None,
    };
}

GraphemeBoundarySearchResult align_byte_column_to_grapheme_boundary(
    const std::string_view utf8_line,
    const ByteColumn column) {
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
