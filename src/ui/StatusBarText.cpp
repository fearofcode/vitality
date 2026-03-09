#include "ui/StatusBarText.h"

#include <cstdint>

#include <QByteArrayView>

namespace vitality {

QString make_status_bar_text(const TextBuffer &buffer, const ByteCursorPos cursor) {
    auto file_label = QStringLiteral("[NO FILE]");
    if (buffer.has_file_path()) {
        const std::string_view display_name = buffer.display_name();
        file_label = QString::fromUtf8(
            QByteArrayView(display_name.data(), static_cast<qsizetype>(display_name.size())));
    }

    const auto display_column = buffer.display_column(cursor);
    const std::int64_t column_for_status = display_column.success
        ? display_column.column.value
        : cursor.column.value;

    return QStringLiteral("%1  Ln %2, Col %3")
        .arg(file_label)
        .arg(cursor.line.value + 1)
        .arg(column_for_status + 1);
}

}  // namespace vitality
