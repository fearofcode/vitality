#pragma once

#include <string_view>

#include "core/PositionConversions.h"

namespace vitality::unicode {

[[nodiscard]] ByteColumnAlignmentResult align_byte_column_to_code_point_boundary(
    std::string_view utf8_line,
    ByteColumn column);

[[nodiscard]] GraphemeBoundarySearchResult align_byte_column_to_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

[[nodiscard]] GraphemeBoundarySearchResult previous_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

[[nodiscard]] GraphemeBoundarySearchResult next_grapheme_boundary(
    std::string_view utf8_line,
    ByteColumn column);

[[nodiscard]] GraphemeColumnResult grapheme_column_at_byte_column(
    std::string_view utf8_line,
    ByteColumn column);

[[nodiscard]] GraphemeBoundarySearchResult grapheme_boundary_for_display_column(
    std::string_view utf8_line,
    GraphemeColumn column);

[[nodiscard]] ByteToQtColumnResult map_byte_column_to_qt_utf16(
    std::string_view utf8_line,
    ByteColumn column);

}  // namespace vitality::unicode
