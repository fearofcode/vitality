#pragma once

#include <string_view>

#include <QString>

#include "core/CoreTypes.h"

namespace vitality {

struct QtTextCursorMapping {
    int qt_cursor_position = 0;
    ByteColumn aligned_byte_column;
};

[[nodiscard]] QString utf8_to_qstring(std::string_view utf8_text);
[[nodiscard]] QtTextCursorMapping map_utf8_byte_column_to_qt_cursor(
    std::string_view utf8_text,
    ByteColumn byte_column);

}  // namespace vitality
