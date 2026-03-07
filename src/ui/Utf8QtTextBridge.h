#pragma once

#include <string_view>

#include <QString>

#include "core/CoreTypes.h"

namespace vitality {

struct QtCursorMapping {
    int qt_cursor_position = 0;
    ColumnIndex aligned_byte_column;
};

[[nodiscard]] QString utf8_to_qstring(std::string_view utf8_text);
[[nodiscard]] QtCursorMapping map_byte_column_to_qt_cursor(std::string_view utf8_text, ColumnIndex byte_column);

}  // namespace vitality
