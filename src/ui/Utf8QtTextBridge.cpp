#include "ui/Utf8QtTextBridge.h"

#include <algorithm>

#include <QByteArrayView>

namespace vitality {

namespace {

[[nodiscard]] bool is_utf8_continuation_byte(const unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] int clamp_byte_column(std::string_view utf8_text, const ColumnIndex byte_column) {
    const int max_column = static_cast<int>(utf8_text.size());
    return std::clamp(byte_column.value, 0, max_column);
}

}  // namespace

QString utf8_to_qstring(const std::string_view utf8_text) {
    return QString::fromUtf8(QByteArrayView(utf8_text.data(), static_cast<qsizetype>(utf8_text.size())));
}

QtCursorMapping map_byte_column_to_qt_cursor(const std::string_view utf8_text, const ColumnIndex byte_column) {
    int aligned_byte_column = clamp_byte_column(utf8_text, byte_column);

    while (aligned_byte_column > 0 &&
           aligned_byte_column < static_cast<int>(utf8_text.size()) &&
           is_utf8_continuation_byte(static_cast<unsigned char>(utf8_text[aligned_byte_column]))) {
        --aligned_byte_column;
    }

    const QString prefix = QString::fromUtf8(
        QByteArrayView(utf8_text.data(), static_cast<qsizetype>(aligned_byte_column)));

    return QtCursorMapping{
        .qt_cursor_position = static_cast<int>(prefix.size()),
        .aligned_byte_column = ColumnIndex{aligned_byte_column},
    };
}

}  // namespace vitality
