#pragma once

#include <QAbstractScrollArea>

#include "buffer/TextBuffer.h"
#include "core/CoreTypes.h"

namespace vitality {

class EditorScrollArea : public QAbstractScrollArea {
public:
    explicit EditorScrollArea(TextBuffer buffer, QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    [[nodiscard]] int line_height() const;
    [[nodiscard]] int status_bar_height() const;
    [[nodiscard]] VisibleLineCount visible_line_count() const;
    [[nodiscard]] int visible_column_count() const;
    [[nodiscard]] int horizontal_scroll_offset_pixels() const;
    [[nodiscard]] int document_max_line_length() const;
    [[nodiscard]] QColor highlight_cursor_background() const;

    void refresh_scrollbars() const;
    void ensure_cursor_visible() const;
    void move_cursor(ByteCursorPos cursor);

    TextBuffer buffer_;
    ByteCursorPos cursor_;
    int SYSTEM_FONT_SIZE_PT = 18;
    int STATUS_BAR_INSET = 6;
};

}  // namespace vitality
