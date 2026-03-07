#include "ui/StatusBarText.h"

#include <QByteArrayView>

namespace vitality {

QString make_status_bar_text(const TextBuffer &buffer, const CursorPos cursor) {
    auto file_label = QStringLiteral("[NO FILE]");
    if (buffer.has_file_path()) {
        const std::string_view display_name = buffer.display_name();
        file_label = QString::fromUtf8(
            QByteArrayView(display_name.data(), static_cast<qsizetype>(display_name.size())));
    }

    return QStringLiteral("%1  Ln %2, Col %3")
        .arg(file_label)
        .arg(cursor.line.value + 1)
        .arg(cursor.column.value + 1);
}

}  // namespace vitality
