#pragma once

#include <string_view>

#include "core/CoreTypes.h"
#include "core/PositionConversions.h"

namespace vitality::layout {

// Stage 7 keeps the canonical stored cursor logical, but introduces real
// derived visual cursor services above storage/buffer. The query is line-local:
// one logical line of UTF-8 text plus one logical grapheme-aligned cursor on
// that line.
struct VisualCursorQuery {
    LineIndex line;
    std::string_view utf8_line;
    LogicalGraphemeCursorPos logical_cursor;
};

struct LogicalVisualCursorResult {
    LogicalGraphemeCursorPos logical_cursor;
    VisualCursorPos visual_cursor;
    VisualCursorX visual_x;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

struct LogicalCursorMappingResult {
    LogicalGraphemeCursorPos logical_cursor;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

[[nodiscard]] LogicalVisualCursorResult logical_to_visual_cursor(const VisualCursorQuery &query);
[[nodiscard]] LogicalCursorMappingResult logical_cursor_for_visual_x(
    LineIndex line,
    std::string_view utf8_line,
    VisualCursorX visual_x);
[[nodiscard]] LogicalCursorMappingResult visual_left_cursor(const VisualCursorQuery &query);
[[nodiscard]] LogicalCursorMappingResult visual_right_cursor(const VisualCursorQuery &query);
[[nodiscard]] LogicalCursorMappingResult visual_home_cursor(const VisualCursorQuery &query);
[[nodiscard]] LogicalCursorMappingResult visual_end_cursor(const VisualCursorQuery &query);

}  // namespace vitality::layout
