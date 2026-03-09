#pragma once

#include <string_view>

#include <QString>

#include "core/CoreTypes.h"
#include "core/PositionConversions.h"

namespace vitality {

using QtTextCursorMapping = ByteToQtColumnResult;

[[nodiscard]] QString utf8_to_qstring(std::string_view utf8_text);
// This file remains as a thin UI-facing adapter in Stage 3. Real Unicode
// alignment and UTF-8/UTF-16 mapping live in the unicode module.
[[nodiscard]] QtTextCursorMapping map_utf8_byte_column_to_qt_cursor(
    std::string_view utf8_text,
    ByteColumn byte_column);

}  // namespace vitality
