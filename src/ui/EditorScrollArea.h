#pragma once

#include <optional>

#include <QAbstractScrollArea>

#include "buffer/TextBuffer.h"
#include "core/CoreTypes.h"

namespace vitality {

class EditorScrollArea : public QAbstractScrollArea {
public:
    explicit EditorScrollArea(TextBuffer buffer, QWidget *parent = nullptr);

    // Narrow accessors for widget-state tests. Production code should rely on
    // buffer/status-bar behavior rather than reaching into editor state.
    [[nodiscard]] ByteCursorPos cursor_for_tests() const;
    [[nodiscard]] bool has_preferred_column_for_tests() const;
    [[nodiscard]] PreferredVisualColumn preferred_column_value_for_tests() const;
    [[nodiscard]] bool has_preferred_visual_x_for_tests() const;
    [[nodiscard]] VisualCursorX preferred_visual_x_value_for_tests() const;
    void set_cursor_for_tests(ByteCursorPos cursor);

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
    // Stage 5 keeps persistent preferred-column state in the editor view.
    // Horizontal movement stays buffer-owned, but vertical key handling uses
    // this state plus buffer-level grapheme mapping to place the cursor on
    // target lines. Stage 7 adds a second transient hint, preferred_visual_x_,
    // so vertical movement on bidi lines can preserve the on-screen x position
    // even though the stored cursor remains logical.
    [[nodiscard]] PreferredVisualColumn current_preferred_column() const;
    void reset_preferred_column_from_cursor();
    void clear_preferred_column();
    [[nodiscard]] std::optional<VisualCursorX> current_preferred_visual_x() const;
    void reset_preferred_visual_x_from_cursor();
    void clear_preferred_visual_x();
    [[nodiscard]] ByteCursorPos move_vertically_with_preferred_column(LineIndex target_line) const;

    // The view stores only logical cursor state. cursor_ is still the
    // canonical logical byte cursor for this editor view, not a visual-order
    // cursor. Stage 7 derives visual arrow/home/end behavior transiently
    // through the layout service one line at a time. Qt UTF-16 positions remain
    // transient mapping artifacts at the UI boundary instead of becoming
    // stored editor state.
    //
    // preferred_column_ is also view-local state, but it is not a visual
    // cursor. It is just the persistent grapheme display-column target used by
    // vertical navigation in logical line order. Stage 6 makes that distinction
    // explicit so future bidi work does not conflate display-column counting
    // with true visual-order cursor behavior.
    //
    // preferred_visual_x_ is a narrower Stage 7 addition. It is not a stored
    // visual cursor, only the last known on-screen x coordinate of the logical
    // caret in the current line layout. Vertical movement may use it when the
    // current line has bidi-aware visual ordering so "move up" or "move down"
    // aims for what is visually above or below the caret rather than merely
    // preserving a logical grapheme count.
    TextBuffer buffer_;
    ByteCursorPos cursor_;
    std::optional<PreferredVisualColumn> preferred_column_;
    std::optional<VisualCursorX> preferred_visual_x_;
    int SYSTEM_FONT_SIZE_PT = 18;
    int STATUS_BAR_INSET = 6;
};

}  // namespace vitality
