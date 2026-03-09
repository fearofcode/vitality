#include "ui/Utf8QtTextMapper.h"

#include "unicode/UnicodeLineOps.h"

namespace vitality {

QString utf8_to_qstring(const std::string_view utf8_text) {
    return QString::fromUtf8(
        utf8_text.data(),  // NOLINT(bugprone-suspicious-stringview-data-usage)
        static_cast<qsizetype>(utf8_text.size()));
}

QtTextCursorMapping map_utf8_byte_column_to_qt_cursor(
    const std::string_view utf8_text,
    const ByteColumn byte_column) {
    return unicode::map_byte_column_to_qt_utf16(utf8_text, byte_column);
}

}  // namespace vitality
